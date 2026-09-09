#include "page_home.h"
#include "page_app_drawer.h"
#include "page_livemap.h"
#include "page_dialplate.h"
#include "page_settings.h"
#include "app_registry.h"
#include "../Config/app_config.h"
#include "../Utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAROUSEL_PANELS 3

#define PANEL_WATCH_FACE  0
#define PANEL_APP_DRAWER  1
#define PANEL_PLACEHOLDER 2

typedef struct {
    lv_obj_t   *panels[CAROUSEL_PANELS];
    lv_obj_t   *container;
    int         cur;
    bool        animating;
    lv_obj_t   *time_label;
    lv_obj_t   *date_label;
    lv_obj_t   *title_label;
    lv_obj_t   *hint_label;
    lv_timer_t *clock_timer;
    lv_anim_timeline_t *entrance_anim;
} home_priv_t;

/* ---------- clock timer ---------- */

static void clock_timer_cb(lv_timer_t *timer)
{
    home_priv_t *d = timer->user_data;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    lv_label_set_text_fmt(d->time_label, "%02d:%02d:%02d",
                          t->tm_hour, t->tm_min, t->tm_sec);

    lv_label_set_text_fmt(d->date_label, "%04d-%02d-%02d",
                          t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

static void clock_start(home_priv_t *d)
{
    if (!d->clock_timer) {
        d->clock_timer = lv_timer_create(clock_timer_cb, 1000, d);
        clock_timer_cb(d->clock_timer);
    }
}

static void clock_stop(home_priv_t *d)
{
    if (d->clock_timer) {
        lv_timer_del(d->clock_timer);
        d->clock_timer = NULL;
    }
}

/* ---------- carousel animation ---------- */

static void anim_set_x_cb(void *var, int32_t val)
{
    lv_obj_set_x((lv_obj_t *)var, val);
}

static void switch_done_cb(lv_anim_t *a)
{
    home_priv_t *d = a->user_data;
    d->animating = false;
}

/* Cancel any in-progress carousel animations on all panels. */
static void carousel_cancel_anims(home_priv_t *d)
{
    for (int i = 0; i < CAROUSEL_PANELS; i++)
        lv_anim_del(d->panels[i], anim_set_x_cb);
    d->animating = false;
}

/*
 * Switch carousel panel with X-TRACK style horizontal slide animation.
 * Both panels animate simultaneously: outgoing slides out, incoming slides in.
 * Easing: ease_out (fast start, slow end).
 */
static void home_switch_panel(home_priv_t *d, int new_idx, int dir)
{
    if (d->animating || new_idx == d->cur)
        return;
    if (new_idx < 0 || new_idx >= CAROUSEL_PANELS)
        return;

    /* Cancel any lingering animations from a previous incomplete transition */
    carousel_cancel_anims(d);
    d->animating = true;

    int old_idx = d->cur;
    lv_obj_t *from = d->panels[old_idx];
    lv_obj_t *to   = d->panels[new_idx];

    /* Place the incoming panel off-screen (dir determines slide direction) */
    lv_obj_set_x(to, dir * APP_SCR_HOR);

    /* Animate outgoing panel: 0 -> off-screen */
    lv_anim_t a_out;
    lv_anim_init(&a_out);
    lv_anim_set_var(&a_out, from);
    lv_anim_set_values(&a_out, 0, -dir * APP_SCR_HOR);
    lv_anim_set_time(&a_out, PAGE_ANIM_TIME);
    lv_anim_set_exec_cb(&a_out, anim_set_x_cb);
    lv_anim_set_path_cb(&a_out, lv_anim_path_ease_out);
    lv_anim_start(&a_out);

    /* Animate incoming panel: off-screen -> 0 */
    lv_anim_t a_in;
    lv_anim_init(&a_in);
    lv_anim_set_var(&a_in, to);
    lv_anim_set_values(&a_in, dir * APP_SCR_HOR, 0);
    lv_anim_set_time(&a_in, PAGE_ANIM_TIME);
    lv_anim_set_exec_cb(&a_in, anim_set_x_cb);
    lv_anim_set_path_cb(&a_in, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a_in, switch_done_cb);
    lv_anim_set_user_data(&a_in, d);
    lv_anim_start(&a_in);

    d->cur = new_idx;
    LOG_I("carousel: panel %d -> %d", old_idx, new_idx);
}

/* ---------- panel builders ---------- */

static void build_watch_face(home_priv_t *d, lv_obj_t *parent)
{
    /* Gradient wallpaper background */
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x1a1a3e), 0);
    lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Date — will animate from top */
    d->date_label = lv_label_create(parent);
    lv_label_set_text(d->date_label, "---- -- --");
    lv_obj_set_style_text_font(d->date_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d->date_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(d->date_label, LV_ALIGN_TOP_MID, 0, 30);

    /* Time — centered, main element */
    d->time_label = lv_label_create(parent);
    lv_label_set_text(d->time_label, "00:00:00");
    lv_obj_set_style_text_font(d->time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(d->time_label, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(d->time_label, LV_ALIGN_CENTER, 0, -20);

    /* Title */
    d->title_label = lv_label_create(parent);
    lv_label_set_text(d->title_label, "Pico Nav");
    lv_obj_set_style_text_font(d->title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d->title_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(d->title_label, LV_ALIGN_CENTER, 0, 20);

    /* Navigation hint */
    d->hint_label = lv_label_create(parent);
    lv_label_set_text(d->hint_label, "<  LEFT   RIGHT  >");
    lv_obj_set_style_text_font(d->hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(d->hint_label, lv_color_hex(0x555555), 0);
    lv_obj_align(d->hint_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    /* === Entrance animation (X-TRACK DialplateView style) === */
    d->entrance_anim = lv_anim_timeline_create();

    /* Date: slide down from off-screen */
    lv_anim_t a_date;
    lv_anim_init(&a_date);
    lv_anim_set_var(&a_date, d->date_label);
    lv_anim_set_exec_cb(&a_date, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a_date, -40, lv_obj_get_y(d->date_label));
    lv_anim_set_time(&a_date, 400);
    lv_anim_set_path_cb(&a_date, lv_anim_path_ease_out);
    lv_anim_timeline_add(d->entrance_anim, 0, &a_date);

    /* Time: fade in + scale (opacity transition) */
    lv_obj_set_style_text_opa(d->time_label, LV_OPA_TRANSP, 0);
    lv_anim_t a_time;
    lv_anim_init(&a_time);
    lv_anim_set_var(&a_time, d->time_label);
    lv_anim_set_exec_cb(&a_time, (lv_anim_exec_xcb_t)lv_obj_set_style_text_opa);
    lv_anim_set_values(&a_time, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a_time, 500);
    lv_anim_set_path_cb(&a_time, lv_anim_path_ease_out);
    lv_anim_timeline_add(d->entrance_anim, 100, &a_time);

    /* Title: slide up from below */
    lv_anim_t a_title;
    lv_anim_init(&a_title);
    lv_anim_set_var(&a_title, d->title_label);
    lv_anim_set_exec_cb(&a_title, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a_title, lv_obj_get_y(d->title_label) + 40,
                       lv_obj_get_y(d->title_label));
    lv_anim_set_time(&a_title, 400);
    lv_anim_set_path_cb(&a_title, lv_anim_path_ease_out);
    lv_anim_timeline_add(d->entrance_anim, 200, &a_title);

    /* Hint: fade in */
    lv_obj_set_style_text_opa(d->hint_label, LV_OPA_TRANSP, 0);
    lv_anim_t a_hint;
    lv_anim_init(&a_hint);
    lv_anim_set_var(&a_hint, d->hint_label);
    lv_anim_set_exec_cb(&a_hint, (lv_anim_exec_xcb_t)lv_obj_set_style_text_opa);
    lv_anim_set_values(&a_hint, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a_hint, 400);
    lv_anim_set_path_cb(&a_hint, lv_anim_path_ease_out);
    lv_anim_timeline_add(d->entrance_anim, 300, &a_hint);
}

static void build_placeholder(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0f0f1a), 0);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Coming Soon");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x555555), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "<  LEFT   RIGHT  >");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* ---------- page lifecycle ---------- */

static void home_create(page_t *self)
{
    home_priv_t *d = calloc(1, sizeof(home_priv_t));
    self->user = d;
    d->cur = 0;
    d->animating = false;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a1a), 0);

    /* Carousel container */
    d->container = lv_obj_create(scr);
    lv_obj_set_size(d->container, APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_pos(d->container, 0, 0);
    lv_obj_clear_flag(d->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(d->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d->container, 0, 0);
    lv_obj_set_style_pad_all(d->container, 0, 0);

    /* Panel 0: Watch face */
    d->panels[PANEL_WATCH_FACE] = lv_obj_create(d->container);
    lv_obj_set_size(d->panels[PANEL_WATCH_FACE], APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_pos(d->panels[PANEL_WATCH_FACE], 0, 0);
    lv_obj_clear_flag(d->panels[PANEL_WATCH_FACE], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(d->panels[PANEL_WATCH_FACE], 0, 0);
    lv_obj_set_style_pad_all(d->panels[PANEL_WATCH_FACE], 0, 0);
    build_watch_face(d, d->panels[PANEL_WATCH_FACE]);

    /* Panel 1: App drawer */
    d->panels[PANEL_APP_DRAWER] = lv_obj_create(d->container);
    lv_obj_set_size(d->panels[PANEL_APP_DRAWER], APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_pos(d->panels[PANEL_APP_DRAWER], APP_SCR_HOR, 0);
    lv_obj_clear_flag(d->panels[PANEL_APP_DRAWER], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(d->panels[PANEL_APP_DRAWER], 0, 0);
    lv_obj_set_style_pad_all(d->panels[PANEL_APP_DRAWER], 0, 0);
    app_drawer_create(d->panels[PANEL_APP_DRAWER]);

    /* Panel 2: Placeholder */
    d->panels[PANEL_PLACEHOLDER] = lv_obj_create(d->container);
    lv_obj_set_size(d->panels[PANEL_PLACEHOLDER], APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_pos(d->panels[PANEL_PLACEHOLDER], 2 * APP_SCR_HOR, 0);
    lv_obj_clear_flag(d->panels[PANEL_PLACEHOLDER], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(d->panels[PANEL_PLACEHOLDER], 0, 0);
    lv_obj_set_style_pad_all(d->panels[PANEL_PLACEHOLDER], 0, 0);
    build_placeholder(d->panels[PANEL_PLACEHOLDER]);
}

static void home_destroy(page_t *self)
{
    home_priv_t *d = self->user;
    carousel_cancel_anims(d);
    if (d->entrance_anim)
        lv_anim_timeline_del(d->entrance_anim);
    clock_stop(d);
    free(d);
    self->user = NULL;
}

static void home_on_show(page_t *self)
{
    home_priv_t *d = self->user;
    /* Ensure current panel is at x=0 and others are off-screen */
    for (int i = 0; i < CAROUSEL_PANELS; i++) {
        int x = (i == d->cur) ? 0 : ((i > d->cur) ? APP_SCR_HOR : -APP_SCR_HOR);
        lv_obj_set_x(d->panels[i], x);
    }
    clock_start(d);
    /* Play the entrance animation */
    if (d->entrance_anim)
        lv_anim_timeline_start(d->entrance_anim);
}

static void home_on_hide(page_t *self)
{
    home_priv_t *d = self->user;
    clock_stop(d);
}

/* ---------- key handling ---------- */

static void home_on_key(page_t *self, const key_event_t *ev)
{
    if (!ev->is_press)
        return;

    home_priv_t *d = self->user;

    switch (ev->id) {
    case KEY_BTN_LEFT:
        home_switch_panel(d, d->cur - 1, -1);
        break;
    case KEY_BTN_RIGHT:
        home_switch_panel(d, d->cur + 1, +1);
        break;
    case KEY_BTN_UP:
        if (d->cur == PANEL_APP_DRAWER)
            app_drawer_set_sel(app_drawer_get_sel() - 1);
        break;
    case KEY_BTN_DOWN:
        if (d->cur == PANEL_APP_DRAWER)
            app_drawer_set_sel(app_drawer_get_sel() + 1);
        break;
    case KEY_BTN_ENTER:
        if (d->cur == PANEL_APP_DRAWER) {
            app_entry_t *app = app_registry_get(app_drawer_get_sel());
            if (app && app->page)
                page_manager_push(app->page);
        }
        break;
    default:
        break;
    }
}

page_t page_home = {
    .name = "home",
    .ops  = &(page_ops_t){
        .create  = home_create,
        .destroy = home_destroy,
        .on_show = home_on_show,
        .on_hide = home_on_hide,
        .on_key  = home_on_key,
    },
};
