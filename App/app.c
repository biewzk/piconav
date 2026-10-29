#include "app.h"
#include "Utils/page_manager.h"
#include "Utils/gps_service.h"
#include "Utils/trip_store.h"
#include "Utils/config_watcher.h"
#include "Utils/plugin_manager.h"
#include "Utils/app_launcher.h"
#include "Utils/app_services.h"
#include "Utils/display_guard.h"
#include "Utils/log.h"
#include "Pages/pages_builtin.h"
#include "Pages/page_app_drawer.h"
#include "../HAL/hal.h"

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
    msg_center_t *mc = msg_center_create();
    app_services_bind(mc);
    LOG_D("msg_center created");

    page_manager_init(mc);
    LOG_D("page_manager init done (subscribed to MSG_KEY)");

    if (hal_key_init() != 0)
        LOG_W("hal_key_init failed, keys unavailable");

    if (hal_gps_init() != 0)
        LOG_W("hal_gps_init failed, GPS unavailable");

    gps_service_init(mc);
    trip_store_init(mc);
    config_watcher_init(mc);

    pages_builtin_register();
    plugin_manager_init();
    page_manager_open("home", NULL);
    LOG_I("pushed home page (depth=%d)", page_manager_depth());
}

/*
 * Main loop: drain key events into the message center, run the LVGL
 * timer handler (which also drives the gps_service sampler) and sleep
 * briefly, until hal_should_quit().
 *
 * A drawer entry that stands for a standalone app executable sets a
 * pending launch request from its key handler; the request is serviced
 * here, outside the LVGL callback, so the GUI can suspend cleanly while
 * the child process owns the screen.
 */
void app_run(void)
{
    int foreign_owner = 0;
    /* hal_tick_get() is an absolute ms clock: starting the deadline at
     * 0 would make (now - next) overflow int32 and never fire. */
    uint32_t next_guard_ms = hal_tick_get();

    while (!hal_should_quit()) {
        if (app_launcher_pending()) {
            app_launcher_run();
            app_drawer_refresh();
            continue;
        }

        /*
         * Single-front-owner policy: if another process opened /dev/fb0
         * (an app started from a serial console, a leftover child, ...)
         * the GUI yields - no rendering, no key reads, UART released -
         * instead of fighting it for the same framebuffer. It resumes
         * as soon as that process closes the device.
         */
        /* 2 Hz is plenty for a human-scale screen hand-off and keeps
         * the /proc scan off the hot path. */
        if ((int32_t)(hal_tick_get() - next_guard_ms) >= 0) {
            int owner;

            next_guard_ms = hal_tick_get() + 500;
            owner = display_guard_owner_pid();
            if (owner != foreign_owner) {
                if (owner) {
                    LOG_I("display: owned by pid %d, GUI suspended", owner);
                    app_launcher_suspend_hw();
                } else {
                    LOG_I("display: released, GUI resumed");
                    app_launcher_resume_hw();
                }
                foreign_owner = owner;
            }
        }
        if (foreign_owner) {
            hal_delay_ms(200);
            continue;
        }

        key_event_t ev;
        while (hal_key_read(&ev)) {
            LOG_D("key event: id=%d press=%d", ev.id, ev.is_press);
            msg_center_publish(app_msg_center(), MSG_KEY, &ev);
        }

        lv_timer_handler();
        hal_delay_ms(5);
    }
}
