#include "map_view.h"
#include "../Config/app_config.h"
#include "../../Mapsforge/mf_map.h"
#include "../../Mapsforge/mf_render.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dev/sim fallback map; only used when neither a path was handed over
 * nor MF_MAP is set (cwd-relative by design for sim/ and build/bin). */
#ifndef MF_MAP_PATH
#define MF_MAP_PATH "../resources/macau.map"
#endif

/* Pan step (px): single tap / long-press repeat */
#define PAN_STEP_PX      32
#define PAN_REPEAT_PX    24
#define PAN_REPEAT_MS    70

/* Auto-recenter policy: only redraw once the fix left a small box around
 * the viewport center, and at most once per second. */
#define FOLLOW_DEAD_PX 24
#define FOLLOW_MIN_MS  900

struct map_view {
    mf_map_t    map;
    int         map_ok;
    lv_obj_t   *canvas;
    lv_obj_t   *info;
    lv_color_t *buf;         /* canvas buffer, malloc'd (150 KB full frame) */
    mf_view_t   view;
    lv_timer_t *pan_timer;   /* Long-press auto pan timer */
    int32_t     pan_dx;      /* Repeat offset per tick */
    int32_t     pan_dy;
    key_id_t    pan_key;     /* Currently held key (KEY_NUM = none) */
    lv_timer_t *zoom_timer;  /* Long-ENTER zoom-out repeat timer */
    int         follow;      /* 1 = recenter on the GPS fix */
    uint32_t    follow_ms;   /* Last auto-recenter (rate limit) */
};

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
static void map_view_redraw(map_view_t *mv)
{
    uint32_t t0 = hal_tick_get();

    int32_t min_lat, min_lon, max_lat, max_lon;
    mf_view_bbox(&mv->view, &min_lat, &min_lon, &max_lat, &max_lon);

    mf_frame_t frame;
    int rc = mf_query(&mv->map, mv->view.zoom, min_lat, min_lon,
                      max_lat, max_lon, &frame);
    if (rc != MF_OK) {
        lv_label_set_text_fmt(mv->info, "query error: %s", mferr_str(rc));
        lv_canvas_fill_bg(mv->canvas, lv_color_hex(0xf5f3ee), LV_OPA_COVER);
        /* A failed query may still hold objects parsed before the error. */
        mf_frame_free(&frame);
        return;
    }

    mf_render(&mv->map, &mv->view, &frame, mv->canvas);

    uint32_t dt = hal_tick_get() - t0;
    lv_label_set_text_fmt(mv->info, "z%d %d obj %ums%s",
                          (int)mv->view.zoom, (int)frame.count, (unsigned)dt,
                          mv->follow ? " GPS" : "");
    lv_obj_invalidate(mv->canvas);

    mf_frame_free(&frame);
}

void map_view_set_gps(map_view_t *mv, const gps_data_t *g)
{
    if (!mv || !mv->map_ok || !g || !g->fix || !mv->follow)
        return;

    lv_point_t p;
    mf_view_project(&mv->view, (int32_t)(g->lat * 1e6),
                    (int32_t)(g->lon * 1e6), &p);

    int32_t dx = (int32_t)p.x - APP_SCR_HOR / 2;
    int32_t dy = (int32_t)p.y - APP_SCR_VER / 2;
    if (dx * dx + dy * dy <= FOLLOW_DEAD_PX * FOLLOW_DEAD_PX)
        return;

    uint32_t now = hal_tick_get();
    if (mv->follow_ms && now - mv->follow_ms < FOLLOW_MIN_MS)
        return;
    mv->follow_ms = now;

    mf_view_init(&mv->view, mv->view.zoom, g->lat, g->lon,
                 APP_SCR_HOR, APP_SCR_VER);
    map_view_redraw(mv);
}

void map_view_recenter(map_view_t *mv, const gps_data_t *g)
{
    if (!mv || !mv->map_ok || !g || !g->fix)
        return;
    mf_view_init(&mv->view, mv->view.zoom, g->lat, g->lon,
                 APP_SCR_HOR, APP_SCR_VER);
    map_view_redraw(mv);
}

void map_view_set_follow(map_view_t *mv, int follow)
{
    if (mv)
        mv->follow = follow ? 1 : 0;
}

/* Long-press repeat: pan by the repeat offset each timer tick. */
static void map_view_pan_timer_cb(lv_timer_t *timer)
{
    map_view_t *mv = timer->user_data;
    mf_view_pan(&mv->view, mv->pan_dx, mv->pan_dy);
    map_view_redraw(mv);
}

