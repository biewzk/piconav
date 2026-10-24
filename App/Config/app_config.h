#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Screen resolution (ST7789V 2.4", portrait) */
#define APP_SCR_HOR    240
#define APP_SCR_VER    320

/* Directory scanned by the map picker for .map files (data partition).
 * Override at runtime with the MF_MAP_DIR environment variable. */
#define APP_MAP_DIR_DEFAULT "/data/maps"

/* Display buffer: full-screen RGB565 */
#define APP_DISP_BUF   (APP_SCR_HOR * APP_SCR_VER)

/* Page transition animation duration (ms) — X-TRACK uses 500ms */
#define PAGE_ANIM_TIME 500

/* Dial page big-digit font sizes */
#define DIAL_SPEED_FONT 48
#define DIAL_SUB_FONT   16

#endif /* APP_CONFIG_H */
