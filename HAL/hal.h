#ifndef HAL_H
#define HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 5 keys + back (simulator only / reserved).
 * BTN_ prefix avoids clashes with the KEY_* macros in
 * linux/input-event-codes.h. */
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
    uint8_t  is_press;   /* 1 = pressed, 0 = released */
} key_event_t;

/* HAL interface (real-device and simulator each provide an
 * implementation; the Makefile selects which one to compile). */
void hal_delay_ms(uint32_t ms);
uint32_t hal_tick_get(void);                 /* ms, for LV_TICK_CUSTOM */
int  hal_should_quit(void);                  /* always 0 on device; 1 when sim window closes */

int  hal_key_init(void);
int  hal_key_read(key_event_t *ev);          /* returns 1 = event available */

#ifdef __cplusplus
}
#endif

#endif /* HAL_H */