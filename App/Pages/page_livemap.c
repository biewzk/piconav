#include "page_livemap.h"
#include "../Config/app_config.h"

static void livemap_create(page_t *self)
{
    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x12101a), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LiveMap");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *ph = lv_label_create(scr);
    lv_label_set_text(ph, "地图占位\n(Phase 4)");
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ph, lv_color_hex(0x888888), 0);
    lv_obj_align(ph, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(ph, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "LEFT/RIGHT: 返回");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
}

static void livemap_on_key(page_t *self, const key_event_t *ev)
{
    (void)self;
    if (!ev->is_press)
        return;
    if (ev->id == KEY_BTN_LEFT || ev->id == KEY_BTN_RIGHT || ev->id == KEY_BTN_ENTER)
        page_manager_pop();
}

page_t page_livemap = {
    .name = "livemap",
    .ops  = &(page_ops_t){
        .create = livemap_create,
        .on_key = livemap_on_key,
    },
};