#include "app_services.h"
#include "gps_service.h"

/*
 * Process-local service context. Exactly one program runs per process:
 * either the launcher GUI (demo) or one standalone app executable, so a
 * single static slot is enough.
 */
static msg_center_t *s_mc;
static gps_data_t s_no_gps;   /* zeroed pool for apps without GPS */

void app_services_bind(msg_center_t *mc)
{
    s_mc = mc;
}

msg_center_t *app_msg_center(void)
{
    return s_mc;
}

const gps_data_t *app_gps(void)
{
    return s_mc ? gps_service_data() : &s_no_gps;
}
