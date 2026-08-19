#include "page_settings.h"
#include "../Config/app_config.h"

#include <stdlib.h>

#define SETTINGS_NUM 4

static const char *s_items[SETTINGS_NUM] = {
    "亮度", "单位", "路线偏好", "关于",
};

typedef struct {
    lv_obj_t *labels[SETTINGS_NUM];
    int       sel;
} settings_priv_t;

/* Repaint the selected item highlight. */
static void settings_paint(settings_priv_t *d)
{
    for (int i = 0; i < SETTINGS_NUM; i++) {
        lv_obj_set_style_text_color(d->labels[i],
            i == d->sel ? lv_color_hex(0x00c2ff) : lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_bg_color(d->labels[i],
            i == d->sel ? lv_color_hex(0x223344) : lv_color_hex(0x000000), 0);
    }
}

/*
 * Build the settings page: title, selectable item list and hint.
 */
static void settings_create(page_t *self)
{
    settings_priv_t *d = calloc(1, sizeof(*d));
    d->sel = 0;
    self->user = d;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "设置");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    for (int i = 0; i < SETTINGS_NUM; i++) {
        lv_obj_t *it = lv_label_create(scr);
        lv_label_set_text(it, s_items[i]);
        lv_obj_set_style_text_font(it, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_all(it, 8, 0);
        lv_obj_set_style_border_width(it, 0, 0);
        lv_obj_align(it, LV_ALIGN_TOP_LEFT, 16, 70 + i * 42);
        lv_obj_set_width(it, 208);
        d->labels[i] = it;
    }

    settings_paint(d);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "UP/DOWN:选择  OK:确认  LEFT:返回");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/*
 * Free the private data allocated in settings_create().
 */
static void settings_destroy(page_t *self)
{
    free(self->user);
    self->user = NULL;
}

/*
 * Key handling: UP/DOWN moves the selection, LEFT pops the page,
 * ENTER is a placeholder for future settings handlers.
 */
static void settings_on_key(page_t *self, const key_event_t *ev)
{
    if (!ev->is_press)
        return;

    settings_priv_t *d = self->user;
    switch (ev->id) {
    case KEY_BTN_UP:
        d->sel = (d->sel - 1 + SETTINGS_NUM) % SETTINGS_NUM;
        settings_paint(d);
        break;
    case KEY_BTN_DOWN:
        d->sel = (d->sel + 1) % SETTINGS_NUM;
        settings_paint(d);
        break;
    case KEY_BTN_LEFT:
        page_manager_pop();
        break;
    case KEY_BTN_ENTER:
        /* Placeholder: hook up setting item handlers later. */
        break;
    default:
        break;
    }
}

page_t page_settings = {
    .name = "settings",
    .ops  = &(page_ops_t){
        .create  = settings_create,
        .destroy = settings_destroy,
        .on_key  = settings_on_key,
    },
};