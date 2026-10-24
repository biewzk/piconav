#include "config_watcher.h"
#include "config_api.h"
#include "log.h"

#include "lvgl/lvgl.h"

#define WATCH_PERIOD_MS 1000

static msg_center_t *s_mc;
static lv_timer_t   *s_timer;

static void watcher_tick(lv_timer_t *timer)
{
    (void)timer;

    if (config_is_dirty())
        return;   /* this app holds unsaved edits; do not clobber them */

    if (config_changed() != 1)
        return;

    config_init();   /* re-read the changed payload into the shared root */
    LOG_I("config watcher: external config change, reloaded");
    msg_center_publish(s_mc, MSG_CONFIG_CHANGED, NULL);
}

void config_watcher_init(msg_center_t *mc)
{
    s_mc = mc;
    s_timer = lv_timer_create(watcher_tick, WATCH_PERIOD_MS, NULL);
}
