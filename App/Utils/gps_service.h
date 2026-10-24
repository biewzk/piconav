#ifndef GPS_SERVICE_H
#define GPS_SERVICE_H

#include <stdint.h>

#include "msg_center.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * gps_service - software-timer GPS sampler feeding the shared data pool
 *
 * The sampler lv_timer drains the receiver at a fixed 1 s cadence (the
 * tty input buffer is 4 KB and a 9600 baud receiver pushes ~900 B/s, so
 * raw bytes must be collected at least once per second), but it only
 * refreshes the pool and publishes MSG_GPS at the configured sample
 * interval. Consumers wake at the activity rate, not the NMEA sentence
 * rate, and read the pool through app_gps() / gps_service_data().
 *
 * In AUTO mode the sample interval follows the visible UI ("demand"):
 * the GPS receiver panel, the map and the dial ask for 1 s while they
 * are on screen; anything else falls back to GPS_DEMAND_DEFAULT_SEC
 * (5 s, the hiking profile). MANUAL mode replaces the demand logic with
 * a user-configured interval; OFF stops UART access entirely (the pool
 * freezes and every consumer shows stale).
 *
 * Config keys (/dev/config JSON, shared with configctl/webctl):
 *   gps.mode  int  0 = auto (default), 1 = manual, 2 = off
 *   gps.freq  int  manual-mode sample interval in seconds, 1..60
 * External processes change them via "configctl set <key> <val>" +
 * "configctl commit"; the app picks changes up by re-reading the config
 * every 30 s inside the sampler (and on every in-app config save).
 */

/* gps.mode values */
#define GPS_MODE_AUTO    0
#define GPS_MODE_MANUAL  1
#define GPS_MODE_OFF     2

/* AUTO fallback when no visible consumer asked for faster samples */
#define GPS_DEMAND_DEFAULT_SEC 5

/* Create the sampler timer, load gps.* config, watch MSG_CONFIG_CHANGED. */
void gps_service_init(msg_center_t *mc);

/* Data pool: refreshed at the sample interval, not per NMEA sentence. */
const gps_data_t *gps_service_data(void);

/* Effective sample interval in ms (AUTO/MANUAL; 0 in OFF mode). */
uint32_t gps_service_interval_ms(void);

/* Staleness threshold consumers should compare against stamp_ms: three
 * sample intervals (floor 10 s), so slow sampling is never mistaken for
 * a dead receiver. */
uint32_t gps_service_stale_ms(void);

/* Visible-UI demand in seconds (AUTO mode only). Pages call it from
 * on_show with their preferred rate and from on_hide with 0 to fall
 * back to the default. A change takes effect immediately with a fresh
 * sample, so entering a fast page never waits out the old interval. */
void gps_service_demand(uint32_t sec);

/* Re-read gps.* from the driver and apply on change (also wired to
 * MSG_CONFIG_CHANGED; the sampler calls it every 30 s so configctl or
 * webctl edits apply without an app restart). */
void gps_service_config_reload(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_SERVICE_H */
