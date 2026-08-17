#include "app.h"
#include "Utils/page_manager.h"
#include "Utils/log.h"
#include "Pages/page_startup.h"
#include "../HAL/hal.h"

static msg_center_t *s_mc;

msg_center_t *app_msg_center(void)
{
    return s_mc;
}

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

    page_manager_push(&page_startup);
    LOG_I("pushed startup page (depth=%d)", page_manager_depth());
}

void app_run(void)
{
    while (!hal_should_quit()) {
        key_event_t ev;
        while (hal_key_read(&ev)) {
            LOG_D("key event: id=%d press=%d", ev.id, ev.is_press);
            msg_center_publish(s_mc, MSG_KEY, &ev);
        }

        gps_data_t g;
        if (hal_gps_read(&g)) {
            LOG_D("gps update: fix=%d lat=%.5f lon=%.5f speed=%.1f km/h",
                  g.fix, g.lat, g.lon, g.speed_kmh);
            msg_center_publish(s_mc, MSG_GPS, &g);
        }

        lv_timer_handler();
        hal_delay_ms(5);
    }
}