/* Stop the auto-pan timer and clear the held key. */
static void map_view_pan_stop(map_view_t *mv)
{
    if (mv->pan_timer) {
        lv_timer_del(mv->pan_timer);
        mv->pan_timer = NULL;
    }
    mv->pan_key = KEY_NUM;
}

/*
 * Start/switch a pan direction: pan once immediately, then start
 * the long-press repeat timer for continuous movement.
 */
static void map_view_pan_start(map_view_t *mv, key_id_t key, int32_t dx, int32_t dy)
{
    if (mv->pan_key != key) {
        map_view_pan_stop(mv);
        mv->pan_key = key;
        mv->pan_dx = dx > 0 ? PAN_REPEAT_PX : (dx < 0 ? -PAN_REPEAT_PX : 0);
        mv->pan_dy = dy > 0 ? PAN_REPEAT_PX : (dy < 0 ? -PAN_REPEAT_PX : 0);
        mv->pan_timer = lv_timer_create(map_view_pan_timer_cb, PAN_REPEAT_MS, mv);
    }
    /* Manual panning takes the map off the fix: stop following until the
     * owner re-enables it. */
    if (mv->follow) {
        mv->follow = 0;
        LOG_I("map view: GPS follow off (manual pan)");
    }
    mf_view_pan(&mv->view, dx, dy);
    map_view_redraw(mv);
}

/* Zoom in one level if not already at the map's max zoom. */
static void map_view_zoom_in(map_view_t *mv)
{
    if (mv->view.zoom < mv->map.zoom_max) {
        mv->view.zoom++;
        map_view_redraw(mv);
    }
}

/* Zoom out one level if not already at the map's min zoom. */
static void map_view_zoom_out(map_view_t *mv)
{
    if (mv->view.zoom > mv->map.zoom_min) {
        mv->view.zoom--;
        map_view_redraw(mv);
    }
}

/* Stop the zoom-out repeat timer. */
static void map_view_zoom_stop(map_view_t *mv)
{
    if (mv->zoom_timer) {
        lv_timer_del(mv->zoom_timer);
        mv->zoom_timer = NULL;
    }
}

/* Long-ENTER hold: keep zooming out until the map's min zoom. */
static void map_view_zoom_timer_cb(lv_timer_t *timer)
{
    map_view_t *mv = timer->user_data;

    if (mv->view.zoom > mv->map.zoom_min)
        map_view_zoom_out(mv);
    else
        map_view_zoom_stop(mv);
}

/* Start the zoom-out repeat (first step happens immediately). */
static void map_view_zoom_repeat_start(map_view_t *mv)
{
    if (!mv->zoom_timer)
        mv->zoom_timer = lv_timer_create(map_view_zoom_timer_cb, 250, mv);
}

void map_view_pause(map_view_t *mv)
{
    if (!mv)
        return;
    map_view_pan_stop(mv);
    map_view_zoom_stop(mv);
}

/*
 * Build the widget: full-parent canvas + diagnostics overlay, open the
 * .map file and initialize the initial viewport.
 */
