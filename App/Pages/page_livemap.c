#include "page_livemap.h"
#include "../Config/app_config.h"
#include "../Utils/page_manager.h"
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
} livemap_priv_t;

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
    lv_label_set_text_fmt(d->info, "z%d %d obj %ums",
                          (int)d->view.zoom, (int)frame.count, (unsigned)dt);
    lv_obj_invalidate(d->canvas);

    mf_frame_free(&frame);
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

/*
 * Build the map page: full-screen canvas, info/hint overlays,
 * open the .map file and initialize the initial viewport.
 */
static void livemap_create(page_t *self)
{
    livemap_priv_t *d = calloc(1, sizeof(livemap_priv_t));
    self->user = d;
    d->pan_key = KEY_NUM;

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
    lv_label_set_text(d->hint, "OK:+  ESC:-");
    lv_obj_set_style_text_font(d->hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(d->hint, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(d->hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d->hint, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(d->hint, 2, 0);
    lv_obj_set_style_radius(d->hint, 3, 0);
    lv_obj_align(d->hint, LV_ALIGN_BOTTOM_RIGHT, -2, -2);

    const char *path = getenv("MF_MAP");
    if (!path)
        path = MF_MAP_PATH;

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
 * Free the map and the private data; stop any active pan timer.
 */
static void livemap_destroy(page_t *self)
{
    livemap_priv_t *d = self->user;
    livemap_pan_stop(d);
    if (d->map_ok)
        mf_close(&d->map);
    free(self->user);
    self->user = NULL;
}

/* Stop auto-pan when the page leaves the foreground. */
static void livemap_on_hide(page_t *self)
{
    livemap_pan_stop(self->user);
}

/*
 * Key handling: arrows pan (with long-press repeat), ENTER zooms
 * in, BACK zooms out. Key release stops the auto-pan timer.
 */
static void livemap_on_key(page_t *self, const key_event_t *ev)
{
    livemap_priv_t *d = self->user;
    if (!d->map_ok)
        return;

    if (!ev->is_press) {
        /* Key release: stop the long-press pan */
        if (ev->id == d->pan_key)
            livemap_pan_stop(d);
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
        livemap_zoom_in(d);
        break;
    case KEY_BTN_BACK:
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
        .on_hide = livemap_on_hide,
        .on_key  = livemap_on_key,
    },
};
