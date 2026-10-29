#include "app_runtime.h"

#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"

#include "../Config/app_config.h"
#include "../Utils/app_services.h"
#include "../Utils/config_watcher.h"
#include "../Utils/gps_service.h"
#include "../Utils/log.h"
#include "../Utils/msg_center.h"
#include "../Utils/page_manager.h"
#include "../../HAL/hal.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SELF_DIR_MAX 256
/* Safety net: a page may own long ENTER (the map zooms out with it), so
 * holding ENTER this long on the root page always leaves the app. Kept
 * below 5 s so the webctl key injector (hold cap 5 s) can test it. */
#define FORCE_QUIT_HOLD_MS 4000

static char s_self_dir[SELF_DIR_MAX];
static int  s_quit;
static uint32_t s_enter_ms;
static int  s_enter_held;

const char *app_self_dir(void)
{
    return s_self_dir;
}

const char *app_plugin_dir(void)
{
    return s_self_dir;
}

/*
 * Resolve the installed directory from /proc/self/exe (robust when the
 * launcher execs by absolute path) and make it the working directory so
 * resource paths relative to the package work.
 */
static void resolve_self_dir(void)
{
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);

    if (n <= 0) {
        s_self_dir[0] = 0;
        return;
    }
    exe[n] = 0;

    char *slash = strrchr(exe, '/');
    if (!slash) {
        s_self_dir[0] = 0;
        return;
    }
    *slash = 0;
    snprintf(s_self_dir, sizeof s_self_dir, "%s", exe);
    if (chdir(s_self_dir) != 0)
        LOG_W("runtime: cannot chdir to %s", s_self_dir);
}

int app_preview_write(const char *text)
{
    char tmp[320], dst[320];
    FILE *f;

    if (!s_self_dir[0] || !text)
        return -1;
    snprintf(tmp, sizeof tmp, "%s/preview.txt.tmp", s_self_dir);
    snprintf(dst, sizeof dst, "%s/preview.txt", s_self_dir);
    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    fclose(f);
    /* rename() is atomic on UBIFS: the launcher never reads a torn file */
    return rename(tmp, dst);
}

/* Long ENTER on the root page: nothing below it, so leave the app. */
static void root_pop_cb(void)
{
    LOG_I("runtime: back on the root page, exiting");
    s_quit = 1;
}

/* Same tick source as the launcher (LV_TICK_CUSTOM_SYS_TIME_EXPR). */
uint32_t custom_tick_get(void)
{
    return hal_tick_get();
}

static void display_init(void)
{
    static lv_color_t buf[APP_DISP_BUF];
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    fbdev_init();

    lv_disp_draw_buf_init(&disp_buf, buf, NULL, APP_DISP_BUF);
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res  = APP_SCR_HOR;
    disp_drv.ver_res  = APP_SCR_VER;
    lv_disp_drv_register(&disp_drv);
}

int app_runtime_run(const app_runtime_cfg_t *cfg)
{
    msg_center_t *mc;

    if (!cfg || !cfg->register_pages || !cfg->root_page) {
        fprintf(stderr, "app_runtime: bad configuration\n");
        return 2;
    }

    resolve_self_dir();
    log_init();
    LOG_I("app '%s' starting (dir %s)",
          cfg->name ? cfg->name : "?", s_self_dir);

    lv_init();
    display_init();

    mc = msg_center_create();
    app_services_bind(mc);

    page_manager_init(mc);
    page_manager_set_root_pop_cb(root_pop_cb);

    if (hal_key_init() != 0)
        LOG_W("runtime: keys unavailable");

    if (cfg->use_gps) {
        if (hal_gps_init() != 0)
            LOG_W("runtime: GPS unavailable");
        gps_service_init(mc);
    }
    if (cfg->use_config_watcher)
        config_watcher_init(mc);

    cfg->register_pages();
    page_manager_open(cfg->root_page, NULL);
    if (cfg->after_start)
        cfg->after_start();
    LOG_I("app '%s' ready (depth=%d)", cfg->name ? cfg->name : "?",
          page_manager_depth());

    while (!s_quit && !hal_should_quit()) {
        key_event_t ev;

        while (hal_key_read(&ev)) {
            LOG_D("key event: id=%d press=%d", ev.id, ev.is_press);
            if (ev.id == KEY_BTN_ENTER && !ev.is_long) {
                if (ev.is_press) {
                    if (!s_enter_held) {
                        s_enter_held = 1;
                        s_enter_ms = hal_tick_get();
                    }
                } else {
                    s_enter_held = 0;
                }
            }
            msg_center_publish(mc, MSG_KEY, &ev);
        }

        if (s_enter_held && page_manager_depth() <= 1 &&
            hal_tick_get() - s_enter_ms >= FORCE_QUIT_HOLD_MS) {
            LOG_W("runtime: ENTER held %d ms at the root, force quit",
                  FORCE_QUIT_HOLD_MS);
            break;
        }

        lv_timer_handler();
        hal_delay_ms(5);
    }

    LOG_I("app '%s' exiting", cfg->name ? cfg->name : "?");
    return 0;
}
