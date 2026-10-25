#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phone-style global status bar, 240x22: local time on the left, GPS
 * signal bars and a power glyph on the right. The right edge keeps room
 * for future icons (bluetooth, recording, storage ...), added from the
 * right towards the GPS bars. Pages that show it start content at y =
 * STATUS_BAR_HEIGHT; the GPS receiver panel and the map panel are the
 * users. Each bar owns its refresh timer (several bars may coexist,
 * one per carousel panel). */
#define STATUS_BAR_HEIGHT 22

/* Build the bar into a parent panel (top-left corner). Only one bar may
 * exist at a time; it cleans up its timer automatically on delete. */
lv_obj_t *status_bar_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_BAR_H */
