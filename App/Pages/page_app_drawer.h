#ifndef PAGE_APP_DRAWER_H
#define PAGE_APP_DRAWER_H

#include "lvgl.h"

/* Build the app drawer UI on the given parent object. */
void app_drawer_create(lv_obj_t *parent);

/* Mark the drawer panel visible/hidden (gates the 1 s live-data
 * refresh and re-asserts focus + scroll position on show). */
void app_drawer_set_visible(int on);

/* Update the selected item (focus style + scroll-to-center). */
void app_drawer_set_sel(int sel);

/* Get the current selection index. */
int app_drawer_get_sel(void);

/* Rebuild the item list from the app registry (used after a hot app
 * install). No-op when the drawer has not been built yet. Keeps the
 * selection index clamped; refreshes the data columns. */
void app_drawer_refresh(void);

#endif /* PAGE_APP_DRAWER_H */
