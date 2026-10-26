/*
 * stopwatch - example pico_nav plugin (PLUGIN_SDK.md)
 *
 * A minimal but complete plugin: one page registered into the page
 * registry and the drawer, an lv_timer-driven stopwatch. ENTER toggles
 * start/stop, DOWN resets, held ENTER pops via the page manager's
 * default back gesture. Demonstrates the plugin contract (ABI/name/
 * version/init) and plain LVGL usage - no services touched.
 */
#include "plugin_api.h"

#include <lvgl.h>
#include <stdlib.h>

#define TIMER_PERIOD_MS 30   /* centisecond display refresh */

typedef struct {
    lv_obj_t   *time_label;
    lv_obj_t   *state_label;
    lv_timer_t *timer;
    int         running;
    uint32_t    accumulated_ms;   /* elapsed before the current run */
    uint32_t    start_ms;         /* tick when the current run began */
} stopwatch_priv_t;

static uint32_t stopwatch_elapsed_ms(const stopwatch_priv_t *d)
{
    uint32_t now = d->running ? lv_tick_get() : 0;
    return d->accumulated_ms + (d->running ? now - d->start_ms : 0);
}

static void stopwatch_paint(stopwatch_priv_t *d)
{
    uint32_t ms = stopwatch_elapsed_ms(d);
    uint32_t cs = (ms % 1000) / 10;
    uint32_t s  = ms / 1000;

    lv_label_set_text_fmt(d->time_label, "%02u:%02u.%02u",
                          (unsigned)(s / 60), (unsigned)(s % 60),
                          (unsigned)cs);
    lv_label_set_text(d->state_label, d->running ? "RUNNING" : "STOPPED");
}

static void stopwatch_timer_cb(lv_timer_t *timer)
{
    stopwatch_paint(timer->user_data);
}

static void stopwatch_start_stop(stopwatch_priv_t *d)
{
    if (d->running) {
        d->accumulated_ms += lv_tick_get() - d->start_ms;
        d->running = 0;
    } else {
        d->start_ms = lv_tick_get();
        d->running = 1;
    }
    stopwatch_paint(d);
}

static void stopwatch_create(page_t *self)
{
    stopwatch_priv_t *d = calloc(1, sizeof(*d));
    self->user = d;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Stopwatch");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    d->time_label = lv_label_create(scr);
    lv_label_set_text(d->time_label, "00:00.00");
    lv_obj_set_style_text_font(d->time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(d->time_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(d->time_label, LV_ALIGN_CENTER, 0, -10);

    d->state_label = lv_label_create(scr);
    lv_label_set_text(d->state_label, "STOPPED");
    lv_obj_set_style_text_font(d->state_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d->state_label, lv_color_hex(0x00ff88), 0);
    lv_obj_align(d->state_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "OK:start/stop  DOWN:reset");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    d->timer = lv_timer_create(stopwatch_timer_cb, TIMER_PERIOD_MS, d);
    stopwatch_paint(d);
}

static void stopwatch_destroy(page_t *self)
{
    stopwatch_priv_t *d = self->user;

    if (d) {
        if (d->timer)
            lv_timer_del(d->timer);
        free(d);
    }
    self->user = NULL;
}

static void stopwatch_on_show(page_t *self)
{
    stopwatch_paint(self->user);
}

static void stopwatch_on_key(page_t *self, const key_event_t *ev)
{
    stopwatch_priv_t *d = self->user;

    if (!ev->is_press)
        return;
    switch (ev->id) {
    case KEY_BTN_ENTER:
        if (!ev->is_long)
            stopwatch_start_stop(d);
        break;
    case KEY_BTN_DOWN:
        if (d->running)
            d->start_ms = lv_tick_get();
        d->accumulated_ms = 0;
        stopwatch_paint(d);
        break;
    default:
        break;
    }
}

static const page_ops_t stopwatch_ops = {
    .create  = stopwatch_create,
    .destroy = stopwatch_destroy,
    .on_show = stopwatch_on_show,
    .on_hide = NULL,
    .on_key  = stopwatch_on_key,
};

static page_t stopwatch_page = {
    .name  = "stopwatch",
    .ops   = &stopwatch_ops,
};

/* ---- plugin contract ---- */

/* Drawer icon from the host (app_icon_* symbols are exported; see
 * PLUGIN_SDK.md for the available icons). */
extern const lv_img_dsc_t app_icon_stopwatch_img;

const uint32_t plugin_abi_version = PLUGIN_ABI_VERSION;
const char *plugin_name    = "stopwatch";
const char *plugin_version = "1.2.0";

int plugin_init(void)
{
    static const app_entry_t entry = {
        .name = "\xE7\xA7\x92\xE8\xA1\xA8",   /* UI label (UTF-8) */
        .icon = "T",
        .icon_color = 0xFFD600,
        .page = &stopwatch_page,
        .img  = &app_icon_stopwatch_img,
        .info = "OK\nDOWN",
        .data = "Start/Stop\nReset",
    };

    page_register(&stopwatch_page);
    if (app_registry_add(&entry) < 0)
        return -1;
    return 0;
}
