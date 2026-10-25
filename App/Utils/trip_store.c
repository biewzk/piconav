#include "trip_store.h"

#include <string.h>

/*
 * Integration policy (moved here from the dial page): only trust gaps
 * up to 2 s between consecutive samples - after a hidden period the
 * first fresh sample just re-seeds the gap tracker instead of adding a
 * huge dt. Moving time only accumulates at riding speed.
 */
#define TRIP_GAP_MAX_MS 2000
#define TRIP_MOVE_KMH   1.0f

static trip_stats_t s_trip;
static uint32_t s_last_stamp;   /* 0 = no previous sample yet */

static void trip_on_gps(msg_topic_t topic, const void *data, void *user)
{
    const gps_data_t *g = data;
    (void)topic;
    (void)user;

    if (!g)
        return;

    if (s_last_stamp && g->stamp_ms != s_last_stamp && g->fix) {
        uint32_t dt_ms = g->stamp_ms - s_last_stamp;
        if (dt_ms <= TRIP_GAP_MAX_MS) {
            s_trip.trip_km += (double)g->speed_kmh * (double)dt_ms / 3600000.0;
            if (g->speed_kmh >= TRIP_MOVE_KMH)
                s_trip.move_ms += dt_ms;
        }
    }
    s_last_stamp = g->stamp_ms;
}

void trip_store_init(msg_center_t *mc)
{
    msg_center_subscribe(mc, MSG_GPS, trip_on_gps, NULL);
}

const trip_stats_t *trip_store_get(void)
{
    return &s_trip;
}

void trip_store_reset(void)
{
    memset(&s_trip, 0, sizeof(s_trip));
    s_last_stamp = 0;
}
