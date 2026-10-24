#include "sampler.h"

#include "lvgl/lvgl.h"

#include <stdlib.h>

struct sampler {
    lv_timer_t *timer;
    uint32_t    sample_ms;   /* publish interval; 0 = every fresh poll */
    uint32_t    last_ms;     /* tick of the last publish */
    int         paused;
    int         forced;      /* publish on the next fresh poll, not the
                              * next interval boundary */
    sampler_poll_fn    poll;
    sampler_publish_fn publish;
    void       *user;
};

/* One producer step: poll, then publish when the interval allows. */
static void sampler_run(sampler_t *s)
{
    int fresh;

    if (s->paused)
        return;

    fresh = s->poll(s->user);
    if (!fresh)
        return;

    if (!s->forced && s->sample_ms != 0 &&
        hal_tick_get() - s->last_ms < s->sample_ms)
        return;

    s->last_ms = hal_tick_get();
    s->forced = 0;
    s->publish(s->user);
}

static void sampler_timer_cb(lv_timer_t *timer)
{
    sampler_run((sampler_t *)timer->user_data);
}

sampler_t *sampler_create(uint32_t poll_ms, uint32_t sample_ms,
                          sampler_poll_fn poll, sampler_publish_fn publish,
                          void *user)
{
    sampler_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->sample_ms = sample_ms;
    s->poll      = poll;
    s->publish   = publish;
    s->user      = user;
    s->timer     = lv_timer_create(sampler_timer_cb, poll_ms ? poll_ms : 1, s);
    return s;
}

void sampler_set_sample_ms(sampler_t *s, uint32_t sample_ms)
{
    if (s)
        s->sample_ms = sample_ms;
}

void sampler_force_sample(sampler_t *s)
{
    if (!s || s->paused)
        return;
    s->forced = 1;
    sampler_run(s);
}

void sampler_pause(sampler_t *s)
{
    if (s)
        s->paused = 1;
}

void sampler_resume(sampler_t *s)
{
    if (s)
        s->paused = 0;
}

void sampler_destroy(sampler_t *s)
{
    if (!s)
        return;
    if (s->timer)
        lv_timer_del(s->timer);
    free(s);
}
