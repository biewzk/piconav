#include "page_livemap.h"
#include "../Utils/page_manager.h"
#include "../Utils/gps_service.h"
#include "../Utils/map_view.h"
#include "../app.h"

#include <stdlib.h>

/*
 * Thin shell around the map_view widget (App/Utils/map_view.h): full-
 * screen map + hint label, GPS demand wiring and the "close me" -> pop
 * glue. All rendering/pan/zoom/follow lives in the widget so plugins
 * can embed a map without touching this page.
 */
typedef struct {
    map_view_t *mv;
    lv_obj_t   *hint;
    msg_sub_t  *gps_sub;
} livemap_priv_t;

/* MSG_GPS handler: feed the fix to the widget (recenter while following). */
static void livemap_on_gps(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    page_t *self = user;
    livemap_priv_t *d = self ? self->user : NULL;

    if (d && d->mv)
        map_view_set_gps(d->mv, data);
}

/* Build the page: map widget + bottom-right operation hint. */
static void livemap_create(page_t *self)
{
    livemap_priv_t *d = calloc(1, sizeof(livemap_priv_t));
    self->user = d;

    d->mv = map_view_create(self->scr, self->param);

    d->hint = lv_label_create(self->scr);
    lv_label_set_text(d->hint, "OK:+ HOLD:-");
    lv_obj_set_style_text_font(d->hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(d->hint, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(d->hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d->hint, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(d->hint, 2, 0);
    lv_obj_set_style_radius(d->hint, 3, 0);
    lv_obj_align(d->hint, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
}

/* Free the widget and the private data. */
static void livemap_destroy(page_t *self)
{
    livemap_priv_t *d = self->user;

    if (d) {
        map_view_delete(d->mv);
        d->mv = NULL;
    }
    free(self->user);
    self->user = NULL;
}

/* Stop auto-pan when the page leaves the foreground. */
static void livemap_on_hide(page_t *self)
{
    livemap_priv_t *d = self->user;

    map_view_pause(d->mv);
    gps_service_demand_release();
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

    map_view_set_follow(d->mv, 1);
    gps_service_demand_add();
    if (!d->gps_sub)
        d->gps_sub = msg_center_subscribe(app_msg_center(), MSG_GPS,
                                          livemap_on_gps, self);

    map_view_recenter(d->mv, app_gps());
}

/*
 * Key handling is the widget's; it reports 1 when the gesture space is
 * exhausted (BACK, or ENTER held at the min zoom) and the page pops.
 * This page must see raw ENTER long-presses for zoom-out, so it opts
 * out of the page manager's default back gesture.
 */
static void livemap_on_key(page_t *self, const key_event_t *ev)
{
    livemap_priv_t *d = self->user;

    if (map_view_on_key(d->mv, ev))
        page_manager_pop();
}

page_t page_livemap = {
    .name  = "livemap",
    .flags = PAGE_FLAG_NO_AUTO_BACK,
    .ops   = &(page_ops_t){
        .create  = livemap_create,
        .destroy = livemap_destroy,
        .on_show = livemap_on_show,
        .on_hide = livemap_on_hide,
        .on_key  = livemap_on_key,
    },
};
