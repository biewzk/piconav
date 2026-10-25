#ifndef TRIP_STORE_H
#define TRIP_STORE_H

#include "msg_center.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * trip_store - session trip counters integrated from the GPS pool
 *
 * Subscribes to MSG_GPS once at app startup and integrates speed into
 * distance / moving time for as long as the sampler publishes. The
 * counters survive page push/pop (the dial page only displays them);
 * they reset on reboot or an explicit trip_store_reset().
 */

typedef struct {
    double   trip_km;    /* distance integrated from GPS speed */
    uint32_t move_ms;    /* time with speed >= 1 km/h */
} trip_stats_t;

/* Subscribe to MSG_GPS and start integrating. */
void trip_store_init(msg_center_t *mc);

/* Current counters (read anytime). */
const trip_stats_t *trip_store_get(void);

/* Zero the counters and the sample-gap tracker. */
void trip_store_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* TRIP_STORE_H */
