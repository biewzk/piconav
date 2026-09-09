#include "page_startup.h"
#include "page_home.h"
#include "../Config/app_config.h"

#include <stdio.h>

/*
 * Build the startup screen: logo, subtitle and version label.
 */
static void startup_create(page_t *self)
{
    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    /* Logo */
    lv_obj_t *logo = lv_label_create(scr);
    lv_label_set_text(logo, "Pico\nNav");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(logo, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_align(logo, LV_TEXT_ALIGN_CENTER, 0);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "GPS Navigation Computer");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xffffff), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 40);

    /* Version */
    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, "v0.2  Framework");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x888888), 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -20);
}

/*
 * Handle key events: any key skips the startup page.
 */
static void startup_on_key(page_t *self, const key_event_t *ev)
{
    (void)self;
    (void)ev;
    /* Any key skips startup and enters the home page. */
    page_manager_replace(&page_home);
}

page_t page_startup = {
    .name = "startup",
    .ops  = &(page_ops_t){
        .create  = startup_create,
        .on_key  = startup_on_key,
    },
};