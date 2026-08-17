#include "page_dialplate.h"
#include "page_livemap.h"
#include "page_settings.h"
#include "../Config/app_config.h"
#include "../app.h"

#include <stdio.h>

/* 页面私有数据 */
typedef struct {
    lv_obj_t *speed_label;   /* 速度大字 */
    lv_obj_t *dist_label;
    lv_obj_t *time_label;
    msg_sub_t *gps_sub;
} dial_priv_t;

static void dial_update_speed(dial_priv_t *d, const gps_data_t *g)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)g->speed_kmh);
    lv_label_set_text(d->speed_label, buf);

    lv_obj_set_style_text_color(d->speed_label,
        g->fix ? lv_color_hex(0xffffff) : lv_color_hex(0x555555), 0);

    if (g->fix) {
        char dbuf[16];
        snprintf(dbuf, sizeof(dbuf), "%.1f km", (g->speed_kmh * 0.1));
        lv_label_set_text(d->dist_label, dbuf);
    }
}

static void dial_on_gps(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    dial_priv_t *d = user;
    if (d->speed_label)
        dial_update_speed(d, data);
}

static void dialplate_create(page_t *self)
{
    dial_priv_t *d = calloc(1, sizeof(dial_priv_t));
    self->user = d;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    /* 中间: 速度大字 */
    d->speed_label = lv_label_create(scr);
    lv_label_set_text(d->speed_label, "0");
    lv_obj_set_style_text_font(d->speed_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(d->speed_label, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(d->speed_label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *unit = lv_label_create(scr);
    lv_label_set_text(unit, "km/h");
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align_to(unit, d->speed_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    /* 底部信息卡: 距离 / 时间 (占位) */
    d->dist_label = lv_label_create(scr);
    lv_label_set_text(d->dist_label, "0.0 km");
    lv_obj_set_style_text_font(d->dist_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d->dist_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(d->dist_label, LV_ALIGN_BOTTOM_LEFT, 16, -16);

    d->time_label = lv_label_create(scr);
    lv_label_set_text(d->time_label, "00:00");
    lv_obj_set_style_text_font(d->time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d->time_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(d->time_label, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    /* 提示 */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "OK:菜单  L/R:地图");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -44);
}

static void dialplate_destroy(page_t *self)
{
    dial_priv_t *d = self->user;
    if (d->gps_sub)
        msg_center_unsubscribe(app_msg_center(), MSG_GPS, d->gps_sub);
    free(self->user);
    self->user = NULL;
}

static void dialplate_on_show(page_t *self)
{
    dial_priv_t *d = self->user;
    if (!d->gps_sub)
        d->gps_sub = msg_center_subscribe(app_msg_center(), MSG_GPS, dial_on_gps, d);
}

static void dialplate_on_hide(page_t *self)
{
    dial_priv_t *d = self->user;
    if (d->gps_sub) {
        msg_center_unsubscribe(app_msg_center(), MSG_GPS, d->gps_sub);
        d->gps_sub = NULL;
    }
}

static void dialplate_on_key(page_t *self, const key_event_t *ev)
{
    if (!ev->is_press)
        return;
    switch (ev->id) {
    case KEY_BTN_ENTER:
        page_manager_push(&page_settings);
        break;
    case KEY_BTN_LEFT:
    case KEY_BTN_RIGHT:
        page_manager_push(&page_livemap);
        break;
    default:
        break;
    }
}

page_t page_dialplate = {
    .name = "dialplate",
    .ops  = &(page_ops_t){
        .create  = dialplate_create,
        .destroy = dialplate_destroy,
        .on_show = dialplate_on_show,
        .on_hide = dialplate_on_hide,
        .on_key  = dialplate_on_key,
    },
};