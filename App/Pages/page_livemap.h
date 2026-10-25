#ifndef PAGE_LIVEMAP_H
#define PAGE_LIVEMAP_H

#include "lvgl.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Map panel of the home carousel (like the GPS receiver panel, not an
 * app-drawer entry). Thin shell around the map_view widget: status bar,
 * 1 s GPS refresh while the panel is the active carousel panel, and the
 * panel-switch glue.
 *
 * Key model on the map panel (all other keys go to the map):
 *   double-tap LEFT/RIGHT  -> prev/next carousel panel
 *   hold OK at min zoom    -> "close": nav(0), the home jumps to the
 *                             watch face
 */

/*
 * Build the panel into `parent` (a full-screen carousel panel) and
 * register `nav` as the panel-switch callback: nav(-1)/nav(+1) = prev/
 * next panel, nav(0) = leave the map to the watch face.
 */
void page_livemap_panel_create(lv_obj_t *parent, void (*nav)(int dir));

/*
 * Feed a key event while the map panel is active. Returns 1 when the
 * panel consumed it (map open) and 0 when the map is unusable - the
 * caller then falls back to plain carousel navigation.
 */
int page_livemap_panel_key(const key_event_t *ev);

/* Activate/deactivate the panel: GPS demand, follow + recenter on
 * activation, pause + release on deactivation. */
void page_livemap_panel_set_active(int on);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_LIVEMAP_H */
