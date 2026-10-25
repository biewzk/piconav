#ifndef APP_ICONS_H
#define APP_ICONS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Drawer app icons (40x40 rounded accent tile + white glyph), generated
 * by tools/gen_app_icons.py - do not edit app_icons.c by hand.
 * Plugins reference these by name: the host binary exports app_*.
 */

extern const lv_img_dsc_t app_icon_trip_img;       /* trip dialplate  */
extern const lv_img_dsc_t app_icon_gps_img;        /* GPS receiver    */
extern const lv_img_dsc_t app_icon_map_img;        /* live map        */
extern const lv_img_dsc_t app_icon_settings_img;   /* settings        */
extern const lv_img_dsc_t app_icon_stopwatch_img;  /* stopwatch       */

#ifdef __cplusplus
}
#endif

#endif /* APP_ICONS_H */
