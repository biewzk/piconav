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
 * Home carousel map panel: full-bleed map_view (with its GPS HUD) plus
 * the shared status bar. The panel refreshes from the GPS pool once per
 * second while active - the same pattern as the GPS receiver panel.
 */

/* Double-tap window for LEFT/RIGHT = carousel panel switch. Dev/sim
 * override: MAPVIEW_DOUBLE_TAP_MS (same knob as the map's 2xOK window;
 * scripted sim keys are widely spaced, the hardware default is 450 ms). */
static uint32_t panel_double_tap_ms = 450;

typedef struct {
    map_view_t *mv;
    lv_timer_t *timer;
    msg_sub_t  *map_sub;      /* "map.selected" runtime topic */
    int         active;
    uint32_t    tap_left_ms;
    uint32_t    tap_right_ms;
    void      (*nav)(int dir);
} livemap_priv_t;

static livemap_priv_t s_lm;   /* single panel instance */

/*
 * Pick the map file to open at boot: the persisted selection, else the
 * first .map (name-sorted) in the data dir, else empty - map_view then
 * falls back to MF_MAP env / the dev path. Only one map is tracked for
 * now; the picker page can still change it via the runtime topic.
 */
static void panel_pick_map(char *buf, size_t cap)
{
    buf[0] = '\0';

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
static void panel_refresh(void)
{
    const gps_data_t *g = app_gps();
    int fresh = g->stamp_ms != 0 &&
                hal_tick_get() - g->stamp_ms <= gps_service_stale_ms();
    map_view_set_gps(s_lm.mv, fresh ? g : NULL);
}

static void panel_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_lm.active)
        panel_refresh();
}

/* Map picker selection: swap the map file live (panel may be inactive). */
static void panel_on_map_selected(msg_topic_t topic, const void *data,
                                  void *user)
{
    (void)topic;
    (void)user;
    const char *path = (const char *)data;

    if (path && path[0])
        map_view_open(s_lm.mv, path);
}

/* Free the widget, timer and subscription when the home screen dies. */
static void panel_on_delete(lv_event_t *e)
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
}

void page_livemap_panel_create(lv_obj_t *parent, void (*nav)(int dir))
{
    memset(&s_lm, 0, sizeof(s_lm));
    s_lm.nav = nav;

    const char *dtov = getenv("MAPVIEW_DOUBLE_TAP_MS");
    if (dtov)
        panel_double_tap_ms = (uint32_t)strtoul(dtov, NULL, 10);

    char path[300];
    panel_pick_map(path, sizeof(path));
    s_lm.mv = map_view_create(parent, path[0] ? path : NULL);

    /* Status bar on top of the full-bleed map (created after it). */
    status_bar_create(parent);

    s_lm.timer = lv_timer_create(panel_timer_cb, 1000, NULL);
    s_lm.map_sub = msg_center_subscribe(app_msg_center(),
                                        app_topic_map_selected(),
                                        panel_on_map_selected, NULL);
    lv_obj_add_event_cb(parent, panel_on_delete, LV_EVENT_DELETE, NULL);
    panel_refresh();
}

void page_livemap_panel_set_active(int on)
{
    if (!s_lm.mv)
        return;
    on = on ? 1 : 0;
    if (on == s_lm.active)
        return;
    s_lm.active = on;

    if (on) {
        /* Fresh "where am I" on every entry: resume follow + recenter. */
        map_view_set_follow(s_lm.mv, 1);
        gps_service_demand_add();
        map_view_recenter(s_lm.mv, app_gps());
        panel_refresh();
    } else {
        map_view_pause(s_lm.mv);
        gps_service_demand_release();
    }
}

int page_livemap_panel_key(const key_event_t *ev)
{
    if (!s_lm.mv || !map_view_is_ok(s_lm.mv))
        return 0;

    /* Double-tap LEFT/RIGHT = carousel panel switch. The stray single-
     * tap pan self-heals: entering the panel again recenters + follows. */
    if ((ev->id == KEY_BTN_LEFT || ev->id == KEY_BTN_RIGHT) &&
        ev->is_press && !ev->is_long) {
        uint32_t now = hal_tick_get();
        uint32_t *last = (ev->id == KEY_BTN_LEFT) ? &s_lm.tap_left_ms
                                                  : &s_lm.tap_right_ms;
        if (*last && now - *last < panel_double_tap_ms) {
            *last = 0;
            map_view_break_gesture(s_lm.mv);
            if (s_lm.nav)
                s_lm.nav(ev->id == KEY_BTN_LEFT ? -1 : 1);
            return 1;
        }
        *last = now;
    }

    if (map_view_on_key(s_lm.mv, ev) && s_lm.nav)
        s_lm.nav(0);   /* hold OK at min zoom: leave to the watch face */
    return 1;
}
