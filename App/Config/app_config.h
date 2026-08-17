#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 屏幕分辨率 (ST7789V 2.4", 纵向) */
#define APP_SCR_HOR    240
#define APP_SCR_VER    320

/* 显示缓冲: 整屏 RGB565 */
#define APP_DISP_BUF   (APP_SCR_HOR * APP_SCR_VER)

/* 页面切换动画时长 (ms), 按 fbtft ~30fps 调优 */
#define PAGE_ANIM_TIME 250

/* 表盘大字字号 */
#define DIAL_SPEED_FONT 48
#define DIAL_SUB_FONT   16

#endif /* APP_CONFIG_H */
