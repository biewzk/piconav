#ifndef HAL_H
#define HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 5 键 + 返回(仅模拟器/预留)
 * 命名加 BTN_ 前缀: 避免与 linux/input-event-codes.h 的 KEY_* 宏冲突 */
typedef enum {
    KEY_BTN_UP = 0,
    KEY_BTN_DOWN,
    KEY_BTN_LEFT,
    KEY_BTN_RIGHT,
    KEY_BTN_ENTER,
    KEY_BTN_BACK,
    KEY_NUM
} key_id_t;

typedef struct {
    key_id_t id;
    uint8_t  is_press;   /* 1=按下, 0=抬起 */
} key_event_t;

typedef struct {
    uint8_t  fix;        /* 0=无定位, 1=有定位 */
    double   lat;        /* 纬度 (deg) */
    double   lon;        /* 经度 (deg) */
    double   speed_kmh;  /* 速度 (km/h) */
    double   alt_m;      /* 海拔 (m) */
    uint8_t  sats;       /* 可见卫星数 */
} gps_data_t;

/* HAL 接口 (真机版与模拟器版各实现一份, 由 Makefile 选择编译) */
void hal_delay_ms(uint32_t ms);
uint32_t hal_tick_get(void);                 /* ms, 供 LV_TICK_CUSTOM */
int  hal_should_quit(void);                  /* 真机恒 0; 模拟器窗口关闭时返回 1 */

int  hal_key_init(void);
int  hal_key_read(key_event_t *ev);          /* 返回 1=有事件 */

int  hal_gps_init(void);
int  hal_gps_read(gps_data_t *g);            /* 返回 1=有新数据 */

#ifdef __cplusplus
}
#endif

#endif /* HAL_H */