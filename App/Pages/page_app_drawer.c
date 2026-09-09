#include "page_app_drawer.h"
#include "app_registry.h"
#include "../Config/app_config.h"

#include <stdlib.h>
#include <string.h>

#define ITEM_H   50
#define TITLE_H  40
#define HINT_H   30
#define HIGHLIGHT_ANIM_TIME 200

static lv_obj_t *s_items[16];    /* label objects for each app entry */
static lv_obj_t *s_highlight;    /* sliding highlight bar */
static int       s_count;        /* number of apps */
static int       s_sel;          /* current selection */
static lv_anim_t s_sel_anim;     /* highlight slide animation */

static void highlight_anim_cb(void *var, int32_t val)
{
    lv_obj_set_y((lv_obj_t *)var, val);
}

/* Animate the highlight bar to the selected item's position. */
static void highlight_slide_to(int sel)
{
    if (!s_highlight || sel < 0 || sel >= s_count)
        return;

    int target_y = TITLE_H + sel * ITEM_H;

    lv_anim_del(s_highlight, highlight_anim_cb);

    lv_anim_init(&s_sel_anim);
    lv_anim_set_var(&s_sel_anim, s_highlight);
    lv_anim_set_values(&s_sel_anim, lv_obj_get_y(s_highlight), target_y);
    lv_anim_set_time(&s_sel_anim, HIGHLIGHT_ANIM_TIME);
    lv_anim_set_exec_cb(&s_sel_anim, highlight_anim_cb);
    lv_anim_set_path_cb(&s_sel_anim, lv_anim_path_ease_out);
    lv_anim_start(&s_sel_anim);
}

static void drawer_paint(void)
{
    /* Instantly update text colors (no animation needed for text) */
    for (int i = 0; i < s_count; i++) {
        int selected = (i == s_sel);
        lv_obj_set_style_text_color(s_items[i],
            selected ? lv_color_hex(0x00c2ff) : lv_color_hex(0xdddddd), 0);
    }
    /* Animate the highlight bar to the new position */
    highlight_slide_to(s_sel);
}

void app_drawer_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0f0f1a), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "应用");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Highlight bar (created first so it's behind text labels) */
    s_highlight = lv_obj_create(parent);
    lv_obj_set_size(s_highlight, APP_SCR_HOR - 16, ITEM_H - 4);
    lv_obj_set_pos(s_highlight, 8, TITLE_H + 2);
    lv_obj_set_style_bg_color(s_highlight, lv_color_hex(0x223344), 0);
    lv_obj_set_style_bg_opa(s_highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_highlight, 6, 0);
    lv_obj_set_style_border_width(s_highlight, 0, 0);
    lv_obj_clear_flag(s_highlight, LV_OBJ_FLAG_SCROLLABLE);

    /* App entries */
    s_count = app_registry_count();
    for (int i = 0; i < s_count && i < 16; i++) {
        app_entry_t *app = app_registry_get(i);

        lv_obj_t *item = lv_label_create(parent);
        lv_obj_set_style_text_font(item, &lv_font_montserrat_24, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_width(item, APP_SCR_HOR - 24);
        lv_obj_align(item, LV_ALIGN_TOP_LEFT, 12, TITLE_H + i * ITEM_H);

        /* Format: "Icon  Name" */
        lv_label_set_text_fmt(item, "%s   %s", app->icon, app->name);
        lv_obj_set_style_text_color(item, lv_color_hex(app->icon_color), 0);

        s_items[i] = item;
    }

    /* Hint */
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "UP/DOWN:选择 OK:打开");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_sel = 0;
    drawer_paint();
}

void app_drawer_set_sel(int sel)
{
    if (s_count == 0)
        return;
    if (sel < 0)
        sel = s_count - 1;
    if (sel >= s_count)
        sel = 0;
    s_sel = sel;
    drawer_paint();
}

int app_drawer_get_sel(void)
{
    return s_sel;
}
