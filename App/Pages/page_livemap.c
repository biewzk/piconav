#include "page_livemap.h"
#include "../Utils/map_view.h"
#include "../Utils/status_bar.h"
#include "../Utils/gps_service.h"
#include "../Utils/config_api.h"
#include "../Utils/msg_center.h"
#include "../Config/app_config.h"
#include "../app.h"
#include "../../HAL/hal.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/*
 * Live map page: full-bleed map_view (with its GPS HUD) plus the shared
 * status bar. A 1 s timer pushes the GPS pool into the widget while the
 * page is visible - the same pattern as the GPS receiver page. Exiting
 * is the hold-OK-at-min-zoom gesture of the map itself (long ENTER is
 * the map's zoom-out, hence PAGE_FLAG_NO_AUTO_BACK).
 */

typedef struct {
    map_view_t *mv;
    lv_timer_t *timer;
    msg_sub_t  *map_sub;      /* "map.selected" runtime topic */
    lv_obj_t   *nomap_lbl;    /* shown when no map could be opened */
    int         visible;
} livemap_priv_t;

static livemap_priv_t s_lm;   /* single page instance */

/*
 * Pick the map file to open: an explicit parameter (picker page), else
 * the persisted selection, else the first .map (name-sorted) in the
 * data dir, else empty - map_view then falls back to MF_MAP env / the
 * dev path.
 */
static void pick_map(const char *param, char *buf, size_t cap)
{
    buf[0] = '\0';

    if (param && param[0]) {
        snprintf(buf, cap, "%s", param);
        return;
    }

    config_init();
    config_get_str(APP_CFG_KEY_MAP_SELECTED, buf, cap, "");
    if (buf[0] && access(buf, R_OK) == 0)
        return;
    buf[0] = '\0';

    DIR *dp = opendir(APP_MAP_DIR_DEFAULT);
    if (!dp)
        return;

    char best[64] = "";
    struct dirent *e;
    while ((e = readdir(dp)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".map") != 0)
            continue;
        if (strlen(e->d_name) < sizeof(best) &&
            (!best[0] || strcasecmp(e->d_name, best) < 0))
            snprintf(best, sizeof(best), "%s", e->d_name);
    }
    closedir(dp);

    if (best[0])
        snprintf(buf, cap, "%s/%s", APP_MAP_DIR_DEFAULT, best);
}

/* Push the current pool state into the widget; NULL = no fresh fix. */
static void refresh(void)
{
    const gps_data_t *g = app_gps();
    int fresh = g->stamp_ms != 0 &&
                hal_tick_get() - g->stamp_ms <= gps_service_stale_ms();
    map_view_set_gps(s_lm.mv, fresh ? g : NULL);
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_lm.visible && s_lm.mv && map_view_is_ok(s_lm.mv))
        refresh();
}

/* Map picker selection: swap the map file live (page may be hidden). */
static void on_map_selected(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    (void)user;
    const char *path = (const char *)data;

    if (s_lm.mv && path && path[0]) {
        map_view_open(s_lm.mv, path);
        if (s_lm.nomap_lbl)
            lv_obj_add_flag(s_lm.nomap_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Free the widget, timer and subscription when the page screen dies. */
static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_lm.timer) {
        lv_timer_del(s_lm.timer);
        s_lm.timer = NULL;
    }
    if (s_lm.map_sub) {
        msg_center_unsubscribe(app_msg_center(), app_topic_map_selected(),
                               s_lm.map_sub);
        s_lm.map_sub = NULL;
    }
    if (s_lm.mv) {
        map_view_delete(s_lm.mv);
        s_lm.mv = NULL;
    }
    s_lm.nomap_lbl = NULL;
    s_lm.visible = 0;
}

static void create(page_t *self)
{
    livemap_priv_t *d = &s_lm;
    memset(d, 0, sizeof(*d));
    self->user = d;

    char path[300];
    pick_map(self->param, path, sizeof(path));
    d->mv = map_view_create(self->scr, path[0] ? path : NULL);

    /* Status bar on top of the full-bleed map (created after it). */
    status_bar_create(self->scr);

    if (!map_view_is_ok(d->mv)) {
        d->nomap_lbl = lv_label_create(self->scr);
        lv_label_set_text(d->nomap_lbl, "NO MAP");
        lv_obj_set_style_text_font(d->nomap_lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(d->nomap_lbl,
                                    lv_color_hex(0xff6666), 0);
        lv_obj_align(d->nomap_lbl, LV_ALIGN_CENTER, 0, -10);
    }

    d->timer = lv_timer_create(timer_cb, 1000, NULL);
    d->map_sub = msg_center_subscribe(app_msg_center(),
                                      app_topic_map_selected(),
                                      on_map_selected, NULL);
    lv_obj_add_event_cb(self->scr, on_delete, LV_EVENT_DELETE, NULL);
}

static void on_show(page_t *self)
{
    livemap_priv_t *d = self->user;
    d->visible = 1;
    if (d->mv && map_view_is_ok(d->mv)) {
        /* Fresh "where am I" on every entry: follow + recenter. */
        map_view_set_follow(d->mv, 1);
        gps_service_demand_add();
        map_view_recenter(d->mv, app_gps());
        refresh();
    }
}

static void on_hide(page_t *self)
{
    livemap_priv_t *d = self->user;
    d->visible = 0;
    if (d->mv && map_view_is_ok(d->mv)) {
        map_view_pause(d->mv);
        gps_service_demand_release();
    }
}

/*
 * All keys go to the map while it is usable (see the header). The map
 * reports "close me" (hold OK at min zoom) with a true return value.
 * Without a map, LEFT/BACK/ENTER pop back to the drawer.
 */
static void on_key(page_t *self, const key_event_t *ev)
{
    livemap_priv_t *d = self->user;

    if (!d->mv || !map_view_is_ok(d->mv)) {
        if (ev->is_press &&
            (ev->id == KEY_BTN_LEFT || ev->id == KEY_BTN_BACK ||
             ev->id == KEY_BTN_ENTER))
            page_manager_pop();
        return;
    }

    if (map_view_on_key(d->mv, ev))
        page_manager_pop();
}

page_t page_livemap = {
    .name  = "livemap",
    .flags = PAGE_FLAG_NO_AUTO_BACK,   /* long ENTER = map zoom out */
    .ops   = &(page_ops_t){
        .create  = create,
        .destroy = NULL,
        .on_show = on_show,
        .on_hide = on_hide,
        .on_key  = on_key,
    },
};
