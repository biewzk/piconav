#include "app.h"
#include "Utils/page_manager.h"
#include "Utils/log.h"
#include "Pages/page_home.h"
#include "../HAL/hal.h"

static msg_center_t *s_mc;

/* Return the global message center created by app_init(). */
msg_center_t *app_msg_center(void)
{
    return s_mc;
}

/*
 * Initialize the application framework:
 *  1. create the message center
 *  2. init the page manager (subscribes to MSG_KEY)
 *  3. init the HAL keys
 *  4. push the livemap page as the root page
 */
void app_init(void)
{
    s_mc = msg_center_create();
    LOG_D("msg_center created");

    page_manager_init(s_mc);
    LOG_D("page_manager init done (subscribed to MSG_KEY)");

    if (hal_key_init() != 0)
        LOG_W("hal_key_init failed, keys unavailable");

    page_manager_push(&page_home);
    LOG_I("pushed home page (depth=%d)", page_manager_depth());
}

/*
 * Main loop: drain key events into the message center, run the
 * LVGL timer handler and sleep briefly, until hal_should_quit().
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