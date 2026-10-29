#ifndef APP_SERVICES_H
#define APP_SERVICES_H

#include "msg_center.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Service accessors shared by the launcher (demo) and standalone app
 * executables. They resolve to whatever process-local instance the
 * running program created:
 *   - demo: app_init() binds the host message center and starts the
 *     services (GPS sampler, trip store, config watcher);
 *   - standalone app: app_runtime_run() binds the app's own message
 *     center and starts the services the app asked for.
 */
void app_services_bind(msg_center_t *mc);
msg_center_t *app_msg_center(void);

/* GPS data pool (refreshed by gps_service at the configured sample
 * interval; consumers read it on_show to show data before the next
 * pool refresh). NULL-sized pool when the app did not start GPS. */
const gps_data_t *app_gps(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SERVICES_H */
