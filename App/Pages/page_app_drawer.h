#ifndef PAGE_APP_DRAWER_H
#define PAGE_APP_DRAWER_H

#include "lvgl.h"

/* Build the app drawer UI on the given parent object. */
void app_drawer_create(lv_obj_t *parent);

/* Update the selected item highlight. */
void app_drawer_set_sel(int sel);

/* Get the current selection index. */
int app_drawer_get_sel(void);

#endif /* PAGE_APP_DRAWER_H */
