#include "map_view.h"
#include "nav_arrow_img.h"
#include "../Config/app_config.h"
#include "../../Mapsforge/mf_map.h"
#include "../../Mapsforge/mf_render.h"
#include "log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

/* Double-click OK window: the second click recenters + resumes follow.
 * Dev/sim override: MAPVIEW_DOUBLE_TAP_MS (scripted keys are widely
 * spaced; the hardware default stays 450 ms). */
#define DOUBLE_TAP_MS_DEFAULT 450
static uint32_t double_tap_ms = DOUBLE_TAP_MS_DEFAULT;

/* Course is only meaningful while actually moving (GPS COG jitters when
 * stationary); below this the arrow keeps its last heading. */
#define MOVE_MIN_KMH    1.0f

/* HUD palette: matches the status bar / GPS page dark chrome. */
#define HUD_BG      0x0A0E13
#define HUD_TEXT    0xE8EEF4
#define HUD_MUTE    0x8FA1B3
#define HUD_OK      0x2EE06B
#define HUD_WARN    0xFFB547

/* Scale bar represents this many pixels when picking the nice value. */
#define SCALE_BAR_PX   56

/* Marker images are 33x33 with the pivot in the middle. */
#define MARKER_SZ      33
#define MARKER_PIV     (MARKER_SZ / 2)

struct map_view {
    mf_map_t    map;
    int         map_ok;
    lv_obj_t   *canvas;
    lv_obj_t   *hud;         /* transparent HUD root (hide/show all) */
    lv_obj_t   *info;        /* diagnostics label, MF_DEBUG only */
    lv_color_t *buf;         /* canvas buffer, malloc'd (150 KB full frame) */
    mf_view_t   view;
    lv_timer_t *pan_timer;   /* Long-press auto pan timer */
    int32_t     pan_dx;      /* Repeat offset per tick */
    int32_t     pan_dy;
    key_id_t    pan_key;     /* Currently held key (KEY_NUM = none) */
    lv_timer_t *zoom_timer;  /* Long-ENTER zoom-out repeat timer */
    int         follow;      /* 1 = recenter on the GPS fix */
    uint32_t    follow_ms;   /* Last auto-recenter (rate limit) */

    /* HUD widgets */
    lv_obj_t   *img_live;    /* green arrow, rotated by course */
    lv_obj_t   *img_idle;    /* muted arrow, last known position */
    lv_obj_t   *acc_ring;    /* accuracy ring around the idle arrow */
    lv_obj_t   *dot;         /* gray dot while searching */
    lv_obj_t   *chip_north, *lbl_north;
    lv_obj_t   *chip_gps, *dot_gps, *lbl_gps;
    lv_obj_t   *chip_spd, *lbl_spd, *lbl_spd_unit;
    lv_obj_t   *chip_scale, *lbl_scale, *bar_scale, *tick_l, *tick_r;
    lv_obj_t   *pill, *lbl_pill, *pill_ticks[4];

    /* last fix cache (drives marker/badge/speed between snapshots) */
    int         seen_fix;    /* at least one fix since create */
    int         has_fix;     /* latest snapshot carried a fresh fix */
    double      fix_lat, fix_lon;
    float       course;
    float       speed_kmh;
    uint8_t     fix_type;
    uint8_t     sats;

    /* double-click OK detection */
    uint32_t    ok_ms;
    int         ok_zoomed;
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

/* ---------- HUD building blocks ---------- */

/* Transparent full-size HUD root; hiding it hides every overlay. */
static lv_obj_t *hud_root_create(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_pos(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    return r;
}

/* Semi-transparent dark chip (the HUD panel style). */
static lv_obj_t *hud_chip(lv_obj_t *parent, int x, int y, int w, int h,
                          int radius)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c, lv_color_hex(HUD_BG), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_80, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, radius, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    return c;
}

static lv_obj_t *hud_label(lv_obj_t *parent, int x, int y, const char *txt,
                           const lv_font_t *font, uint32_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    return l;
}

static lv_obj_t *hud_rect(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t col)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    return r;
}

/* Round dot (GPS badge status, searching marker). */
static lv_obj_t *hud_dot(lv_obj_t *parent, int x, int y, int d, uint32_t col)
{
    lv_obj_t *r = hud_rect(parent, x, y, d, d, col);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    return r;
}

