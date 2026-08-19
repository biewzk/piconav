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

/* Headless test: inject one key event (goes through hal_key_read ->
 * bus dispatch, same path as on-device).
 * SIM_KEY="right" or "left/up/down/enter/back". */
static void sim_inject_key(const char *name)
{
    SDL_Event ev = {0};
    static const struct { const char *n; SDL_Keycode k; } map[] = {
        { "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT },
        { "right", SDLK_RIGHT }, { "enter", SDLK_RETURN }, { "back", SDLK_ESCAPE },
    };
    SDL_Keycode k = SDLK_UNKNOWN;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(map[i].n, name) == 0) { k = map[i].k; break; }
    if (k == SDLK_UNKNOWN) {
        LOG_E("SIM_KEY unknown: %s", name);
        return;
    }
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = k;
    SDL_PushEvent(&ev);
    ev.type = SDL_KEYUP;
    SDL_PushEvent(&ev);
    LOG_I("sim: injected key '%s'", name);
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
        char buf[64];
        strncpy(buf, key, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *tok = strtok(buf, ",");
        uint32_t base = SDL_GetTicks();
        uint32_t interval_ms = 400;
        const char *iv = getenv("SIM_KEY_MS");
        if (iv)
            interval_ms = (uint32_t)strtoul(iv, NULL, 10);
        while (tok) {
            while ((int)(SDL_GetTicks() - base) < (int)interval_ms)
                SDL_Delay(10);
            sim_inject_key(tok);
            base = SDL_GetTicks();
            tok = strtok(NULL, ",");
        }
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