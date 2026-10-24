#ifndef HAL_H
#define HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 5 keys (up/down/left/right/enter) + back (simulator only / reserved).
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

/* ENTER is delivered as gestures by the page manager (see page_manager.c):
 * the raw press is withheld; pages see a short click {is_press=1} on
 * release, a long press {is_press=1, is_long=1} at the threshold, and
 * {is_press=0, is_long=1} when a long press ends. Arrows and BACK keep
 * raw press/release semantics. HALs always set is_long = 0. */
typedef struct {
    key_id_t id;
    uint8_t  is_press;   /* 1 = pressed, 0 = released */
    uint8_t  is_long;    /* 1 = long-press gesture (ENTER only) */
} key_event_t;

/* GPS fix snapshot, published on MSG_GPS and cached by App/app.c.
 * lat/lon are signed degrees (+N/+E). fix = 0 means the receiver has no
 * position solution yet; the remaining fields then hold the last known
 * values (speed is 0). */
typedef struct {
    uint8_t  fix;         /* 1 = valid position fix */
    uint8_t  sats;        /* satellites used in the solution */
    float    hdop;
    double   lat;
    double   lon;
    float    alt_m;       /* altitude above mean sea level */
    float    speed_kmh;   /* ground speed */
    float    course_deg;  /* course over ground */
    uint32_t utc;         /* UTC seconds since midnight, 0xFFFFFFFF = unknown */
    uint32_t stamp_ms;    /* hal_tick_get() when this snapshot was parsed */
} gps_data_t;

/* HAL interface (real-device and simulator each provide an
 * implementation; the Makefile selects which one to compile). */
void hal_delay_ms(uint32_t ms);
uint32_t hal_tick_get(void);                 /* ms, for LV_TICK_CUSTOM */
int  hal_should_quit(void);                  /* always 0 on device; 1 when sim window closes */

int  hal_key_init(void);
int  hal_key_read(key_event_t *ev);          /* returns 1 = event available */

/* GPS: device reads NMEA from the UART, simulator replays a .nmea file.
 * hal_gps_init() never blocks and never aborts the app on failure (the
 * device just keeps retrying); hal_gps_read() returns 1 when a fresh
 * snapshot is available, 0 when nothing changed. */
int  hal_gps_init(void);
int  hal_gps_read(gps_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HAL_H */