/* Build the whole HUD once; widgets stay hidden until there is data. */
static void hud_create(map_view_t *mv)
{
    lv_obj_t *r = hud_root_create(mv->canvas->parent);
    mv->hud = r;

    /* North chip (map is always north-up) */
    mv->chip_north = hud_chip(r, 4, 26, 16, 16, 4);
    mv->lbl_north = hud_label(mv->chip_north, 0, 2, "N",
                              &lv_font_montserrat_10, HUD_TEXT);
    lv_obj_set_width(mv->lbl_north, 16);
    lv_obj_set_style_text_align(mv->lbl_north, LV_TEXT_ALIGN_CENTER, 0);

    /* GPS fix badge: dot + "3D - 12" / "2D - 8" / "SEARCH" */
    mv->chip_gps = hud_chip(r, 178, 26, 58, 16, 4);
    mv->dot_gps = hud_dot(mv->chip_gps, 4, 6, 4, HUD_WARN);
    mv->lbl_gps = hud_label(mv->chip_gps, 12, 3, "SEARCH",
                            &lv_font_montserrat_10, HUD_WARN);

    /* Speed chip: big number + km/h */
    mv->chip_spd = hud_chip(r, 4, 282, 108, 34, 4);
    mv->lbl_spd = hud_label(mv->chip_spd, 10, 4, "0.0",
                            &lv_font_montserrat_24, HUD_TEXT);
    mv->lbl_spd_unit = hud_label(mv->chip_spd, 78, 21, "km/h",
                                 &lv_font_montserrat_10, HUD_MUTE);

    /* Scale chip: fixed 56 px bar, label = rounded meters */
    mv->chip_scale = hud_chip(r, 164, 290, 72, 26, 4);
    mv->lbl_scale = hud_label(mv->chip_scale, 0, 2, "--",
                              &lv_font_montserrat_10, HUD_TEXT);
    lv_obj_set_width(mv->lbl_scale, 72);
    lv_obj_set_style_text_align(mv->lbl_scale, LV_TEXT_ALIGN_CENTER, 0);
    mv->bar_scale = hud_rect(mv->chip_scale, 8, 15, SCALE_BAR_PX, 2, HUD_TEXT);
    mv->tick_l = hud_rect(mv->chip_scale, 8, 12, 2, 8, HUD_TEXT);
    mv->tick_r = hud_rect(mv->chip_scale, 6 + SCALE_BAR_PX, 12, 2, 8,
                          HUD_TEXT);

    /* Marker stack (bottom = ring, then arrows, then dot on top) */
    mv->acc_ring = lv_obj_create(r);
    lv_obj_set_size(mv->acc_ring, 26, 26);
    lv_obj_clear_flag(mv->acc_ring,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(mv->acc_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mv->acc_ring, lv_color_hex(HUD_OK), 0);
    lv_obj_set_style_bg_opa(mv->acc_ring, LV_OPA_10, 0);
    lv_obj_set_style_border_color(mv->acc_ring, lv_color_hex(HUD_OK), 0);
    lv_obj_set_style_border_width(mv->acc_ring, 1, 0);
    lv_obj_set_style_border_opa(mv->acc_ring, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(mv->acc_ring, 0, 0);

    mv->img_idle = lv_img_create(r);
    lv_img_set_src(mv->img_idle, &nav_arrow_gray_img);
    lv_img_set_pivot(mv->img_idle, MARKER_PIV, MARKER_PIV);
    lv_img_set_antialias(mv->img_idle, true);

    mv->img_live = lv_img_create(r);
    lv_img_set_src(mv->img_live, &nav_arrow_solid_img);
    lv_img_set_pivot(mv->img_live, MARKER_PIV, MARKER_PIV);
    lv_img_set_antialias(mv->img_live, true);

    mv->dot = hud_dot(r, 0, 0, 9, HUD_MUTE);
    /* White rim so the dot reads on any map background */
    lv_obj_set_style_border_color(mv->dot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(mv->dot, 1, 0);

    /* Recenter pill: crosshair + "2xOK", green outline */
    mv->pill = lv_obj_create(r);
    lv_obj_set_size(mv->pill, 74, 18);
    lv_obj_set_pos(mv->pill, 83, 250);
    lv_obj_clear_flag(mv->pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(mv->pill, lv_color_hex(HUD_BG), 0);
    lv_obj_set_style_bg_opa(mv->pill, LV_OPA_90, 0);
    lv_obj_set_style_border_color(mv->pill, lv_color_hex(HUD_OK), 0);
    lv_obj_set_style_border_width(mv->pill, 1, 0);
    lv_obj_set_style_radius(mv->pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(mv->pill, 0, 0);

    lv_obj_t *icon = lv_obj_create(mv->pill);
    lv_obj_set_size(icon, 10, 10);
    lv_obj_set_pos(icon, 6, 3);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(icon, lv_color_hex(HUD_TEXT), 0);
    lv_obj_set_style_border_width(icon, 1, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    static const int t[4][4] = {
        { 4, -3, 2, 3 }, { 4, 10, 2, 3 }, { -3, 4, 3, 2 }, { 10, 4, 3, 2 },
    };
    for (int i = 0; i < 4; i++)
        mv->pill_ticks[i] = hud_rect(icon, t[i][0], t[i][1], t[i][2],
                                     t[i][3], HUD_TEXT);
    mv->lbl_pill = hud_label(mv->pill, 22, 4, "2xOK",
                             &lv_font_montserrat_10, HUD_TEXT);
}

/* Show/hide the whole HUD (used when the map file failed to open). */
static void hud_set_visible(map_view_t *mv, int on)
{
    if (!mv->hud)
        return;
    if (on)
        lv_obj_clear_flag(mv->hud, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(mv->hud, LV_OBJ_FLAG_HIDDEN);
}

/* Position a 33x33 marker image so its pivot sits on (x, y). */
static void marker_place(lv_obj_t *img, int x, int y)
{
    lv_obj_set_pos(img, x - MARKER_PIV, y - MARKER_PIV);
}

/* Update marker images/dot/ring from the cached fix state. */
static void hud_marker_update(map_view_t *mv)
{
    int cx = APP_SCR_HOR / 2, cy = APP_SCR_VER / 2;

    if (!mv->seen_fix) {
        /* Nothing ever received: searching dot at the screen center. */
        marker_place(mv->img_live, cx, cy);
        marker_place(mv->img_idle, cx, cy);
        lv_obj_set_pos(mv->acc_ring, cx - 13, cy - 13);
        lv_obj_set_pos(mv->dot, cx - 4, cy - 4);
        lv_obj_add_flag(mv->img_live, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mv->img_idle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mv->acc_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mv->dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_point_t p;
    mf_view_project(&mv->view, (int32_t)(mv->fix_lat * 1e6),
                    (int32_t)(mv->fix_lon * 1e6), &p);
    if (p.x < MARKER_PIV) p.x = MARKER_PIV;
    if (p.x > APP_SCR_HOR - MARKER_PIV - 1) p.x = APP_SCR_HOR - MARKER_PIV - 1;
    if (p.y < MARKER_PIV + 10) p.y = MARKER_PIV + 10;   /* below status bar */
    if (p.y > APP_SCR_VER - MARKER_PIV - 1) p.y = APP_SCR_VER - MARKER_PIV - 1;

    marker_place(mv->img_live, p.x, p.y);
    marker_place(mv->img_idle, p.x, p.y);
    lv_obj_set_pos(mv->acc_ring, p.x - 13, p.y - 13);
    lv_obj_set_pos(mv->dot, p.x - 4, p.y - 4);

    /* Green arrow while following (live position), muted arrow with an
     * accuracy ring while free-panning, gray dot when the fix went
     * stale - the approved plan-A marker semantics. */
    int live = mv->has_fix && mv->follow;
    if (live) {
        lv_obj_clear_flag(mv->img_live, LV_OBJ_FLAG_HIDDEN);
        lv_img_set_angle(mv->img_live,
                         ((int)(mv->course * 10.0f) % 3600 + 3600) % 3600);
    } else {
        lv_obj_add_flag(mv->img_live, LV_OBJ_FLAG_HIDDEN);
    }
    if (!mv->has_fix) {
        lv_obj_clear_flag(mv->dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mv->img_idle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mv->acc_ring, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mv->dot, LV_OBJ_FLAG_HIDDEN);
        if (live) {
            lv_obj_add_flag(mv->img_idle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(mv->acc_ring, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(mv->img_idle, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(mv->acc_ring, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Fix badge: 3D/2D + satellite count while fresh, SEARCH otherwise. */
static void hud_badge_update(map_view_t *mv)
{
    if (mv->has_fix) {
        const char *dim = mv->fix_type == 3 ? "3D"
                        : (mv->fix_type == 2 ? "2D" : "FIX");
        lv_label_set_text_fmt(mv->lbl_gps, "%s \xE2\x80\xA2 %u",
                              dim, (unsigned)mv->sats);
        lv_obj_set_style_text_color(mv->lbl_gps,
            lv_color_hex(mv->fix_type == 3 ? HUD_OK : HUD_WARN), 0);
        lv_obj_set_style_bg_color(mv->dot_gps,
            lv_color_hex(mv->fix_type == 3 ? HUD_OK : HUD_WARN), 0);
    } else {
        lv_label_set_text(mv->lbl_gps, "SEARCH");
        lv_obj_set_style_text_color(mv->lbl_gps, lv_color_hex(HUD_WARN), 0);
        lv_obj_set_style_bg_color(mv->dot_gps, lv_color_hex(HUD_WARN), 0);
    }
}

static void hud_speed_update(map_view_t *mv)
{
    int v10 = (int)(mv->speed_kmh * 10.0f + 0.5f);
    lv_label_set_text_fmt(mv->lbl_spd, "%d.%u", v10 / 10,
                          (unsigned)(v10 % 10));
}

/* Show the recenter pill exactly when follow is off but a fix was seen. */
static void hud_pill_update(map_view_t *mv)
{
    if (mv->map_ok && mv->seen_fix && !mv->follow)
        lv_obj_clear_flag(mv->pill, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(mv->pill, LV_OBJ_FLAG_HIDDEN);
}

/* Scale label from the viewport center latitude and zoom. */
static void hud_scale_update(map_view_t *mv)
{
    static const double nice[] = {
        20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000,
        100000, 200000, 500000, 1000000,
    };
    double lat_r = mv->view.center_lat * M_PI / 180.0;
    double mpp = 156543.03392 * cos(lat_r) / (double)(1LL << mv->view.zoom);
    double meters = mpp * SCALE_BAR_PX;

    double best = nice[0];
    for (unsigned i = 1; i < sizeof(nice) / sizeof(nice[0]); i++)
        if (fabs(log(nice[i] / meters)) < fabs(log(best / meters)))
            best = nice[i];

    char buf[16];
    if (best >= 995.0)
        snprintf(buf, sizeof(buf), "%u km", (unsigned)(best / 1000.0 + 0.5));
    else
        snprintf(buf, sizeof(buf), "%u m", (unsigned)(best + 0.5));
    lv_label_set_text(mv->lbl_scale, buf);
}

/* Refresh every data-driven HUD element. Cheap: object moves/text only. */
static void hud_update(map_view_t *mv)
{
    if (!mv->map_ok)
        return;
    hud_marker_update(mv);
    hud_badge_update(mv);
    hud_speed_update(mv);
    hud_pill_update(mv);
}

/* ---------- map rendering ---------- */

/*
 * Re-query the current viewport and render it onto the canvas, then
 * refresh the scale label (zoom/center changed) and the debug label.
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
        if (mv->info)
            lv_label_set_text_fmt(mv->info, "query error: %s", mferr_str(rc));
        lv_canvas_fill_bg(mv->canvas, lv_color_hex(0xf5f3ee), LV_OPA_COVER);
        /* A failed query may still hold objects parsed before the error. */
        mf_frame_free(&frame);
        return;
    }

    mf_render(&mv->map, &mv->view, &frame, mv->canvas);

    uint32_t dt = hal_tick_get() - t0;
    if (mv->info)
        lv_label_set_text_fmt(mv->info, "z%d %d obj %ums%s",
                              (int)mv->view.zoom, (int)frame.count,
                              (unsigned)dt, mv->follow ? " GPS" : "");
    lv_obj_invalidate(mv->canvas);

    hud_scale_update(mv);

    mf_frame_free(&frame);
}

/* ---------- GPS following ---------- */

/* Recenter the viewport on a lat/lon and redraw. */
static void map_view_recenter_ll(map_view_t *mv, double lat, double lon)
{
    mf_view_init(&mv->view, mv->view.zoom, lat, lon,
                 APP_SCR_HOR, APP_SCR_VER);
    mv->follow_ms = 0;
    map_view_redraw(mv);
    hud_update(mv);
}

void map_view_set_gps(map_view_t *mv, const gps_data_t *g)
{
    if (!mv || !mv->map_ok)
        return;

    if (g && g->fix) {
        mv->seen_fix = 1;
        mv->has_fix = 1;
        mv->fix_lat = g->lat;
        mv->fix_lon = g->lon;
        mv->fix_type = g->fix_type;
        mv->sats = g->sats;
        mv->speed_kmh = g->speed_kmh;
        if (g->speed_kmh > MOVE_MIN_KMH)
            mv->course = g->course_deg;

        if (mv->follow) {
            /* Dead-zone + rate limited auto-recenter. */
            lv_point_t p;
            mf_view_project(&mv->view, (int32_t)(g->lat * 1e6),
                            (int32_t)(g->lon * 1e6), &p);
            int32_t dx = (int32_t)p.x - APP_SCR_HOR / 2;
            int32_t dy = (int32_t)p.y - APP_SCR_VER / 2;
            uint32_t now = hal_tick_get();
            if (dx * dx + dy * dy > FOLLOW_DEAD_PX * FOLLOW_DEAD_PX &&
                (!mv->follow_ms || now - mv->follow_ms >= FOLLOW_MIN_MS)) {
                mv->follow_ms = now;
                map_view_recenter_ll(mv, g->lat, g->lon);
                return;
            }
        }
    } else {
        mv->has_fix = 0;
        if (g) {
            /* Keep the last position but show it as stale. */
            mv->speed_kmh = g->speed_kmh;
            if (g->speed_kmh > MOVE_MIN_KMH)
                mv->course = g->course_deg;
        }
    }

    hud_update(mv);
}

void map_view_recenter(map_view_t *mv, const gps_data_t *g)
{
    if (!mv || !mv->map_ok || !g || !g->fix)
        return;
    mv->seen_fix = 1;
    mv->has_fix = 1;
    mv->fix_lat = g->lat;
    mv->fix_lon = g->lon;
    mv->fix_type = g->fix_type;
    mv->sats = g->sats;
    mv->speed_kmh = g->speed_kmh;
    map_view_recenter_ll(mv, g->lat, g->lon);
}

void map_view_set_follow(map_view_t *mv, int follow)
{
    if (!mv)
        return;
    mv->follow = follow ? 1 : 0;
    hud_update(mv);
}

/* ---------- panning / zooming ---------- */

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
    /* Manual panning takes the map off the fix: stop following until
     * the owner re-enables it. */
    if (mv->follow) {
        mv->follow = 0;
        LOG_I("map view: GPS follow off (manual pan)");
    }
    mf_view_pan(&mv->view, dx, dy);
    map_view_redraw(mv);
    hud_update(mv);
}

/* Zoom in one level if not already at the map's max zoom. Returns 1
 * when the zoom actually changed (needed to undo a double-click). */
static int map_view_zoom_in(map_view_t *mv)
{
    if (mv->view.zoom >= mv->map.zoom_max)
        return 0;
    mv->view.zoom++;
    map_view_redraw(mv);
    hud_update(mv);
    return 1;
}

/* Zoom out one level if not already at the map's min zoom. */
static void map_view_zoom_out(map_view_t *mv)
{
    if (mv->view.zoom > mv->map.zoom_min) {
        mv->view.zoom--;
        map_view_redraw(mv);
        hud_update(mv);
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

void map_view_break_gesture(map_view_t *mv)
{
    if (!mv)
        return;
    map_view_pause(mv);
    mv->ok_ms = 0;
}

int map_view_is_ok(const map_view_t *mv)
{
    return mv ? mv->map_ok : 0;
}

/* ---------- map file handling ---------- */

int map_view_open(map_view_t *mv, const char *path)
{
    if (!mv || !path || !path[0])
        return -1;

    if (mv->map_ok) {
        mf_close(&mv->map);
        mv->map_ok = 0;
    }

    int rc = mf_open(&mv->map, path);
    mv->map_ok = (rc == MF_OK);
    hud_set_visible(mv, mv->map_ok);
    if (!mv->map_ok) {
        if (mv->info)
            lv_label_set_text_fmt(mv->info, "map open fail: %s", mferr_str(rc));
        LOG_E("map view: open %s failed: %s", path, mferr_str(rc));
        return -1;
    }
    LOG_I("map view: opened %s (zoom %d..%d)",
          path, (int)mv->map.zoom_min, (int)mv->map.zoom_max);

    /* Keep the viewport when it lies inside the new map, else jump to
     * the new map's start position; clamp the zoom to the new range. */
    double lat = mv->view.center_lat, lon = mv->view.center_lon;
    if (lat < mv->map.bbox_min.lat_e6 / 1e6 ||
        lat > mv->map.bbox_max.lat_e6 / 1e6 ||
        lon < mv->map.bbox_min.lon_e6 / 1e6 ||
        lon > mv->map.bbox_max.lon_e6 / 1e6) {
        if (mv->map.start_pos.lat_e6 >= 0) {
            lat = (double)mv->map.start_pos.lat_e6 / 1e6;
            lon = (double)mv->map.start_pos.lon_e6 / 1e6;
        } else {
            lat = ((double)mv->map.bbox_min.lat_e6 +
                   mv->map.bbox_max.lat_e6) / 2e6;
            lon = ((double)mv->map.bbox_min.lon_e6 +
                   mv->map.bbox_max.lon_e6) / 2e6;
        }
    }
    int32_t zoom = mv->view.zoom;
    if (zoom < mv->map.zoom_min)
        zoom = mv->map.zoom_min;
    if (zoom > mv->map.zoom_max)
        zoom = mv->map.zoom_max;

    mf_view_init(&mv->view, zoom, lat, lon, APP_SCR_HOR, APP_SCR_VER);
    map_view_redraw(mv);
    hud_update(mv);
    return 0;
}

/*
 * Build the widget: full-parent canvas + HUD, open the .map file and
 * initialize the initial viewport.
 */
map_view_t *map_view_create(lv_obj_t *parent, const char *map_path)
{
    map_view_t *mv = calloc(1, sizeof(*mv));
    if (!mv)
        return NULL;
    mv->pan_key = KEY_NUM;
    mv->follow = 1;
    {
        const char *ov = getenv("MAPVIEW_DOUBLE_TAP_MS");
        if (ov)
            double_tap_ms = (uint32_t)strtoul(ov, NULL, 10);
    }

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

    hud_create(mv);

    /* Diagnostics overlay is developer-only (MF_DEBUG=1). */
    if (getenv("MF_DEBUG")) {
        mv->info = lv_label_create(parent);
        lv_obj_set_style_text_font(mv->info, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(mv->info, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_color(mv->info, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(mv->info, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(mv->info, 2, 0);
        lv_obj_set_style_radius(mv->info, 3, 0);
        lv_obj_align(mv->info, LV_ALIGN_TOP_LEFT, 2, 2);
    }

    /* Path precedence: MF_MAP env (sim/dev) > argument > dev fallback. */
    const char *path = getenv("MF_MAP");
    if (!path || !path[0])
        path = (map_path && map_path[0]) ? map_path : MF_MAP_PATH;

    int rc = mf_open(&mv->map, path);
    mv->map_ok = (rc == MF_OK);
    hud_set_visible(mv, mv->map_ok);
    if (!mv->map_ok) {
        if (mv->info)
            lv_label_set_text_fmt(mv->info, "map open fail: %s", mferr_str(rc));
        LOG_E("map view: open %s failed: %s", path, mferr_str(rc));
        return mv;
    }
    LOG_I("map view: opened %s (zoom %d..%d)",
          path, (int)mv->map.zoom_min, (int)mv->map.zoom_max);

    /* Initial viewport: map start pos (or bbox center), start zoom */
    double lat, lon;
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
    int32_t zoom = mv->map.start_zoom > 0 ? mv->map.start_zoom
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
    hud_update(mv);
    return mv;
}

/*
 * Free the map, the canvas buffer and any active repeat timers. The
 * LVGL objects (canvas/HUD) die with the parent screen.
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
 * in, double-click recenters and resumes follow, ENTER held zooms out
 * continuously - and reports "close me" when held while already at the
 * min zoom. BACK (sim ESC) mirrors that. Key release stops the
 * auto-pan/zoom timers.
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
            /* Short click: zoom in; a second click inside the window is
             * the "recenter + follow" gesture (undo the first zoom). */
            uint32_t now = hal_tick_get();
            if (mv->ok_ms && now - mv->ok_ms < double_tap_ms) {
                if (mv->ok_zoomed)
                    map_view_zoom_out(mv);
                mv->follow = 1;
                if (mv->seen_fix)
                    map_view_recenter_ll(mv, mv->fix_lat, mv->fix_lon);
                else
                    hud_update(mv);
                mv->ok_ms = 0;
            } else {
                mv->ok_zoomed = map_view_zoom_in(mv);
                mv->ok_ms = now;
            }
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
