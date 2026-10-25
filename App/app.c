#include "app.h"
#include "Utils/page_manager.h"
#include "Utils/gps_service.h"
#include "Utils/trip_store.h"
#include "Utils/config_watcher.h"
#include "Utils/log.h"
#include "Pages/pages_builtin.h"
#include "../HAL/hal.h"

static msg_center_t *s_mc;

/* Return the global message center created by app_init(). */
msg_center_t *app_msg_center(void)
{
    return s_mc;
}

/* Id of the runtime "map.selected" topic, registered on first use. */
msg_topic_t app_topic_map_selected(void)
{
    static msg_topic_t t = MSG_TOPIC_INVALID;

    if (t == MSG_TOPIC_INVALID && s_mc)
        t = msg_topic_register(s_mc, "map.selected");
    return t;
}

/* Return the GPS data pool (refreshed by gps_service at the configured
 * sample interval, not per NMEA sentence). */
const gps_data_t *app_gps(void)
{
    return gps_service_data();
}

/*
 * Initialize the application framework:
 *  1. create the message center
 *  2. init the page manager (subscribes to MSG_KEY)
 *  3. init the HAL keys and GPS
 *  4. start the services (sampler, trip store, config watcher)
 *  5. register the built-in pages/apps, then open the home page
 */
void app_init(void)
{
    s_mc = msg_center_create();
    LOG_D("msg_center created");

    page_manager_init(s_mc);
    LOG_D("page_manager init done (subscribed to MSG_KEY)");

    if (hal_key_init() != 0)
        LOG_W("hal_key_init failed, keys unavailable");

    if (hal_gps_init() != 0)
        LOG_W("hal_gps_init failed, GPS unavailable");

    gps_service_init(s_mc);
    trip_store_init(s_mc);
    config_watcher_init(s_mc);

    pages_builtin_register();
    page_manager_open("home", NULL);
    LOG_I("pushed home page (depth=%d)", page_manager_depth());
}

/*
 * Main loop: drain key events into the message center, run the LVGL
 * timer handler (which also drives the gps_service sampler) and sleep
 * briefly, until hal_should_quit().
 */
void app_run(void)
{
    while (!hal_should_quit()) {
        key_event_t ev;
        while (hal_key_read(&ev)) {
            LOG_D("key event: id=%d press=%d", ev.id, ev.is_press);
            msg_center_publish(s_mc, MSG_KEY, &ev);
        }

        lv_timer_handler();
        hal_delay_ms(5);
    }
}