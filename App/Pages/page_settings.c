#include "page_settings.h"
#include "../Config/app_config.h"
#include "../Utils/config_api.h"
#include "../app.h"
#include "../../HAL/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Editable items (0,1,2) + a static item (3: About) */
#define SETTINGS_NUM 4

static const char *s_items[SETTINGS_NUM] = {
    "Brightness", "Units", "Route Pref", "About",
};

/* Brightness: 0..100 */
/* Units: metric / imperial */
/* Route preference: shortest / fastest */

typedef struct {
    lv_obj_t *labels[SETTINGS_NUM];
    lv_obj_t *values[SETTINGS_NUM];
    int       sel;
    int       inited;   /* whether config was loaded in this lifecycle */
} settings_priv_t;

/* Display text for each editable item's current value */
static const char *setting_unit_str(int v)
{
    return v ? "Imperial" : "Metric";
}

static const char *setting_route_str(int v)
{
    return v ? "Fastest" : "Shortest";
}

/* Refresh the current-value display to the right of an item */
static void settings_refresh_value(settings_priv_t *d, int idx)
{
    const char *text = "";
    int b;

    switch (idx) {
    case 0: /* Brightness */
        config_get_int("display.brightness", &b, 80);
        {
            static char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d%%", b);
            text = tmp;
        }
        break;
    case 1: /* Units */
        config_get_bool("units.metric", &b, 1);
        text = setting_unit_str(!b);
        break;
    case 2: /* Route preference */
        config_get_bool("nav.pref_fastest", &b, 0);
        text = setting_route_str(b);
        break;
    case 3: /* About */
        text = "v1.0.0";
        break;
    default:
        break;
    }
    if (d->values[idx] && text)
        lv_label_set_text(d->values[idx], text);
}

/* Edit an item's value (increment/cycle) and persist it */
static void settings_apply(settings_priv_t *d, int idx)
{
    int b, n;
    int changed = 1;

    switch (idx) {
    case 0: /* Brightness: step by 10 each cycle, wrap at 100 */
        config_get_int("display.brightness", &n, 80);
        n = (n + 10 > 100) ? 0 : n + 10;
        config_set_int("display.brightness", n);
        break;
    case 1: /* Units toggle */
        config_get_bool("units.metric", &b, 1);
        config_set_bool("units.metric", !b);
        break;
    case 2: /* Route preference toggle */
        config_get_bool("nav.pref_fastest", &b, 0);
        config_set_bool("nav.pref_fastest", !b);
        break;
    case 3: /* About: not editable */
        changed = 0;
        break;
    default:
        changed = 0;
        break;
    }

    settings_refresh_value(d, idx);

    if (changed && config_save() == 0) {
        msg_center_publish(app_msg_center(), MSG_CONFIG_CHANGED, NULL);
    }
}

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
    lv_label_set_text(title, "Settings");
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

        /* current value on the right */
        lv_obj_t *val = lv_label_create(scr);
        lv_label_set_text(val, "");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, lv_color_hex(0x88aacc), 0);
        lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -16, 70 + i * 42 + 8);
        d->values[i] = val;
    }

    settings_paint(d);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "UP/DOWN:Select  OK:Confirm  LEFT:Back");
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

/* Load config and refresh the values when the page is shown */
static void settings_on_show(page_t *self)
{
    settings_priv_t *d = self->user;

    if (!d)
        return;
    config_init();
    d->inited = 1;
    for (int i = 0; i < SETTINGS_NUM; i++)
        settings_refresh_value(d, i);
}

/* Release config resources when the page is hidden */
static void settings_on_hide(page_t *self)
{
    settings_priv_t *d = self->user;

    if (d && d->inited) {
        config_deinit();
        d->inited = 0;
    }
}

/*
 * Key handling: UP/DOWN moves the selection, LEFT pops the page,
 * ENTER edits/applies the selected setting item and persists it.
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
    case KEY_BTN_BACK:
        page_manager_pop();
        break;
    case KEY_BTN_ENTER:
        settings_apply(d, d->sel);
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
        .on_show = settings_on_show,
        .on_hide = settings_on_hide,
        .on_key  = settings_on_key,
    },
};
