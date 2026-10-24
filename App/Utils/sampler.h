#ifndef SAMPLER_H
#define SAMPLER_H

#include <stdint.h>

#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * sampler - generic data-pool producer on top of one lv_timer.
 *
 * Producers that feed a consumer-readable pool usually want two
 * different cadences: a fast poll (e.g. draining a peripheral RX buffer
 * before it overflows) and a slower sample refresh (the rate at which
 * consumers should see new data and wake up). One sampler runs a single
 * lv_timer at the poll rate; every tick it calls the poll callback and,
 * when that reports fresh data and the sample interval has elapsed,
 * calls the publish callback (move the data into the pool, notify
 * consumers). Consumers keep pulling from the pool at their own pace.
 *
 * Not thread-safe: create and drive it from the LVGL task.
 */

typedef struct sampler sampler_t;

/* poll: collect new data; return 1 when a fresh sample is available.
 * publish: move the fresh sample into the pool and wake consumers. */
typedef int  (*sampler_poll_fn)(void *user);
typedef void (*sampler_publish_fn)(void *user);

/* Create a sampler. poll_ms is the lv_timer period; sample_ms is the
 * publish interval (0 = publish on every fresh poll). */
sampler_t *sampler_create(uint32_t poll_ms, uint32_t sample_ms,
                          sampler_poll_fn poll, sampler_publish_fn publish,
                          void *user);

/* Change the publish interval; effective from the next poll tick. */
void sampler_set_sample_ms(sampler_t *s, uint32_t sample_ms);

/* Poll + publish immediately, ignoring the interval (page entries,
 * demand switches). When the poll finds no fresh data the force stays
 * armed and is consumed by the first successful later poll. No-op
 * while paused. */
void sampler_force_sample(sampler_t *s);

/* Suspend/resume polling entirely (feature switched off, nothing to
 * drain). While paused the underlying timer keeps running but the
 * callbacks are not invoked. */
void sampler_pause(sampler_t *s);
void sampler_resume(sampler_t *s);

/* Stop the underlying timer and free the sampler. */
void sampler_destroy(sampler_t *s);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLER_H */
