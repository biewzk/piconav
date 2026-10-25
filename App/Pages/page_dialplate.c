#include "page_dialplate.h"
#include "../Config/app_config.h"
#include "../Utils/page_manager.h"
#include "../Utils/gps_service.h"
#include "../Utils/trip_store.h"
#include "../Utils/log.h"
#include "../app.h"

#include <stdio.h>
#include <stdlib.h>

/* Per-page private data (freed in dialplate_destroy). */
typedef struct {
    lv_obj_t *speed_label;   /* Big speed digit */
    lv_obj_t *unit_label;
    lv_obj_t *fix_label;     /* "NO FIX" / "GPS n SAT" */
    lv_obj_t *dist_label;
    lv_obj_t *time_label;
    msg_sub_t *gps_sub;      /* MSG_GPS subscription while visible */
} dial_priv_t;

/* mm:ss (or h:mm:ss once over an hour) for the moving-time card. */
static void format_duration(uint32_t total_ms, char *buf, size_t len)
{
    uint32_t s = total_ms / 1000;
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    uint32_t sec = s % 60;

    if (h > 0)
        snprintf(buf, len, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)sec);
    else
        snprintf(buf, len, "%02u:%02u", (unsigned)m, (unsigned)sec);
}

/*
 * Refresh the dial from a GPS snapshot: speed, fix indicator, and the
 * trip counters (integrated by trip_store, not by this page - they keep
 * running while other pages are on top).
 */
static void dialplate_apply(dial_priv_t *d, const gps_data_t *g)
{
    if (!d || !g)
        return;

    if (g->fix) {
        lv_label_set_text_fmt(d->fix_label, "GPS %u SAT", (unsigned)g->sats);
        lv_obj_set_style_text_color(d->fix_label, lv_color_hex(0x00ff88), 0);
        lv_label_set_text_fmt(d->speed_label, "%d", (int)(g->speed_kmh + 0.5f));
    } else {
        lv_label_set_text(d->fix_label, "NO FIX");
        lv_obj_set_style_text_color(d->fix_label, lv_color_hex(0xff5555), 0);
        lv_label_set_text(d->speed_label, "--");
    }

    const trip_stats_t *t = trip_store_get();

    char tbuf[16];
    format_duration(t->move_ms, tbuf, sizeof(tbuf));

    /* LVGL's lv_snprintf has float support disabled (LV_SPRINTF_USE_FLOAT
     * 0), so format the trip distance with the libc printf instead. */
    long km100 = (long)(t->trip_km * 100.0 + 0.5);
    char dbuf[40];
    snprintf(dbuf, sizeof(dbuf), "%ld.%02ld km", km100 / 100, km100 % 100);

    lv_label_set_text(d->dist_label, dbuf);
    lv_label_set_text(d->time_label, tbuf);
}

/* MSG_GPS handler: a fresh NMEA snapshot was parsed by the HAL. */
static void dialplate_on_gps(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    page_t *self = user;
    if (self && self->user)
        dialplate_apply(self->user, data);
}

/* Subscribe on entry and paint the last known fix immediately (the next
 * pool refresh can be up to a sample interval away). The dial is the
 * riding view: 1 s samples keep the trip integrator inside its 2 s gap
 * rule and the speed readout live. */
static void dialplate_on_show(page_t *self)
{
    dial_priv_t *d = self->user;
    if (!d)
        return;

    gps_service_demand_add();
    dialplate_apply(d, app_gps());

    if (!d->gps_sub)
        d->gps_sub = msg_center_subscribe(app_msg_center(), MSG_GPS,
                                          dialplate_on_gps, self);
}

static void dialplate_on_hide(page_t *self)
{
    dial_priv_t *d = self->user;

    gps_service_demand_release();
    if (d && d->gps_sub) {
        msg_center_unsubscribe(app_msg_center(), MSG_GPS, d->gps_sub);
        d->gps_sub = NULL;
    }
}

/*
 * Build the dial plate: fix indicator on top, big speed in the center,
 * trip distance/time cards at the bottom, and a key hint line.
 */
static void dialplate_create(page_t *self)
{
    dial_priv_t *d = calloc(1, sizeof(dial_priv_t));
    self->user = d;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    /* Top: GPS fix / satellite count */
    d->fix_label = lv_label_create(scr);
    lv_label_set_text(d->fix_label, "NO FIX");
    lv_obj_set_style_text_font(d->fix_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d->fix_label, lv_color_hex(0xff5555), 0);
    lv_obj_align(d->fix_label, LV_ALIGN_TOP_MID, 0, 14);

    /* Center: big speed digit */
    d->speed_label = lv_label_create(scr);
    lv_label_set_text(d->speed_label, "--");
    lv_obj_set_style_text_font(d->speed_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(d->speed_label, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(d->speed_label, LV_ALIGN_CENTER, 0, -20);

    d->unit_label = lv_label_create(scr);
    lv_label_set_text(d->unit_label, "km/h");
    lv_obj_set_style_text_font(d->unit_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(d->unit_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align_to(d->unit_label, d->speed_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    /* Bottom info cards: trip distance / moving time (GPS driven) */
    d->dist_label = lv_label_create(scr);
    lv_label_set_text(d->dist_label, "0.00 km");
    lv_obj_set_style_text_font(d->dist_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d->dist_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(d->dist_label, LV_ALIGN_BOTTOM_LEFT, 16, -16);

    d->time_label = lv_label_create(scr);
    lv_label_set_text(d->time_label, "00:00");
    lv_obj_set_style_text_font(d->time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d->time_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(d->time_label, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    /* Hint */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "HOLD OK:Back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -44);
}

/*
 * Free the private data allocated in dialplate_create().
 */
static void dialplate_destroy(page_t *self)
{
    free(self->user);
    self->user = NULL;
}

/*
 * Key handling: LEFT/BACK pop back to the drawer; a held ENTER pops via
 * the page manager's default back gesture, so the page only reacts to
 * raw arrow keys. The device has no BACK key, but the sim (ESC) does.
 */
static void dialplate_on_key(page_t *self, const key_event_t *ev)
{
    (void)self;
    if (!ev->is_press)
        return;
    switch (ev->id) {
    case KEY_BTN_LEFT:
    case KEY_BTN_BACK:
        page_manager_pop();
        break;
    default:
        break;
    }
}

page_t page_dialplate = {
    .name = "dialplate",
    .ops  = &(page_ops_t){
        .create  = dialplate_create,
        .destroy = dialplate_destroy,
        .on_show = dialplate_on_show,
        .on_hide = dialplate_on_hide,
        .on_key  = dialplate_on_key,
    },
};
