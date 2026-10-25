#ifndef MAP_VIEW_H
#define MAP_VIEW_H

#include "lvgl.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * map_view - reusable offline Mapsforge view widget
 *
 * A self-contained LVGL widget that opens a Mapsforge .map file, renders
 * the viewport onto a full-parent canvas (buffer allocated on create,
 * freed on delete), supports arrow-key panning with hold-to-repeat,
 * ENTER zoom in / held-ENTER zoom out, and recenters on a GPS fix while
 * following. The dial-style diagnostics overlay (zoom/object count/
 * render time) is part of the widget.
 *
 * The built-in "livemap" page is a thin shell around this widget; plugins
 * embed it the same way. One instance at a time is the practical limit
 * on this device (the canvas alone is APP_SCR_HOR*APP_SCR_VER RGB565).
 */

typedef struct map_view map_view_t;

/*
 * Create the map view filling `parent`. map_path may be NULL/empty: the
 * MF_MAP env override wins, then the compiled-in dev fallback. A bad or
 * missing file is NOT fatal - the widget shows the error in its info
 * label and ignores keys; NULL is only returned on OOM.
 */
map_view_t *map_view_create(lv_obj_t *parent, const char *map_path);

/* Free the map, canvas buffer and timers. The LVGL objects die with the
 * parent screen; call this from the owning page's destroy. */
void map_view_delete(map_view_t *mv);

/*
 * Route a key event (raw semantics, long-press flags included).
 * Returns 1 when the widget exhausted its gesture space - BACK pressed
 * or ENTER held - at the map's minimum zoom and the owner should close.
 */
int map_view_on_key(map_view_t *mv, const key_event_t *ev);

/* Stop pan/zoom repeat timers (owner page hid; keeps rendering off). */
void map_view_pause(map_view_t *mv);

/* Feed a GPS fix: recenters while following (dead-zone + rate limited -
 * a full frame is not cheap on this SoC). Safe with fix == 0. */
void map_view_set_gps(map_view_t *mv, const gps_data_t *g);

/* Force-recenter on a fix right now (owner page (re)appeared). */
void map_view_recenter(map_view_t *mv, const gps_data_t *g);

/* 1 = keep the fix centered (default); manual panning turns it off. */
void map_view_set_follow(map_view_t *mv, int follow);

#ifdef __cplusplus
}
#endif

#endif /* MAP_VIEW_H */
