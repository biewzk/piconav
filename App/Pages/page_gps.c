#include "page_gps.h"
#include "../Utils/status_bar.h"
#include "../Utils/gps_service.h"
#include "../app.h"

#include <stdio.h>
#include <math.h>

/* Layout constants: coordinates follow the reviewed SVG design (240x320,
 * status bar on top). All x/y are absolute panel coordinates. */
#define SKY_CX      72
#define SKY_CY      115
#define SKY_R       54
#define SNR_BASE_Y  252       /* bar baseline */
#define SNR_MAX_H   42        /* px at 55 dB-Hz */
#define SNR_MAX_DB  55

#define GPS_WIDGETS_MAX 12    /* sky dots and SNR bars shown at once */

/* Palette (outdoor professional dark theme) */
#define C_BG      0x0B0F14
#define C_HEADER  0x0E141B
#define C_TEXT    0xE8EEF4
#define C_MUTED   0x8FA1B3
#define C_FAINT   0x3A4C5E
#define C_HAIR    0x223243
#define C_HAIR2   0x182430
#define C_RING    0x2A3B4C
#define C_RING_IN 0x1C2A38
#define C_CROSS   0x16222E
#define C_CYAN    0x00C2FF
#define C_GREEN   0x2EE06B
#define C_GREEN_D 0x062812
#define C_AMBER   0xFFB547
#define C_AMBER_D 0x241500
#define C_GREY    0x4A5A6A
#define C_VIEW    0x5A6E80
#define C_DIMTXT  0x263442

/* Degree sign U+00B0 as UTF-8 (LVGL text encoding); the glyph is in
 * every built-in Montserrat range (0x20-0x7F, 0xB0, 0x2022). */
#define DEG_STR "\xC2\xB0"

typedef struct {
    /* header */
    lv_obj_t *badge;
    lv_obj_t *badge_lbl;
    lv_obj_t *dot;
    /* right column */
    lv_obj_t *big_lbl;
    lv_obj_t *view_lbl;
    lv_obj_t *hdop_lbl;
    lv_obj_t *utc_lbl;
    /* position + bottom strip */
    lv_obj_t *lat_lbl;
    lv_obj_t *lon_lbl;
    lv_obj_t *alt_lbl;
    lv_obj_t *spd_lbl;
    lv_obj_t *crs_lbl;
    /* sky plot + SNR widget pools */
    lv_obj_t *sky[GPS_WIDGETS_MAX];
    lv_obj_t *sky_lbl[GPS_WIDGETS_MAX];
    lv_obj_t *bar[GPS_WIDGETS_MAX];
    lv_obj_t *bar_lbl[GPS_WIDGETS_MAX];
    lv_obj_t *center_dot;
    lv_timer_t *timer;
} gps_priv_t;

static gps_priv_t s_gps;    /* single panel instance */

/* ---------- small builders ---------- */

static lv_obj_t *gps_rect(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t bg)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(r, bg ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    return r;
}

static lv_obj_t *gps_ring(lv_obj_t *parent, int x, int y, int d, uint32_t col)
{
    lv_obj_t *r = gps_rect(parent, x, y, d, d, 0);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(col), 0);
    lv_obj_set_style_border_width(r, 1, 0);
    return r;
}

