#include "gps_service.h"
#include "sampler.h"
#include "config_api.h"
#include "log.h"

#include <string.h>

/*
 * gps_service.c - see gps_service.h for the design. The pool replaces
 * the snapshot App/app.c used to refresh from its main loop. The
 * producer side is a sampler (fast drain poll, throttled publish);
 * external gps.* config edits arrive via MSG_CONFIG_CHANGED from the
 * config watcher.
 */

#define GPS_POLL_MS         1000  /* raw UART drain cadence (see header) */
#define GPS_MIN_INTERVAL_S  1
#define GPS_MAX_INTERVAL_S  60
#define GPS_STALE_FLOOR_MS  10000
#define GPS_DEFAULT_FREQ_S  5

#define GPS_KEY_MODE    "gps.mode"
#define GPS_KEY_FREQ    "gps.freq"
/* Live state, '@'-prefixed: visible to every reader while staged, but
 * config_save()/configctl commit strip it before touching flash (see
 * config_api.h). The unprefixed name was used by rootfs 1.9.8 only. */
#define GPS_KEY_LIVE    "@gps.interval"
#define GPS_KEY_LIVE_OLD "gps.interval"

static msg_center_t *s_mc;
static sampler_t    *s_sampler;

static gps_data_t s_pool;      /* data pool consumers read */
static gps_data_t s_latest;    /* freshest parsed receiver state */

static int      s_mode     = GPS_MODE_AUTO;
static uint32_t s_freq_s   = GPS_DEFAULT_FREQ_S;  /* manual-mode interval */
static int      s_demand_n;                       /* fast-demand consumers */
static uint32_t s_interval_ms;                    /* effective publish gap */

static const char *mode_name(void);

static void actual_interval_publish(void);

static const char *mode_name(void)
{
    switch (s_mode) {
    case GPS_MODE_MANUAL: return "manual";
    case GPS_MODE_OFF:    return "off";
    default:              return "auto";
    }
}

/* Recompute the effective publish interval and log transitions. */
static void interval_update(const char *reason)
{
    uint32_t old = s_interval_ms;
    uint32_t sec;

    switch (s_mode) {
    case GPS_MODE_MANUAL: sec = s_freq_s; break;
    case GPS_MODE_OFF:    sec = 0;        break;
    default:
        sec = s_demand_n > 0 ? 1 : GPS_DEMAND_DEFAULT_SEC;
        break;
    }

    if (sec != 0) {
        if (sec < GPS_MIN_INTERVAL_S)
            sec = GPS_MIN_INTERVAL_S;
        if (sec > GPS_MAX_INTERVAL_S)
            sec = GPS_MAX_INTERVAL_S;
        s_interval_ms = sec * 1000;
    } else {
        s_interval_ms = 0;
    }

    if (s_interval_ms != old) {
        LOG_I("gps service: %s, interval %u ms (%s)",
              mode_name(), (unsigned)s_interval_ms, reason);
        actual_interval_publish();
    }
}

/* Push the current mode/interval into the sampler: OFF suspends the
 * UART drain entirely, everything else resumes it at the interval. */
static void sampler_apply(void)
{
    if (!s_sampler)
        return;

    if (s_mode == GPS_MODE_OFF) {
        sampler_pause(s_sampler);
        return;
    }
    sampler_resume(s_sampler);
    sampler_set_sample_ms(s_sampler, s_interval_ms);
}

/*
 * Publish the actual sampling rate as @gps.interval (seconds, 0 = off).
 * Volatile keys are pushed to the driver staging buffer by config_set_*
 * itself, so every reader of /dev/config sees the rate AUTO mode is
 * really running at as pages come and go - and no commit can persist it
 * (config_save/configctl commit strip '@' keys). gps.freq is never
 * touched here: it stays the manual-mode user setting.
 */
static void actual_interval_publish(void)
{
    int sec = s_mode == GPS_MODE_OFF ? 0 : (int)(s_interval_ms / 1000);

    if (config_is_dirty())
        return;   /* unsaved edits in flight; do not stage over them */

    /* Fold in an external edit that landed since our last look so a
     * concurrent configctl change is not pushed away. */
    if (config_changed() == 1)
        config_init();

    config_set_int(GPS_KEY_LIVE, sec);
    LOG_D("gps service: published @gps.interval=%us", sec);
}

/* Poll: drain everything the receiver pushed since the last tick (the
 * tty input buffer only holds ~4 KB, ~4 s at 9600 baud). */
