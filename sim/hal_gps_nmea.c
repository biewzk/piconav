#define _POSIX_C_SOURCE 200809L

#include "../HAL/hal.h"
#include "../HAL/nmea.h"
#include "../App/Utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simulator GPS HAL: replays the NMEA file given by SIM_NMEA (default
 * "sample.nmea", the same file main_sim.c falls back to) through the
 * shared parser, so the app sees the same gps_data_t flow as on device.
 * Lines are emitted as a 1 Hz burst (SIM_GPS_MS = interval per line). */

#define GPS_MAX_LINES   1024
#define GPS_LINE_MAX    128
#define GPS_DEFAULT_MS  100

static char     s_lines[GPS_MAX_LINES][GPS_LINE_MAX];
static int      s_line_n;
static int      s_line_i;
static uint32_t s_interval_ms = GPS_DEFAULT_MS;
static uint32_t s_next_ms;
static nmea_parser_t s_parser;

/* Read the replay file into memory (one NMEA sentence per line). */
static int load_nmea_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_W("gps(sim): cannot open %s, GPS replay disabled", path);
        return -1;
    }

    s_line_n = 0;
    char buf[GPS_LINE_MAX];
    while (s_line_n < GPS_MAX_LINES && fgets(buf, sizeof(buf), f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = 0;
        if (n == 0 || buf[0] != '$')
            continue;
        snprintf(s_lines[s_line_n], GPS_LINE_MAX, "%s", buf);
        s_line_n++;
    }
    fclose(f);

    LOG_I("gps(sim): replaying %s (%d sentences, %ums apart)",
          path, s_line_n, (unsigned)s_interval_ms);
    return s_line_n > 0 ? 0 : -1;
}

int hal_gps_init(void)
{
    const char *path = getenv("SIM_NMEA");
    const char *ms = getenv("SIM_GPS_MS");

    if (ms)
        s_interval_ms = (uint32_t)strtoul(ms, NULL, 10);
    if (s_interval_ms < 10)
        s_interval_ms = GPS_DEFAULT_MS;

    nmea_init(&s_parser);
    s_line_i = 0;
    s_next_ms = 0;

    if (!path || !path[0])
        path = "sample.nmea";
    return load_nmea_file(path);
}

int hal_gps_read(gps_data_t *out)
{
    if (!out || s_line_n == 0)
        return 0;

    uint32_t now = hal_tick_get();
    int updated = 0;

    if (s_next_ms == 0)
        s_next_ms = now;

    /* Emit at most one full pass per call so a stalled timer cannot spin. */
    while (now >= s_next_ms) {
        char line[GPS_LINE_MAX + 2];
        snprintf(line, sizeof(line), "%s\r\n", s_lines[s_line_i]);
        s_line_i = (s_line_i + 1) % s_line_n;
        updated |= nmea_feed(&s_parser, line, (int)strlen(line));
        s_next_ms += s_interval_ms;
        if (s_line_i == 0)
            break;
    }

    if (!updated)
        return 0;

    *out = s_parser.data;
    return 1;
}

/* Simulator: the NMEA replay is process-local, nothing to release. */
void hal_gps_suspend(void) {}
void hal_gps_resume(void) {}
