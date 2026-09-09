#include "page_dialplate.h"
#include "../Config/app_config.h"
#include "../Utils/page_manager.h"
#include "../app.h"

#include <stdio.h>
#include <stdlib.h>

/* Per-page private data (freed in dialplate_destroy). */
typedef struct {
    lv_obj_t *speed_label;   /* Big speed digit */
    lv_obj_t *dist_label;
    lv_obj_t *time_label;
} dial_priv_t;

/*
 * Build the dial plate: big speed in the center, distance/time
 * cards at the bottom, and a key hint line.
 */
static void dialplate_create(page_t *self)
{
    dial_priv_t *d = calloc(1, sizeof(dial_priv_t));
    self->user = d;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    /* Center: big speed digit */
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

    /* Bottom info cards: distance / time (placeholder) */
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

    /* Hint */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "OK:菜单  L/R:地图");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -44);
}

/*
 * Free the private data allocated in dialplate_create().
 */
static void dialplate_destroy(page_t *self)
{
    free(self->user);
    self->user = NULL;
}

/*
 * Key handling: ENTER opens settings, LEFT/RIGHT opens the map.
 */
static void dialplate_on_key(page_t *self, const key_event_t *ev)
{
    if (!ev->is_press)
        return;
    switch (ev->id) {
    case KEY_BTN_BACK:
        page_manager_pop();
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
        .on_key  = dialplate_on_key,
    },
};