static lv_obj_t *gps_label(lv_obj_t *parent, int x, int y, const char *txt,
                           const lv_font_t *font, uint32_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* Value label with a fixed box so right alignment never depends on the
 * current text width (right edge at x = 234). */
static lv_obj_t *gps_value(lv_obj_t *parent, int y, const char *txt,
                           const lv_font_t *font, uint32_t col)
{
    lv_obj_t *l = gps_label(parent, 150, y, txt, font, col);
    lv_obj_set_width(l, 84);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

/* ---------- formatting ---------- */

/* dd(degree)mm.mmm' H from signed degrees; hemisphere picked by axis. */
static void fmt_latlon(double v, char north, char south, char *buf, size_t n)
{
    char hemi = v >= 0 ? north : south;
    double av = v < 0 ? -v : v;
    int deg = (int)av;
    double min = (av - (double)deg) * 60.0;
    if (min >= 59.9995) {
        deg++;
        min = 0.0;
    }
    int mi = (int)min;
    int mf = (int)((min - (double)mi) * 1000.0 + 0.5);
    if (mf >= 1000) {
        mf = 0;
        mi++;
        if (mi >= 60) { mi = 0; deg++; }
    }
    snprintf(buf, n, "%d" DEG_STR "%02d.%03d' %c", deg, mi, mf, hemi);
}

/* ---------- satellite views ---------- */

/* One dot on the sky plot: azimuth around the circle, elevation as the
 * radius (rim = horizon, centre = zenith). */
static void sky_place(lv_obj_t *dot, int elev, int azim)
{
    float r = (float)SKY_R * (1.0f - (float)elev / 90.0f);
    float a = (float)azim * 3.1415926f / 180.0f;
    int x = SKY_CX + (int)(r * sinf(a)) - 7;
    int y = SKY_CY - (int)(r * cosf(a)) - 7;
    lv_obj_set_pos(dot, x, y);
}

static void sky_style(lv_obj_t *dot, lv_obj_t *lbl, bool used, bool fix)
{
    if (used) {
        lv_obj_set_style_bg_color(dot, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_GREEN_D), 0);
    } else if (fix) {
        lv_obj_set_style_bg_color(dot, lv_color_hex(C_BG), 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(C_VIEW), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
    } else {
        lv_obj_set_style_bg_color(dot, lv_color_hex(C_BG), 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(C_AMBER), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_AMBER), 0);
    }
}

/* 0 = strongest satellite; used first, then by signal strength. */
static int sat_rank(const gps_data_t *g, int i)
{
    int rank = 0;
    for (int j = 0; j < GPS_SATS_MAX; j++) {
        if (j == i || !g->sat[j].seen)
            continue;
        if (g->sat[j].used > g->sat[i].used)
            rank++;
        else if (g->sat[j].used == g->sat[i].used &&
                 g->sat[j].snr > g->sat[i].snr)
            rank++;
    }
    return rank;
}

/* ---------- refresh ---------- */

static void gps_apply(void)
{
    const gps_data_t *g = app_gps();
    /* Scale the staleness window with the active sample interval so a
     * slow (5 s) sampling profile is not mistaken for a dead receiver. */
    bool published = g->stamp_ms != 0;
    bool stale = !published || (hal_tick_get() - g->stamp_ms > gps_service_stale_ms());
    bool live = published && !stale;

    /* Badge + live dot */
    const char *badge_txt;
    uint32_t badge_bg, badge_fg, dot_col;
    if (stale) {
        badge_txt = "NO DATA";
        badge_bg = C_FAINT;
        badge_fg = C_BG;
        dot_col = C_GREY;
    } else if (g->fix) {
        badge_txt = g->fix_type == 3 ? "3D FIX" :
                    g->fix_type == 2 ? "2D FIX" : "FIX";
        badge_bg = C_GREEN;
        badge_fg = C_GREEN_D;
        dot_col = C_GREEN;
    } else {
        badge_txt = "SEARCH";
        badge_bg = C_AMBER;
        badge_fg = C_AMBER_D;
        dot_col = C_AMBER;
    }
    lv_label_set_text(s_gps.badge_lbl, badge_txt);
    lv_obj_set_style_bg_color(s_gps.badge, lv_color_hex(badge_bg), 0);
    lv_obj_set_style_text_color(s_gps.badge_lbl, lv_color_hex(badge_fg), 0);
    lv_obj_set_style_bg_color(s_gps.dot, lv_color_hex(dot_col), 0);

    /* Right column */
    if (g->fix && live) {
        lv_label_set_text_fmt(s_gps.big_lbl, "%u", (unsigned)g->sats);
        lv_obj_set_style_text_color(s_gps.big_lbl, lv_color_hex(C_CYAN), 0);
    } else if (stale) {
        lv_label_set_text(s_gps.big_lbl, "--");
        lv_obj_set_style_text_color(s_gps.big_lbl, lv_color_hex(C_GREY), 0);
    } else {
        lv_label_set_text(s_gps.big_lbl, "0");
        lv_obj_set_style_text_color(s_gps.big_lbl, lv_color_hex(C_GREY), 0);
    }

    if (live) {
        lv_label_set_text_fmt(s_gps.view_lbl, "%u", (unsigned)g->sats_view);
    } else {
        lv_label_set_text(s_gps.view_lbl, stale ? "--" : "0");
    }

    if (g->fix && live) {
        char hbuf[16];
        snprintf(hbuf, sizeof(hbuf), "%.1f", (double)g->hdop);
        lv_label_set_text(s_gps.hdop_lbl, hbuf);
        lv_obj_set_style_text_color(s_gps.hdop_lbl, lv_color_hex(
            g->hdop < 2.0f ? C_GREEN : (g->hdop < 5.0f ? C_AMBER : 0xFF5A5A)), 0);
    } else {
        lv_label_set_text(s_gps.hdop_lbl, "--.-");
        lv_obj_set_style_text_color(s_gps.hdop_lbl, lv_color_hex(C_FAINT), 0);
    }

    if (g->utc != 0xFFFFFFFFu) {
        lv_label_set_text_fmt(s_gps.utc_lbl, "%02u:%02u:%02u",
                              (unsigned)(g->utc / 3600) % 24,
                              (unsigned)(g->utc / 60) % 60,
                              (unsigned)g->utc % 60);
    } else {
        lv_label_set_text(s_gps.utc_lbl, "--:--:--");
    }

    /* Sky plot + SNR bars */
    for (int i = 0; i < GPS_WIDGETS_MAX; i++) {
        lv_obj_add_flag(s_gps.sky[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_gps.bar[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_gps.bar_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_gps.center_dot, LV_OBJ_FLAG_HIDDEN);

    if (live) {
        int n_sky = 0, n_bar = 0;
        for (int i = 0; i < GPS_SATS_MAX && (n_sky < GPS_WIDGETS_MAX ||
                                             n_bar < GPS_WIDGETS_MAX); i++) {
            const gps_sat_t *s = &g->sat[i];
            if (!s->seen || sat_rank(g, i) >= GPS_WIDGETS_MAX)
                continue;

            if (n_sky < GPS_WIDGETS_MAX && s->elev != 0xFF && s->azim >= 0) {
                lv_obj_t *dot = s_gps.sky[n_sky];
                lv_obj_t *lbl = s_gps.sky_lbl[n_sky];
                lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
                sky_place(dot, s->elev, s->azim);
                sky_style(dot, lbl, s->used != 0, g->fix != 0);
                lv_label_set_text_fmt(lbl, "%u", (unsigned)s->prn);
                n_sky++;
            }
            if (n_bar < GPS_WIDGETS_MAX && s->snr >= 0) {
                int h = s->snr * SNR_MAX_H / SNR_MAX_DB;
                if (h < 2)
                    h = 2;
                lv_obj_t *b = s_gps.bar[n_bar];
                lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_height(b, h);
                lv_obj_set_y(b, SNR_BASE_Y - h);
                lv_obj_set_style_bg_color(b, lv_color_hex(
                    s->used ? C_GREEN : (g->fix ? C_VIEW : C_AMBER)), 0);
                lv_obj_t *bl = s_gps.bar_lbl[n_bar];
                lv_obj_clear_flag(bl, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text_fmt(bl, "%u", (unsigned)s->prn);
                n_bar++;
            }
        }
        if (!g->fix)
            lv_obj_clear_flag(s_gps.center_dot, LV_OBJ_FLAG_HIDDEN);
    }

    /* Position */
    if (g->fix && live) {
        char buf[24];
        fmt_latlon(g->lat, 'N', 'S', buf, sizeof(buf));
        lv_label_set_text(s_gps.lat_lbl, buf);
        fmt_latlon(g->lon, 'E', 'W', buf, sizeof(buf));
        lv_label_set_text(s_gps.lon_lbl, buf);
        lv_obj_set_style_text_color(s_gps.lat_lbl, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_text_color(s_gps.lon_lbl, lv_color_hex(C_TEXT), 0);
    } else {
        lv_label_set_text(s_gps.lat_lbl, "--" DEG_STR "--.---' N");
        lv_label_set_text(s_gps.lon_lbl, "---" DEG_STR "--.---' E");
        lv_obj_set_style_text_color(s_gps.lat_lbl, lv_color_hex(C_FAINT), 0);
        lv_obj_set_style_text_color(s_gps.lon_lbl, lv_color_hex(C_FAINT), 0);
    }

    /* Bottom strip (inline label + value per cell) */
    char buf[20];
    if (g->fix && live) {
        int a10 = (int)(g->alt_m * 10.0f + (g->alt_m < 0 ? -0.5f : 0.5f));
        snprintf(buf, sizeof(buf), "%d.%um", a10 / 10,
                 a10 < 0 ? -(a10 % 10) : a10 % 10);
        lv_label_set_text(s_gps.alt_lbl, buf);
        int s10 = (int)(g->speed_kmh * 10.0f + 0.5f);
        snprintf(buf, sizeof(buf), "%d.%ukm/h", s10 / 10, s10 % 10);
        lv_label_set_text(s_gps.spd_lbl, buf);
        unsigned crs = (unsigned)(g->course_deg + 0.5f) % 360u;
        snprintf(buf, sizeof(buf), "%03u" DEG_STR, crs);
        lv_label_set_text(s_gps.crs_lbl, buf);
        lv_obj_set_style_text_color(s_gps.alt_lbl, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_text_color(s_gps.spd_lbl, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_text_color(s_gps.crs_lbl, lv_color_hex(C_TEXT), 0);
    } else {
        lv_label_set_text(s_gps.alt_lbl, "--.- m");
        lv_label_set_text(s_gps.spd_lbl, "-.-km/h");
        lv_label_set_text(s_gps.crs_lbl, "---" DEG_STR);
        lv_obj_set_style_text_color(s_gps.alt_lbl, lv_color_hex(C_FAINT), 0);
        lv_obj_set_style_text_color(s_gps.spd_lbl, lv_color_hex(C_FAINT), 0);
        lv_obj_set_style_text_color(s_gps.crs_lbl, lv_color_hex(C_FAINT), 0);
    }
}

static void gps_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    gps_apply();
}

static void gps_on_delete(lv_event_t *e)
{
    (void)e;
    if (s_gps.timer) {
        lv_timer_del(s_gps.timer);
        s_gps.timer = NULL;
    }
}

/* ---------- build ---------- */

void page_gps_panel_create(lv_obj_t *parent)
{
    lv_obj_t *p = parent;
    lv_obj_set_style_bg_color(p, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);

    /* Global status bar (reserved chrome; other pages can adopt it) */
    status_bar_create(p);

    /* Page header */
    gps_rect(p, 0, 22, 240, 26, C_HEADER);
    gps_rect(p, 0, 47, 240, 1, C_HAIR);
    gps_label(p, 8, 26, "GPS", &lv_font_montserrat_16, C_TEXT);
    lv_obj_t *sub = gps_label(p, 42, 32, "RECEIVER",
                              &lv_font_montserrat_10, C_FAINT);
    lv_obj_set_style_text_letter_space(sub, 2, 0);
    s_gps.dot = gps_rect(p, 155, 33, 5, 5, C_GREEN);
    lv_obj_set_style_radius(s_gps.dot, LV_RADIUS_CIRCLE, 0);
    s_gps.badge = gps_rect(p, 168, 28, 64, 15, C_GREEN);
    lv_obj_set_style_radius(s_gps.badge, LV_RADIUS_CIRCLE, 0);
    s_gps.badge_lbl = gps_label(s_gps.badge, 0, 1, "--",
                                &lv_font_montserrat_10, C_GREEN_D);
    lv_obj_set_width(s_gps.badge_lbl, 64);
    lv_obj_set_style_text_align(s_gps.badge_lbl, LV_TEXT_ALIGN_CENTER, 0);

    /* Sky plot: outer ring = horizon, centre = zenith */
    gps_ring(p, 18, 61, 108, C_RING);
    gps_ring(p, 36, 79, 72, C_RING_IN);
    gps_ring(p, 54, 97, 36, C_RING_IN);
    gps_rect(p, 18, 115, 108, 1, C_CROSS);
    gps_rect(p, 72, 61, 1, 108, C_CROSS);
    gps_label(p, 68, 48, "N", &lv_font_montserrat_10, C_FAINT);
    gps_label(p, 129, 111, "E", &lv_font_montserrat_10, C_FAINT);
    gps_label(p, 4, 111, "W", &lv_font_montserrat_10, C_FAINT);
    gps_label(p, 68, 173, "S", &lv_font_montserrat_10, C_FAINT);
    gps_label(p, 15, 111, "30" DEG_STR, &lv_font_montserrat_10, C_FAINT);
    s_gps.center_dot = gps_rect(p, 70, 113, 5, 5, C_AMBER);
    lv_obj_set_style_radius(s_gps.center_dot, LV_RADIUS_CIRCLE, 0);

    /* Satellite dots (pool): fixed 14 px box, PRN label centred inside */
    for (int i = 0; i < GPS_WIDGETS_MAX; i++) {
        lv_obj_t *c = lv_obj_create(p);
        lv_obj_set_size(c, 14, 14);
        lv_obj_set_pos(c, -40, -40);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(C_BG), 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        s_gps.sky[i] = c;

        lv_obj_t *l = lv_label_create(c);
        lv_label_set_text(l, "");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_width(l, 14);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        s_gps.sky_lbl[i] = l;
    }

    /* Right stats column */
    gps_rect(p, 140, 52, 1, 130, C_HAIR2);
    gps_label(p, 150, 56, "USED SATS", &lv_font_montserrat_10, C_MUTED);
    s_gps.big_lbl = gps_label(p, 146, 70, "--", &lv_font_montserrat_32, C_CYAN);
    gps_rect(p, 146, 108, 88, 1, C_HAIR2);
    gps_label(p, 150, 118, "IN VIEW", &lv_font_montserrat_10, C_MUTED);
    s_gps.view_lbl = gps_value(p, 116, "0", &lv_font_montserrat_14, C_TEXT);
    gps_rect(p, 146, 134, 88, 1, C_HAIR2);
    gps_label(p, 150, 144, "HDOP", &lv_font_montserrat_10, C_MUTED);
    s_gps.hdop_lbl = gps_value(p, 142, "--.-", &lv_font_montserrat_14, C_TEXT);
    gps_rect(p, 146, 160, 88, 1, C_HAIR2);
    gps_label(p, 150, 170, "UTC", &lv_font_montserrat_10, C_MUTED);
    s_gps.utc_lbl = gps_value(p, 169, "--:--:--", &lv_font_montserrat_12, C_TEXT);

    /* SNR strip */
    gps_label(p, 8, 193, "SNR dB-Hz", &lv_font_montserrat_10, C_FAINT);
    gps_rect(p, 146, 193, 8, 8, C_GREEN);
    gps_label(p, 158, 193, "USED", &lv_font_montserrat_10, C_FAINT);
    lv_obj_t *sw2 = gps_rect(p, 196, 193, 8, 8, 0);
    lv_obj_set_style_border_color(sw2, lv_color_hex(C_VIEW), 0);
    lv_obj_set_style_border_width(sw2, 1, 0);
    gps_label(p, 208, 193, "VIEW", &lv_font_montserrat_10, C_FAINT);
    gps_rect(p, 18, 218, 216, 1, C_HAIR);
    gps_rect(p, 18, 233, 216, 1, C_HAIR);
    gps_label(p, 2, 213, "45", &lv_font_montserrat_10, C_FAINT);
    gps_label(p, 2, 228, "25", &lv_font_montserrat_10, C_FAINT);
    gps_rect(p, 18, SNR_BASE_Y, 216, 1, C_RING);

    for (int i = 0; i < GPS_WIDGETS_MAX; i++) {
        int x = 21 + (i * 175 + 50) / 10;    /* 17.5 px pitch, rounded */
        s_gps.bar[i] = gps_rect(p, x, SNR_BASE_Y - 2, 11, 2, C_GREEN);
        lv_obj_t *bl = gps_label(p, x - 5, 255, "",
                                 &lv_font_montserrat_10, C_FAINT);
        lv_obj_set_width(bl, 21);
        lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
        s_gps.bar_lbl[i] = bl;
    }

    /* Position */
    gps_rect(p, 0, 268, 240, 1, C_HAIR);
    gps_label(p, 8, 274, "LAT", &lv_font_montserrat_10, C_MUTED);
    s_gps.lat_lbl = gps_label(p, 40, 272, "--", &lv_font_montserrat_14, C_FAINT);
    gps_label(p, 8, 292, "LON", &lv_font_montserrat_10, C_MUTED);
    s_gps.lon_lbl = gps_label(p, 40, 290, "--", &lv_font_montserrat_14, C_FAINT);
    gps_rect(p, 0, 305, 240, 1, C_HAIR);

    /* Bottom strip: ALT / SPD / CRS in single-line cells */
    gps_rect(p, 80, 307, 1, 13, C_HAIR2);
    gps_rect(p, 160, 307, 1, 13, C_HAIR2);
    gps_label(p, 8, 308, "ALT", &lv_font_montserrat_10, C_FAINT);
    s_gps.alt_lbl = gps_label(p, 30, 306, "--.- m", &lv_font_montserrat_12, C_FAINT);
    gps_label(p, 88, 308, "SPD", &lv_font_montserrat_10, C_FAINT);
    s_gps.spd_lbl = gps_label(p, 110, 306, "-.-km/h", &lv_font_montserrat_12, C_FAINT);
    gps_label(p, 168, 308, "CRS", &lv_font_montserrat_10, C_FAINT);
    s_gps.crs_lbl = gps_label(p, 190, 306, "---" DEG_STR, &lv_font_montserrat_12, C_FAINT);

    /* 1 s refresh: values change at the NMEA rate, staleness needs a tick */
    s_gps.timer = lv_timer_create(gps_timer_cb, 1000, NULL);
    lv_obj_add_event_cb(p, gps_on_delete, LV_EVENT_DELETE, NULL);
    gps_apply();
}
