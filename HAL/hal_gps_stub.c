#include "hal.h"
#include "../App/Utils/log.h"

/* Real-device GPS: wired to UART + minmea in Phase 3; no-op for now */

int hal_gps_init(void)
{
    LOG_W("GPS not connected (hal_gps_stub): no fix data on device");
    return 0;
}

int hal_gps_read(gps_data_t *g)
{
    (void)g;
    return 0;
}