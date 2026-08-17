#define _POSIX_C_SOURCE 200809L
#include "../HAL/hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NMEA 文件回放: 逐行解析 GPRMC/GPGGA 中的位置/速度, 模拟 GPS 源
 * 模拟器在 sim/main_sim.c 通过 argc[1] 传入文件名 */

static FILE *s_fp;
static int   s_available = 1;   /* 至少回放一次 */
static gps_data_t s_last;

/* 简单 CRC 校验可跳过; 解析字段即可 */
static int nmea_parse_line(const char *line, gps_data_t *g)
{
    if (strncmp(line, "$GPRMC", 6) == 0) {
        /* $GPRMC,time,status,lat,N,lon,E,speed_knots,... */
        char *tok[12] = {0};
        int i = 0;
        const char *p = line;
        while (p && i < 12) {
            const char *comma = strchr(p, ',');
            if (!comma)
                break;
            tok[i] = strndup(p, comma - p);
            p = comma + 1;
            i++;
        }
        /* status */
        if (i > 2 && tok[2] && tok[2][0] == 'A') {
            g->fix = 1;
            if (i > 3 && tok[3]) {
                double lat = atof(tok[3]);
                g->lat = (int)(lat / 100) + (lat - (int)(lat / 100) * 100) / 60.0;
                if (i > 4 && tok[4] && tok[4][0] == 'S') g->lat = -g->lat;
            }
            if (i > 5 && tok[5]) {
                double lon = atof(tok[5]);
                g->lon = (int)(lon / 100) + (lon - (int)(lon / 100) * 100) / 60.0;
                if (i > 6 && tok[6] && tok[6][0] == 'W') g->lon = -g->lon;
            }
            if (i > 7 && tok[7])
                g->speed_kmh = atof(tok[7]) * 1.852;  /* 节 -> km/h */
        }
        for (int j = 0; j < 12; j++)
            free(tok[j]);
        return g->fix ? 1 : 0;
    }
    return 0;
}

int hal_gps_init(void)
{
    const char *path = getenv("SIM_NMEA");
    if (path) {
        s_fp = fopen(path, "r");
        if (s_fp)
            return 0;
        fprintf(stderr, "hal_gps_sim: 无法打开 %s\n", path);
    } else {
        /* 无文件则循环发布一次默认数据, 便于演示 */
        s_last.fix = 1;
        s_last.lat = 31.2304;
        s_last.lon = 121.4737;
        s_last.speed_kmh = 25.0;
        s_last.alt_m = 12.0;
        s_last.sats = 8;
        s_available = 1;
        return 0;
    }
    return -1;
}

int hal_gps_read(gps_data_t *g)
{
    if (!s_fp) {
        if (!s_available)
            return 0;
        s_available = 0;
        *g = s_last;
        return 1;
    }

    char line[128];
    while (fgets(line, sizeof(line), s_fp)) {
        if (nmea_parse_line(line, &s_last)) {
            *g = s_last;
            return 1;
        }
    }
    /* 文件结束: 循环回放 */
    rewind(s_fp);
    while (fgets(line, sizeof(line), s_fp)) {
        if (nmea_parse_line(line, &s_last)) {
            *g = s_last;
            return 1;
        }
    }
    return 0;
}