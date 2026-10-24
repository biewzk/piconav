#ifndef PAGE_GPS_H
#define PAGE_GPS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPS receiver status panel: sky plot + SNR bars + position details,
 * 240x320 including the status bar. Shown as the third home carousel
 * panel (its own screen; deliberately NOT an entry in the app drawer). */
void page_gps_panel_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_GPS_H */
