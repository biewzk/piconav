#include "page_livemap.h"
#include "../Config/app_config.h"
#include "../Utils/page_manager.h"
#include "../Utils/gps_service.h"
#include "../app.h"
#include "../Utils/log.h"
#include "../../Mapsforge/mf_map.h"
#include "../../Mapsforge/mf_render.h"
#include "../../HAL/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Map file path; override with the MF_MAP environment variable. */
#ifndef MF_MAP_PATH
#define MF_MAP_PATH "../resources/macau.map"
#endif

/* Map chosen through the picker page; takes precedence over MF_MAP_PATH,
 * but MF_MAP (sim/dev override) still wins. */
static char s_map_path[256];

void page_livemap_set_map(const char *path)
{
    if (!path || !path[0])
        return;
    snprintf(s_map_path, sizeof(s_map_path), "%s", path);
}

/* Pan step (px): single tap / long-press repeat */
#define PAN_STEP_PX      32
#define PAN_REPEAT_PX    24
#define PAN_REPEAT_MS    70

/* Canvas buffer: 240x320 RGB565 */
static lv_color_t s_canvas_buf[APP_SCR_HOR * APP_SCR_VER];

typedef struct {
    mf_map_t   map;
    bool       map_ok;
    lv_obj_t  *canvas;
    lv_obj_t  *info;
    lv_obj_t  *hint;
    mf_view_t  view;
    lv_timer_t *pan_timer;   /* Long-press auto pan timer */
    int32_t    pan_dx;       /* Repeat offset per tick */
    int32_t    pan_dy;
    key_id_t   pan_key;      /* Currently held key (KEY_NUM = none) */
    lv_timer_t *zoom_timer;  /* Long-ENTER zoom-out repeat timer */
    msg_sub_t *gps_sub;      /* MSG_GPS subscription while visible */
    uint8_t    follow;       /* 1 = recenter on the GPS fix */
    uint32_t   follow_ms;    /* Last auto-recenter (rate limit) */
} livemap_priv_t;

/* Auto-recenter policy: only redraw once the fix left a small box around
 * the viewport center, and at most once per second (a full map frame on
 * this SoC takes a visible amount of time). */
#define FOLLOW_DEAD_PX 24
#define FOLLOW_MIN_MS  900

/* Translate a Mapsforge return code into a human-readable string. */
static const char *mferr_str(int rc)
{
    switch (rc) {
    case MF_OK:           return "OK";
    case MF_ERR_IO:       return "IO error";
    case MF_ERR_MAGIC:    return "not a mapsforge file";
    case MF_ERR_HEADER:   return "bad header";
    case MF_ERR_RANGE:    return "data range error";
    case MF_ERR_NO_SUBFILE: return "zoom not supported";
    case MF_ERR_LIMIT:    return "resource limit";
    default:              return "unknown";
    }
}

/*
 * Re-query the current viewport and render it onto the canvas,
 * then update the info label with zoom/object-count/render time.
 */
static void livemap_redraw(livemap_priv_t *d)
{
    uint32_t t0 = hal_tick_get();

    int32_t min_lat, min_lon, max_lat, max_lon;
    mf_view_bbox(&d->view, &min_lat, &min_lon, &max_lat, &max_lon);

    mf_frame_t frame;
    int rc = mf_query(&d->map, d->view.zoom, min_lat, min_lon, max_lat, max_lon, &frame);
    if (rc != MF_OK) {
        lv_label_set_text_fmt(d->info, "query error: %s", mferr_str(rc));
        lv_canvas_fill_bg(d->canvas, lv_color_hex(0xf5f3ee), LV_OPA_COVER);
        /* A failed query may still hold objects parsed before the error. */
        mf_frame_free(&frame);
        return;
    }

    mf_render(&d->map, &d->view, &frame, d->canvas);

    uint32_t dt = hal_tick_get() - t0;
    lv_label_set_text_fmt(d->info, "z%d %d obj %ums%s",
                          (int)d->view.zoom, (int)frame.count, (unsigned)dt,
                          d->follow ? " GPS" : "");
    lv_obj_invalidate(d->canvas);

    mf_frame_free(&frame);
}

