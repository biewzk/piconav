#include "status_bar.h"
#include "gps_service.h"
#include "../app.h"
#include "../../HAL/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Colors match the GPS receiver page palette (dark outdoor theme). */
#define COL_BG     0x0A0E13
#define COL_LINE   0x223243
#define COL_TEXT   0xE8EEF4
#define COL_MUTE   0x8FA1B3
#define COL_OK     0x2EE06B
#define COL_WARN   0xFFB547
#define COL_DIM    0x263442
#define COL_BOLT   0xFFD54D

typedef struct {
    lv_obj_t  *time_label;
    lv_obj_t  *sig[4];      /* ascending GPS signal bars */
    lv_timer_t *timer;
} sb_priv_t;

/* Tiny lightning bolt inside the battery outline (USB powered). */
static const lv_point_t s_bolt_pts[] = {
    {5, 0}, {2, 5}, {4, 5}, {3, 8},
};

/* Signal bars: 4 green when fixed; while searching, 1 amber bar plus one
 * more per 6 satellites in view; dim when there is no fresh data. */
static void sb_refresh_gps(sb_priv_t *sb)
{
    const gps_data_t *g = app_gps();
    int lit = 0;
    uint32_t lit_col = COL_DIM;

    /* Staleness follows the sampler interval (see gps_service.h): a 5 s
     * sampling profile must not read as "no data". */
    if (gps_service_is_fresh(g)) {
        if (g->fix) {
            lit = 4;
            lit_col = COL_OK;
        } else {
            lit = g->sats_view ? 1 + g->sats_view / 6 : 1;
            if (lit > 4)
                lit = 4;
            lit_col = COL_WARN;
        }
    }

    for (int i = 0; i < 4; i++)
        lv_obj_set_style_bg_color(sb->sig[i],
                                  lv_color_hex(i < lit ? lit_col : COL_DIM), 0);
}

static void sb_timer_cb(lv_timer_t *timer)
{
    sb_priv_t *sb = timer->user_data;

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt)
        lv_label_set_text_fmt(sb->time_label, "%02d:%02d",
                              lt->tm_hour, lt->tm_min);
    sb_refresh_gps(sb);
}

static void sb_on_delete(lv_event_t *e)
{
    sb_priv_t *sb = lv_event_get_user_data(e);

    if (sb && sb->timer) {
        lv_timer_del(sb->timer);
        sb->timer = NULL;
    }
    free(sb);
}

/* Plain filled rectangle (no chrome). */
static lv_obj_t *sb_rect(lv_obj_t *parent, int x, int y, int w, int h,
                         uint32_t color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    return r;
}

lv_obj_t *status_bar_create(lv_obj_t *parent)
{
    sb_priv_t *sb = calloc(1, sizeof(*sb));
    if (!sb)
        return NULL;

    lv_obj_t *bar = sb_rect(parent, 0, 0, 240, STATUS_BAR_HEIGHT, COL_BG);

    /* Clock (device local time, like a phone) */
    sb->time_label = lv_label_create(bar);
    lv_label_set_text(sb->time_label, "--:--");
    lv_obj_set_style_text_font(sb->time_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sb->time_label, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(sb->time_label, 8, 4);

    /* GPS signal bars, bottom-aligned on the text line */
    static const uint8_t hs[4] = {4, 6, 8, 10};
    for (int i = 0; i < 4; i++) {
        sb->sig[i] = sb_rect(bar, 196 + i * 4, 16 - hs[i], 3, hs[i], COL_DIM);
        lv_obj_set_style_radius(sb->sig[i], 1, 0);
    }

    /* Battery outline + cap + bolt (static: USB powered for now; becomes
     * a level gauge once battery hardware exists) */
    lv_obj_t *batt = lv_obj_create(bar);
    lv_obj_set_size(batt, 13, 8);
    lv_obj_set_pos(batt, 218, 7);
    lv_obj_clear_flag(batt, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(batt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(batt, lv_color_hex(COL_MUTE), 0);
    lv_obj_set_style_border_width(batt, 1, 0);
    lv_obj_set_style_radius(batt, 2, 0);
    lv_obj_set_style_pad_all(batt, 0, 0);

    sb_rect(bar, 231, 10, 2, 3, COL_MUTE);

    lv_obj_t *bolt = lv_line_create(bar);
    lv_line_set_points(bolt, s_bolt_pts, 4);
    lv_obj_set_pos(bolt, 223, 7);
    lv_obj_set_style_line_color(bolt, lv_color_hex(COL_BOLT), 0);
    lv_obj_set_style_line_width(bolt, 1, 0);

    sb->timer = lv_timer_create(sb_timer_cb, 1000, sb);
    sb_timer_cb(sb->timer);

    lv_obj_add_event_cb(bar, sb_on_delete, LV_EVENT_DELETE, sb);
    return bar;
}