static int gps_poll(void *user)
{
    gps_data_t tmp;
    (void)user;

    if (!hal_gps_read(&tmp))
        return 0;   /* receiver silent: keep the pool as it is */
    s_latest = tmp;
    while (hal_gps_read(&tmp))
        s_latest = tmp;
    return 1;
}

/* Publish: move the freshest parsed state into the pool and wake the
 * MSG_GPS consumers. */
static void gps_publish(void *user)
{
    (void)user;

    s_pool = s_latest;
    if (s_pool.stamp_ms == 0)
        s_pool.stamp_ms = hal_tick_get();
    msg_center_publish(s_mc, MSG_GPS, &s_pool);
    LOG_D("gps service: pool refresh @%ums", (unsigned)hal_tick_get());
}

static void on_config_changed(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    (void)data;
    (void)user;
    gps_service_config_reload();
}

void gps_service_config_reload(void)
{
    int mode = s_mode;
    int freq = (int)s_freq_s;

    config_init();   /* re-read the last committed block from the driver */
    config_get_int(GPS_KEY_MODE, &mode, GPS_MODE_AUTO);
    config_get_int(GPS_KEY_FREQ, &freq, (int)GPS_DEFAULT_FREQ_S);
    if (freq < GPS_MIN_INTERVAL_S)
        freq = GPS_MIN_INTERVAL_S;
    if (freq > GPS_MAX_INTERVAL_S)
        freq = GPS_MAX_INTERVAL_S;

    if (mode != s_mode || (uint32_t)freq != s_freq_s) {
        s_mode   = mode;
        s_freq_s = (uint32_t)freq;
        interval_update("config");
        sampler_apply();
        sampler_force_sample(s_sampler);
    }

    /* An external commit strips volatile keys from flash; republish the
     * live rate so readers keep seeing it (~1 s after the commit). */
    actual_interval_publish();
}

void gps_service_demand_add(void)
{
    s_demand_n++;
    if (s_demand_n == 1) {
        interval_update("demand");
        sampler_apply();
        /* Entering a fast page must not wait out the slow interval. */
        sampler_force_sample(s_sampler);
    }
}

void gps_service_demand_release(void)
{
    if (s_demand_n <= 0) {
        LOG_W("gps service: demand release without a matching add");
        return;
    }
    s_demand_n--;
    if (s_demand_n == 0) {
        interval_update("demand");
        sampler_apply();
    }
}

uint32_t gps_service_interval_ms(void)
{
    return s_interval_ms;
}

uint32_t gps_service_stale_ms(void)
{
    uint32_t iv = s_interval_ms ? s_interval_ms : GPS_DEFAULT_FREQ_S * 1000;
    uint32_t t  = iv * 3;
    return t > GPS_STALE_FLOOR_MS ? t : GPS_STALE_FLOOR_MS;
}

int gps_service_is_fresh(const gps_data_t *g)
{
    return g && g->stamp_ms != 0 &&
           hal_tick_get() - g->stamp_ms <= gps_service_stale_ms();
}

const gps_data_t *gps_service_data(void)
{
    return &s_pool;
}

void gps_service_init(msg_center_t *mc)
{
    int mode = GPS_MODE_AUTO;
    int freq = (int)GPS_DEFAULT_FREQ_S;

    s_mc = mc;

    config_init();
    config_get_int(GPS_KEY_MODE, &mode, GPS_MODE_AUTO);
    config_get_int(GPS_KEY_FREQ, &freq, (int)GPS_DEFAULT_FREQ_S);
    if (freq < GPS_MIN_INTERVAL_S)
        freq = GPS_MIN_INTERVAL_S;
    if (freq > GPS_MAX_INTERVAL_S)
        freq = GPS_MAX_INTERVAL_S;
    s_mode   = mode;
    s_freq_s = (uint32_t)freq;

    /* rootfs 1.9.8 staged the live rate unprefixed; drop the leftover */
    {
        int legacy = -1;

        config_get_int(GPS_KEY_LIVE_OLD, &legacy, -1);
        if (legacy != -1)
            config_unset(GPS_KEY_LIVE_OLD);
    }

    interval_update("init");
    s_sampler = sampler_create(GPS_POLL_MS, s_interval_ms,
                               gps_poll, gps_publish, NULL);
    sampler_apply();
    actual_interval_publish();   /* publish the boot-time actual rate */

    msg_center_subscribe(mc, MSG_CONFIG_CHANGED, on_config_changed, NULL);

    LOG_I("gps service: started (%s, interval %u ms)",
          mode_name(), (unsigned)s_interval_ms);
}