/*
 * MSG_GPS handler: keep the fix in the middle of the map while following.
 * Frames are only re-rendered once the receiver left a small box around
 * the center (and at most once per FOLLOW_MIN_MS), because a full
 * Mapsforge frame is not cheap on this SoC.
 */
static void livemap_on_gps(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    page_t *self = user;
    livemap_priv_t *d = self ? self->user : NULL;
    const gps_data_t *g = data;

    if (!d || !d->map_ok || !g || !g->fix || !d->follow)
        return;

    lv_point_t p;
    mf_view_project(&d->view, (int32_t)(g->lat * 1e6), (int32_t)(g->lon * 1e6), &p);

    int32_t dx = (int32_t)p.x - APP_SCR_HOR / 2;
    int32_t dy = (int32_t)p.y - APP_SCR_VER / 2;
    if (dx * dx + dy * dy <= FOLLOW_DEAD_PX * FOLLOW_DEAD_PX)
        return;

    uint32_t now = hal_tick_get();
    if (d->follow_ms && now - d->follow_ms < FOLLOW_MIN_MS)
        return;
    d->follow_ms = now;

    mf_view_init(&d->view, d->view.zoom, g->lat, g->lon, APP_SCR_HOR, APP_SCR_VER);
    livemap_redraw(d);
}

/* Long-press repeat: pan by the repeat offset each timer tick. */
static void livemap_pan_timer_cb(lv_timer_t *timer)
{
    livemap_priv_t *d = timer->user_data;
    mf_view_pan(&d->view, d->pan_dx, d->pan_dy);
    livemap_redraw(d);
}

/* Stop the auto-pan timer and clear the held key. */
static void livemap_pan_stop(livemap_priv_t *d)
{
    if (d->pan_timer) {
        lv_timer_del(d->pan_timer);
        d->pan_timer = NULL;
    }
    d->pan_key = KEY_NUM;
}

/*
 * Start/switch a pan direction: pan once immediately, then start
 * the long-press repeat timer for continuous movement.
 */
static void livemap_pan_start(livemap_priv_t *d, key_id_t key, int32_t dx, int32_t dy)
{
    if (d->pan_key != key) {
        livemap_pan_stop(d);
        d->pan_key = key;
        d->pan_dx = dx > 0 ? PAN_REPEAT_PX : (dx < 0 ? -PAN_REPEAT_PX : 0);
        d->pan_dy = dy > 0 ? PAN_REPEAT_PX : (dy < 0 ? -PAN_REPEAT_PX : 0);
        d->pan_timer = lv_timer_create(livemap_pan_timer_cb, PAN_REPEAT_MS, d);
    }
    /* Manual panning takes the map off the fix: stop following until the
     * page is reopened. */
    if (d->follow) {
        d->follow = 0;
        LOG_I("livemap: GPS follow off (manual pan)");
    }
    mf_view_pan(&d->view, dx, dy);
    livemap_redraw(d);
}

/* Zoom in one level if not already at the map's max zoom. */
static void livemap_zoom_in(livemap_priv_t *d)
{
    if (d->view.zoom < d->map.zoom_max) {
        d->view.zoom++;
        livemap_redraw(d);
    }
}

/* Zoom out one level if not already at the map's min zoom. */
static void livemap_zoom_out(livemap_priv_t *d)
{
    if (d->view.zoom > d->map.zoom_min) {
        d->view.zoom--;
        livemap_redraw(d);
    }
}

/* Stop the zoom-out repeat timer. */
static void livemap_zoom_stop(livemap_priv_t *d)
{
    if (d->zoom_timer) {
        lv_timer_del(d->zoom_timer);
        d->zoom_timer = NULL;
    }
}

/* Long-ENTER hold: keep zooming out until the map's min zoom. */
static void livemap_zoom_timer_cb(lv_timer_t *timer)
{
    livemap_priv_t *d = timer->user_data;

    if (d->view.zoom > d->map.zoom_min)
        livemap_zoom_out(d);
    else
        livemap_zoom_stop(d);
}

/* Start the zoom-out repeat (first step happens immediately). */
static void livemap_zoom_repeat_start(livemap_priv_t *d)
{
    if (!d->zoom_timer)
        d->zoom_timer = lv_timer_create(livemap_zoom_timer_cb, 250, d);
}

/*
 * Build the map page: full-screen canvas, info/hint overlays,
 * open the .map file and initialize the initial viewport.
 */
static void livemap_create(page_t *self)
{
    livemap_priv_t *d = calloc(1, sizeof(livemap_priv_t));
    self->user = d;
    d->pan_key = KEY_NUM;
    d->follow = 1;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xf5f3ee), 0);

    /* Canvas fills the whole screen */
    d->canvas = lv_canvas_create(scr);
    lv_obj_set_size(d->canvas, APP_SCR_HOR, APP_SCR_VER);
    lv_canvas_set_buffer(d->canvas, s_canvas_buf, APP_SCR_HOR, APP_SCR_VER, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(d->canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Top-left info label: semi-transparent black bg, white text */
    d->info = lv_label_create(scr);
    lv_obj_set_style_text_font(d->info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(d->info, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(d->info, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d->info, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(d->info, 2, 0);
    lv_obj_set_style_radius(d->info, 3, 0);
    lv_obj_align(d->info, LV_ALIGN_TOP_LEFT, 2, 2);

    /* Bottom-right operation hint */
    d->hint = lv_label_create(scr);
    lv_label_set_text(d->hint, "OK:+ HOLD:-");
    lv_obj_set_style_text_font(d->hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(d->hint, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(d->hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d->hint, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(d->hint, 2, 0);
    lv_obj_set_style_radius(d->hint, 3, 0);
    lv_obj_align(d->hint, LV_ALIGN_BOTTOM_RIGHT, -2, -2);

    const char *path = getenv("MF_MAP");
    if (!path || !path[0])
        path = s_map_path[0] ? s_map_path : MF_MAP_PATH;

    int rc = mf_open(&d->map, path);
    d->map_ok = (rc == MF_OK);
    if (!d->map_ok) {
        lv_label_set_text_fmt(d->info, "map open fail: %s", mferr_str(rc));
        LOG_E("livemap: open %s failed: %s", path, mferr_str(rc));
        return;
    }
    LOG_I("livemap: opened %s (zoom %d..%d)", path, (int)d->map.zoom_min, (int)d->map.zoom_max);

    /* Initial viewport: map start pos (or bbox center), start zoom */
    double lat, lon;
    int32_t zoom;
    if (d->map.start_pos.lat_e6 >= 0) {
        lat = (double)d->map.start_pos.lat_e6 / 1e6;
        lon = (double)d->map.start_pos.lon_e6 / 1e6;
    } else {
        lat = ((double)d->map.bbox_min.lat_e6 + d->map.bbox_max.lat_e6) / 2e6;
        lon = ((double)d->map.bbox_min.lon_e6 + d->map.bbox_max.lon_e6) / 2e6;
    }
    const char *latov = getenv("SIM_LAT");
    if (latov)
        lat = atof(latov);
    const char *lonov = getenv("SIM_LON");
    if (lonov)
        lon = atof(lonov);
    zoom = d->map.start_zoom > 0 ? d->map.start_zoom
         : (d->map.zoom_min + d->map.zoom_max) / 2;
    const char *zov = getenv("SIM_ZOOM");
    if (zov)
        zoom = atoi(zov);
    if (zoom < d->map.zoom_min)
        zoom = d->map.zoom_min;
    if (zoom > d->map.zoom_max)
        zoom = d->map.zoom_max;

    mf_view_init(&d->view, zoom, lat, lon, APP_SCR_HOR, APP_SCR_VER);
    livemap_redraw(d);
}

/*
 * Free the map and the private data; stop any active pan/zoom timer.
 */
static void livemap_destroy(page_t *self)
{
    livemap_priv_t *d = self->user;
    livemap_pan_stop(d);
    livemap_zoom_stop(d);
    if (d->map_ok)
        mf_close(&d->map);
    free(self->user);
    self->user = NULL;
}

/* Stop auto-pan when the page leaves the foreground. */
static void livemap_on_hide(page_t *self)
{
    livemap_priv_t *d = self->user;

    livemap_pan_stop(d);
    gps_service_demand(0);
    if (d->gps_sub) {
        msg_center_unsubscribe(app_msg_center(), MSG_GPS, d->gps_sub);
        d->gps_sub = NULL;
    }
}

/* Follow the GPS fix while the page is visible: 1 s samples (the map
 * only re-renders on pool refreshes), subscribe on entry and recenter
 * immediately when a fix is already known. */
static void livemap_on_show(page_t *self)
{
    livemap_priv_t *d = self->user;
    if (!d)
        return;

    d->follow = 1;
    d->follow_ms = 0;
    gps_service_demand(1);
    if (!d->gps_sub)
        d->gps_sub = msg_center_subscribe(app_msg_center(), MSG_GPS,
                                          livemap_on_gps, self);

    const gps_data_t *g = app_gps();
    if (d->map_ok && g->fix) {
        mf_view_init(&d->view, d->view.zoom, g->lat, g->lon,
                     APP_SCR_HOR, APP_SCR_VER);
        livemap_redraw(d);
    }
}

/*
 * Key handling: arrows pan (with long-press repeat), ENTER click zooms
 * in, ENTER held zooms out continuously (and exits the page when held
 * while already at the min zoom). BACK mirrors the ENTER gestures for
 * the simulator. Key release stops the auto-pan/zoom timers.
 */
static void livemap_on_key(page_t *self, const key_event_t *ev)
{
    livemap_priv_t *d = self->user;
    if (!d->map_ok)
        return;

    if (!ev->is_press) {
        /* Key release: stop the long-press pan / zoom-out */
        if (ev->id == d->pan_key)
            livemap_pan_stop(d);
        if (ev->id == KEY_BTN_ENTER && ev->is_long)
            livemap_zoom_stop(d);
        return;
    }

    switch (ev->id) {
    case KEY_BTN_UP:
        livemap_pan_start(d, ev->id, 0, -PAN_STEP_PX);
        break;
    case KEY_BTN_DOWN:
        livemap_pan_start(d, ev->id, 0, PAN_STEP_PX);
        break;
    case KEY_BTN_LEFT:
        livemap_pan_start(d, ev->id, -PAN_STEP_PX, 0);
        break;
    case KEY_BTN_RIGHT:
        livemap_pan_start(d, ev->id, PAN_STEP_PX, 0);
        break;
    case KEY_BTN_ENTER:
        if (ev->is_long) {
            if (d->view.zoom > d->map.zoom_min) {
                livemap_zoom_out(d);
                livemap_zoom_repeat_start(d);
            } else {
                /* Already at the min zoom: holding OK exits the map. */
                livemap_zoom_stop(d);
                page_manager_pop();
            }
        } else {
            livemap_zoom_in(d);
        }
        break;
    case KEY_BTN_BACK:
        /* Simulator-only key (ESC): raw semantics, no gesture layer. */
        if (d->view.zoom > d->map.zoom_min)
            livemap_zoom_out(d);
        else
            page_manager_pop();
        break;
    default:
        break;
    }
}

page_t page_livemap = {
    .name = "livemap",
    .ops  = &(page_ops_t){
        .create  = livemap_create,
        .destroy = livemap_destroy,
        .on_show = livemap_on_show,
        .on_hide = livemap_on_hide,
        .on_key  = livemap_on_key,
    },
};
