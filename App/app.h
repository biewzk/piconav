#ifndef APP_H
#define APP_H

#include "Utils/msg_center.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global message center, created by app_init(). */
msg_center_t *app_msg_center(void);

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