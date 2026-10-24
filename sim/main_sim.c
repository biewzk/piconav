#define _POSIX_C_SOURCE 200809L
#include "main_sim.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "../App/app.h"
#include "../App/Utils/log.h"
#include "../App/Utils/page_manager.h"
#include "../HAL/hal.h"

#define SIM_SCALE 2   /* 240x320 -> 480x640 window */
#define SIM_HOR    240
#define SIM_VER    320

static SDL_Window   *s_win;
static SDL_Renderer *s_renderer;
static SDL_Texture  *s_tex;
static int s_quit;
static uint32_t s_autoquit_ms;   /* 0 = off; else auto-exit after this many ms (headless test) */
static uint32_t s_start_ms;

void sim_set_quit(int q) { s_quit = q; }
int  sim_is_quit(void)   { return s_quit; }

int hal_should_quit(void)
{
    if (s_quit)
        return 1;
    if (s_autoquit_ms && (hal_tick_get() - s_start_ms) > s_autoquit_ms)
        return 1;
    return 0;
}

/* Headless test: key name -> SDL keycode for the scheduled injector
 * below (events go through hal_key_read -> bus dispatch, same path as
 * on-device). */
static SDL_Keycode inject_name_to_key(const char *name)
{
    static const struct { const char *n; SDL_Keycode k; } map[] = {
        { "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT },
        { "right", SDLK_RIGHT }, { "enter", SDLK_RETURN }, { "back", SDLK_ESCAPE },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(map[i].n, name) == 0)
            return map[i].k;
    return SDLK_UNKNOWN;
}

/*
 * Scheduled key injection for headless tests. Events are pushed as SDL
 * events from an lv_timer DURING app_run (not before it), because the
 * page manager's long-press detection only advances while
 * lv_timer_handler runs. SIM_KEY tokens: "up/down/left/right/enter/
 * back", optionally ":<hold_ms>" (default 60 ms = short click; e.g.
 * "enter:800" holds ENTER for 800 ms and exercises the long-press
 * gesture). Consecutive tokens are spaced SIM_KEY_MS apart.
 */
#define INJECT_MAX   64
#define INJECT_T0_MS 300   /* settle time before the first key */

typedef struct {
    uint32_t   at_ms;   /* absolute time from injection start */
    SDL_Keycode key;
    int        press;
} inject_step_t;

static inject_step_t s_isteps[INJECT_MAX];
static int s_istep_n, s_istep_i;
static uint32_t s_inject_t0 = UINT32_MAX;

static void inject_step_add(uint32_t at, SDL_Keycode k, int press)
{
    if (s_istep_n >= INJECT_MAX)
        return;
    s_isteps[s_istep_n].at_ms = at;
    s_isteps[s_istep_n].key = k;
    s_isteps[s_istep_n].press = press;
    s_istep_n++;
}

static void inject_timer_cb(lv_timer_t *timer)
{
    if (s_inject_t0 == UINT32_MAX)
        s_inject_t0 = SDL_GetTicks();
    uint32_t now = SDL_GetTicks() - s_inject_t0;

    while (s_istep_i < s_istep_n && s_isteps[s_istep_i].at_ms <= now) {
        inject_step_t *s = &s_isteps[s_istep_i++];
        SDL_Event ev = {0};
        ev.type = s->press ? SDL_KEYDOWN : SDL_KEYUP;
        ev.key.keysym.sym = s->key;
        SDL_PushEvent(&ev);
        LOG_I("sim: injected %s '%s' @%ums", s->press ? "press" : "release",
              SDL_GetKeyName(s->key), (unsigned)now);
    }
    if (s_istep_i >= s_istep_n)
        lv_timer_del(timer);
}

/* Headless test: dump the framebuffer to PPM before auto-exit,
 * for checking render output. */
static void sim_dump_ppm(lv_disp_t *disp, const char *path)
{
    if (!disp || !disp->driver->draw_buf)
        return;
    lv_disp_draw_buf_t *db = disp->driver->draw_buf;
    lv_color_t *p = db->buf1;
    if (!p)
        return;

    /* Force a full-screen refresh first: with partial rendering, buf1 only
     * holds the last refreshed area at offset 0, so a raw dump of it would
     * show stale pixels everywhere else. */
    if (lv_scr_act()) {
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(disp);
    }

    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", SIM_HOR, SIM_VER);
    for (int y = 0; y < SIM_VER; y++) {
        for (int x = 0; x < SIM_HOR; x++) {
            lv_color_t c = p[y * SIM_HOR + x];
            unsigned char r = (c.ch.red << 3) | (c.ch.red >> 2);
            unsigned char g = (c.ch.green << 2) | (c.ch.green >> 4);
            unsigned char b = (c.ch.blue << 3) | (c.ch.blue >> 2);
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    }
    fclose(f);
    fprintf(stderr, "sim: dumped %s\n", path);
}

/* LVGL flush callback: blit the dirty area to the SDL texture. */
static void sim_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    SDL_Rect r = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(s_tex, &r, color_p, w * sizeof(lv_color_t));

    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_tex, NULL, NULL);
    SDL_RenderPresent(s_renderer);

    lv_disp_flush_ready(drv);
}

/*
 * Simulator entry point: setup SDL + LVGL, run app_init/app_run,
 * optionally inject keys (SIM_KEY) and dump the frame (SIM_DUMP_PPM)
 * for headless testing. SDL window close sets the quit flag.
 */
int main(int argc, char *argv[])
{
    if (argc > 1)
        setenv("SIM_NMEA", argv[1], 1);
    else
        setenv("SIM_NMEA", "sample.nmea", 1);

    const char *aq = getenv("SIM_AUTOQUIT_MS");
    if (aq)
        s_autoquit_ms = (uint32_t)strtoul(aq, NULL, 10);

    log_init();
    LOG_I("pico_nav simulator starting (SDL, NMEA=%s)", getenv("SIM_NMEA"));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    s_start_ms = SDL_GetTicks();

    s_win = SDL_CreateWindow("Pico Nav Sim",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SIM_HOR * SIM_SCALE, SIM_VER * SIM_SCALE,
                             SDL_WINDOW_SHOWN);
    s_renderer = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    s_tex = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, SIM_HOR, SIM_VER);

    lv_init();
    lv_disp_draw_buf_t draw_buf;
    lv_color_t *buf1 = malloc(SIM_HOR * SIM_VER * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SIM_HOR * SIM_VER);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = sim_flush_cb;
    disp_drv.hor_res  = SIM_HOR;
    disp_drv.ver_res  = SIM_VER;
    lv_disp_drv_register(&disp_drv);

    app_init();
    LOG_I("app_init done, entering main loop");

    const char *key = getenv("SIM_KEY");
    if (key) {
        char buf[128];
        strncpy(buf, key, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        uint32_t interval_ms = 400;
        const char *iv = getenv("SIM_KEY_MS");
        if (iv)
            interval_ms = (uint32_t)strtoul(iv, NULL, 10);

        uint32_t t = INJECT_T0_MS;
        char *tok = strtok(buf, ",");
        while (tok) {
            uint32_t hold_ms = 60;
            char *colon = strchr(tok, ':');
            if (colon) {
                *colon = 0;
                hold_ms = (uint32_t)strtoul(colon + 1, NULL, 10);
            }
            SDL_Keycode k = inject_name_to_key(tok);
            if (k != SDLK_UNKNOWN) {
                inject_step_add(t, k, 1);
                inject_step_add(t + hold_ms, k, 0);
            } else {
                LOG_E("SIM_KEY unknown: %s", tok);
            }
            t += hold_ms + interval_ms;
            tok = strtok(NULL, ",");
        }
        lv_timer_create(inject_timer_cb, 20, NULL);
    }

    app_run();

    LOG_I("pico_nav simulator exit");

    const char *dump = getenv("SIM_DUMP_PPM");
    if (dump)
        sim_dump_ppm(lv_disp_get_default(), dump);

    SDL_DestroyTexture(s_tex);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}

/* LV_TICK_CUSTOM_SYS_TIME_EXPR: LVGL tick source. */
uint32_t custom_tick_get(void)
{
    return hal_tick_get();
}