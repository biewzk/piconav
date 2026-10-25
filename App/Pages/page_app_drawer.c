#include "page_app_drawer.h"
#include "app_registry.h"
#include "../Config/app_config.h"

#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_app_16);

/*
 * X-TRACK style launcher: full-width rows of icon tile + app name, a
 * sliding highlight bar and a staggered slide-in entrance animation
 * every time the drawer becomes visible.
 *
 * Text uses lv_font_app_16, a generated bitmap font subset (ASCII +
 * the CJK glyphs the UI actually uses; montserrat renders Chinese as
 * blanks and lv_font_simsun_16_cjk misses most of them).
 */

#define ITEM_H   50
#define TITLE_H  40
#define ROW_H    46
#define ROW_X    8
#define ICON_SZ  40
#define HIGHLIGHT_ANIM_TIME 200
#define ENTRANCE_ITEM_MS    280
#define ENTRANCE_STAGGER_MS 60
#define ENTRANCE_FROM_DX    44

#define C_BG      0x0f0f1a
#define C_SEL     0x00c2ff
#define C_ITEM    0xdddddd
#define C_HL      0x223344
#define C_HINT    0x555555

/* "ying yong" - drawer title, UTF-8 escapes (code stays ASCII) */
#define TXT_TITLE "\xE5\xBA\x94\xE7\x94\xA8"
/* "UP/DOWN:xuan ze   OK:da kai" - key hint, UTF-8 escapes */
#define TXT_HINT  "UP/DOWN:\xE9\x80\x89\xE6\x8B\xA9   OK:\xE6\x89\x93\xE5\xBC\x80"

static lv_obj_t *s_rows[16];     /* row container per app entry */
static lv_obj_t *s_names[16];    /* name label inside the row   */
static lv_obj_t *s_highlight;    /* sliding highlight bar */
static lv_anim_t s_sel_anim;     /* highlight slide animation */
static int       s_count;        /* number of apps */
static int       s_sel;          /* current selection */

static void highlight_anim_cb(void *var, int32_t val)
{
    lv_obj_set_y((lv_obj_t *)var, val);
}

static void row_x_anim_cb(void *var, int32_t val)
{
    lv_obj_set_x((lv_obj_t *)var, val);
}

static void row_opa_anim_cb(void *var, int32_t val)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)val, 0);
}

/* Animate the highlight bar to the selected item's position. */
static void highlight_slide_to(int sel)
{
    if (!s_highlight || sel < 0 || sel >= s_count)
        return;

    int target_y = TITLE_H + 2 + sel * ITEM_H;

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
    for (int i = 0; i < s_count && i < 16; i++)
        lv_obj_set_style_text_color(s_names[i],
            (i == s_sel) ? lv_color_hex(C_SEL) : lv_color_hex(C_ITEM), 0);
    highlight_slide_to(s_sel);
}

/*
 * Staggered slide-in of the app rows (X-TRACK app list entrance):
 * every row starts shifted right and transparent, then eases into
 * place, one after another.
 */
void app_drawer_play_entrance(void)
{
    for (int i = 0; i < s_count && i < 16; i++) {
        lv_obj_t *row = s_rows[i];
        lv_anim_t a;

        lv_anim_del(row, row_x_anim_cb);
        lv_anim_del(row, row_opa_anim_cb);
        lv_obj_set_x(row, ROW_X + ENTRANCE_FROM_DX);
        lv_obj_set_style_opa(row, LV_OPA_TRANSP, 0);

        lv_anim_init(&a);
        lv_anim_set_var(&a, row);
        lv_anim_set_values(&a, ROW_X + ENTRANCE_FROM_DX, ROW_X);
        lv_anim_set_time(&a, ENTRANCE_ITEM_MS);
        lv_anim_set_delay(&a, i * ENTRANCE_STAGGER_MS);
        lv_anim_set_exec_cb(&a, row_x_anim_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);

        lv_anim_init(&a);
        lv_anim_set_var(&a, row);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&a, ENTRANCE_ITEM_MS);
        lv_anim_set_delay(&a, i * ENTRANCE_STAGGER_MS);
        lv_anim_set_exec_cb(&a, row_opa_anim_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

void app_drawer_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, TXT_TITLE);
    lv_obj_set_style_text_font(title, &lv_font_app_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_SEL), 0);
    lv_obj_set_style_text_letter_space(title, 4, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* Highlight bar (created first so it sits behind the rows) */
    s_highlight = lv_obj_create(parent);
    lv_obj_set_size(s_highlight, APP_SCR_HOR - 16, ROW_H);
    lv_obj_set_pos(s_highlight, ROW_X, TITLE_H + 2);
    lv_obj_set_style_bg_color(s_highlight, lv_color_hex(C_HL), 0);
    lv_obj_set_style_bg_opa(s_highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_highlight, 8, 0);
    lv_obj_set_style_border_width(s_highlight, 0, 0);
    lv_obj_clear_flag(s_highlight, LV_OBJ_FLAG_SCROLLABLE);

    /* App rows */
    s_count = app_registry_count();
    for (int i = 0; i < s_count && i < 16; i++) {
        app_entry_t *app = app_registry_get(i);

        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, APP_SCR_HOR - 16, ROW_H);
        lv_obj_set_pos(row, ROW_X, TITLE_H + 2 + i * ITEM_H);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        s_rows[i] = row;

        if (app->img) {
            lv_obj_t *ic = lv_img_create(row);
            lv_img_set_src(ic, app->img);
            lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
        } else {
            /* Letter-glyph fallback for plugins without an icon image */
            lv_obj_t *g = lv_label_create(row);
            lv_label_set_text(g, app->icon ? app->icon : "?");
            lv_obj_set_style_text_font(g, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(g, lv_color_hex(app->icon_color), 0);
            lv_obj_set_width(g, ICON_SZ);
            lv_obj_set_style_text_align(g, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(g, LV_ALIGN_LEFT_MID, 0, 0);
        }

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, app->name);
        lv_obj_set_style_text_font(lbl, &lv_font_app_16, 0);
        lv_obj_set_style_text_letter_space(lbl, 2, 0);
        lv_obj_set_width(lbl, APP_SCR_HOR - 16 - 52 - 8);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 52, 0);
        s_names[i] = lbl;
    }

    /* Hint */
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, TXT_HINT);
    lv_obj_set_style_text_font(hint, &lv_font_app_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(C_HINT), 0);
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
