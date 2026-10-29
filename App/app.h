#ifndef APP_H
#define APP_H

#include "Utils/msg_center.h"
#include "Utils/app_services.h"
#include "../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Launcher-side application framework (demo). Required accessors
 * (app_msg_center/app_gps) live in Utils/app_services.h so the same
 * page code builds into standalone app executables too.
 */

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