map_view_t *map_view_create(lv_obj_t *parent, const char *map_path)
{
    map_view_t *mv = calloc(1, sizeof(*mv));
    if (!mv)
        return NULL;
    mv->pan_key = KEY_NUM;
    mv->follow = 1;

    mv->buf = malloc(APP_SCR_HOR * APP_SCR_VER * sizeof(lv_color_t));
    if (!mv->buf) {
        LOG_E("map view: canvas alloc failed");
        free(mv);
        return NULL;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(0xf5f3ee), 0);

    /* Canvas fills the whole parent */
    mv->canvas = lv_canvas_create(parent);
    lv_obj_set_size(mv->canvas, APP_SCR_HOR, APP_SCR_VER);
    lv_canvas_set_buffer(mv->canvas, mv->buf, APP_SCR_HOR, APP_SCR_VER,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(mv->canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Top-left info label: semi-transparent black bg, white text */
    mv->info = lv_label_create(parent);
    lv_obj_set_style_text_font(mv->info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mv->info, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(mv->info, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mv->info, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(mv->info, 2, 0);
    lv_obj_set_style_radius(mv->info, 3, 0);
    lv_obj_align(mv->info, LV_ALIGN_TOP_LEFT, 2, 2);

    /* Path precedence: MF_MAP env (sim/dev) > argument > dev fallback. */
    const char *path = getenv("MF_MAP");
    if (!path || !path[0])
        path = (map_path && map_path[0]) ? map_path : MF_MAP_PATH;

    int rc = mf_open(&mv->map, path);
    mv->map_ok = (rc == MF_OK);
    if (!mv->map_ok) {
        lv_label_set_text_fmt(mv->info, "map open fail: %s", mferr_str(rc));
        LOG_E("map view: open %s failed: %s", path, mferr_str(rc));
        return mv;
    }
    LOG_I("map view: opened %s (zoom %d..%d)",
          path, (int)mv->map.zoom_min, (int)mv->map.zoom_max);

    /* Initial viewport: map start pos (or bbox center), start zoom */
    double lat, lon;
    int32_t zoom;
    if (mv->map.start_pos.lat_e6 >= 0) {
        lat = (double)mv->map.start_pos.lat_e6 / 1e6;
        lon = (double)mv->map.start_pos.lon_e6 / 1e6;
    } else {
        lat = ((double)mv->map.bbox_min.lat_e6 + mv->map.bbox_max.lat_e6) / 2e6;
        lon = ((double)mv->map.bbox_min.lon_e6 + mv->map.bbox_max.lon_e6) / 2e6;
    }
    const char *latov = getenv("SIM_LAT");
    if (latov)
        lat = atof(latov);
    const char *lonov = getenv("SIM_LON");
    if (lonov)
        lon = atof(lonov);
    zoom = mv->map.start_zoom > 0 ? mv->map.start_zoom
         : (mv->map.zoom_min + mv->map.zoom_max) / 2;
    const char *zov = getenv("SIM_ZOOM");
    if (zov)
        zoom = atoi(zov);
    if (zoom < mv->map.zoom_min)
        zoom = mv->map.zoom_min;
    if (zoom > mv->map.zoom_max)
        zoom = mv->map.zoom_max;

    mf_view_init(&mv->view, zoom, lat, lon, APP_SCR_HOR, APP_SCR_VER);
    map_view_redraw(mv);
    return mv;
}

/*
 * Free the map, the canvas buffer and any active repeat timers. The
 * LVGL objects (canvas/info) die with the parent screen.
 */
void map_view_delete(map_view_t *mv)
{
    if (!mv)
        return;
    map_view_pause(mv);
    if (mv->map_ok)
        mf_close(&mv->map);
    free(mv->buf);
    free(mv);
}

/*
 * Key handling: arrows pan (with long-press repeat), ENTER click zooms
 * in, ENTER held zooms out continuously - and reports "close me" when
 * held while already at the min zoom. BACK (sim ESC) mirrors that.
 * Key release stops the auto-pan/zoom timers.
 */
int map_view_on_key(map_view_t *mv, const key_event_t *ev)
{
    if (!mv || !mv->map_ok)
        return 0;

    if (!ev->is_press) {
        /* Key release: stop the long-press pan / zoom-out */
        if (ev->id == mv->pan_key)
            map_view_pan_stop(mv);
        if (ev->id == KEY_BTN_ENTER && ev->is_long)
            map_view_zoom_stop(mv);
        return 0;
    }

    switch (ev->id) {
    case KEY_BTN_UP:
        map_view_pan_start(mv, ev->id, 0, -PAN_STEP_PX);
        break;
    case KEY_BTN_DOWN:
        map_view_pan_start(mv, ev->id, 0, PAN_STEP_PX);
        break;
    case KEY_BTN_LEFT:
        map_view_pan_start(mv, ev->id, -PAN_STEP_PX, 0);
        break;
    case KEY_BTN_RIGHT:
        map_view_pan_start(mv, ev->id, PAN_STEP_PX, 0);
        break;
    case KEY_BTN_ENTER:
        if (ev->is_long) {
            if (mv->view.zoom > mv->map.zoom_min) {
                map_view_zoom_out(mv);
                map_view_zoom_repeat_start(mv);
            } else {
                /* Already at the min zoom: holding OK closes the map. */
                map_view_zoom_stop(mv);
                return 1;
            }
        } else {
            map_view_zoom_in(mv);
        }
        break;
    case KEY_BTN_BACK:
        /* Simulator-only key (ESC): raw semantics, no gesture layer. */
        if (mv->view.zoom > mv->map.zoom_min)
            map_view_zoom_out(mv);
        else
            return 1;
        break;
    default:
        break;
    }
    return 0;
}
