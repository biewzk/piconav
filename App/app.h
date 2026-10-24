#ifndef APP_H
#define APP_H

#include "Utils/msg_center.h"
#include "../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global message center, created by app_init(). */
msg_center_t *app_msg_center(void);

/* GPS data pool (refreshed by gps_service at the configured sample
 * interval; consumers read it on_show to show data before the next
 * pool refresh). */
const gps_data_t *app_gps(void);

/*
 * Initialize the application framework: message center,
 * page manager and HAL (called from the platform main).
 */
void app_init(void);

/*
 * Application main loop: poll keys, dispatch events and
 * run the LVGL timer handler until hal_should_quit() is true.
 */
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */