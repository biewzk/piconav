#ifndef MAP_VIEW_H
#define MAP_VIEW_H

#include "lvgl.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * map_view - reusable offline Mapsforge view widget with a GPS HUD
 *
 * A self-contained LVGL widget that opens a Mapsforge .map file, renders
 * the viewport onto a full-parent canvas (buffer allocated on create,
 * freed on delete) and draws the reviewed "plan A" HUD on top:
 *
 *   - position marker: green arrow rotated by GPS course while
 *     following, muted gray arrow at the last known position (with an
 *     accuracy ring) after manual panning, gray dot while searching;
 *   - top-right fix badge (3D/2D + satellite count, SEARCH when stale);
 *   - bottom-left speed chip, bottom-right scale bar, top-left N chip;
 *   - a "2xOK" pill while GPS follow is off (double-click OK recenters);
 *   - zoom/object-count diagnostics only when MF_DEBUG is set.
 *
 * The widget is fed with map_view_set_gps(); NULL means "no fresh fix"
 * (the caller applies the staleness policy). One instance at a time is
 * the practical limit on this device (the canvas alone is
 * APP_SCR_HOR*APP_SCR_VER RGB565).
 *
 * Keys: arrows pan (tap step, hold-to-repeat), ENTER clicks zoom in,
 * double-click ENTER recenters and resumes follow, held ENTER zooms out
 * and reports "close me" when already at the minimum zoom.
 */

typedef struct map_view map_view_t;

/*
 * Create the map view filling `parent`. map_path may be NULL/empty: the
 * MF_MAP env override wins, then the compiled-in dev fallback. A bad or
 * missing file is NOT fatal - the widget hides its HUD, shows the error
 * in the diagnostics label and ignores keys; NULL is only returned on
 * out-of-memory.
 */
map_view_t *map_view_create(lv_obj_t *parent, const char *map_path);

/* Free the map, canvas buffer and timers. The LVGL objects die with the
 * parent screen; call this from the owner's delete handler. */
void map_view_delete(map_view_t *mv);

/* 1 when a map file is open and rendering (keys are live). */
int map_view_is_ok(const map_view_t *mv);

/*
 * Route a key event (raw semantics, long-press flags included).
 * Returns 1 when the widget exhausted its gesture space - BACK pressed
 * or ENTER held at the map's minimum zoom - and the owner should close.
 */
int map_view_on_key(map_view_t *mv, const key_event_t *ev);

/* Stop pan/zoom repeat timers (owner hid; keeps rendering off). */
void map_view_pause(map_view_t *mv);

/*
 * Feed a GPS snapshot: NULL switches the HUD to the searching state.
 * With a fix, updates marker/badge/speed and - while following -
 * recenters (dead-zone + rate limited: a full frame is not cheap on
 * this SoC).
 */
void map_view_set_gps(map_view_t *mv, const gps_data_t *g);

/* Force-recenter on a fix right now (owner page (re)appeared). */
void map_view_recenter(map_view_t *mv, const gps_data_t *g);

/* 1 = keep the fix centered (default); manual panning turns it off. */
void map_view_set_follow(map_view_t *mv, int follow);

/*
 * Close and (re)open the map file, keeping the HUD state. The current
 * center is kept when it lies inside the new map's bbox, else the new
 * map's start position is used. Returns 0 on success.
 */
int map_view_open(map_view_t *mv, const char *map_path);

/* Cancel an ongoing pan/zoom gesture (e.g. before leaving the view). */
void map_view_break_gesture(map_view_t *mv);

#ifdef __cplusplus
}
#endif

#endif /* MAP_VIEW_H */
