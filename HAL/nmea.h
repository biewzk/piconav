#ifndef NMEA_H
#define NMEA_H

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal NMEA-0183 receiver parser shared by the device UART HAL and
 * the simulator replay HAL. Only the sentences needed by the UI are
 * decoded: GGA (fix/sats/altitude/position/time), RMC (status, position,
 * speed, course, time), GSA (2D/3D + satellites used in the solution)
 * and GSV (per-satellite azimuth/elevation/SNR + in-view count). Talker
 * IDs are ignored, so $GNGGA, $GPGGA, $BDGGA, ... are all accepted. */
#define NMEA_LINE_MAX 128
#define NMEA_SYS_MAX  4   /* GPS, GLONASS, BeiDou, other/combined */

typedef struct {
    char       line[NMEA_LINE_MAX];  /* sentence being assembled, no '$' */
    int        len;
    int        overflow;             /* current sentence too long: drop it */
    gps_data_t data;                 /* merged result of parsed sentences */
    /* Per-constellation GSV bookkeeping (indexed by sys_index()):
     * "in view" totals and the used-in-solution flags stashed from the
     * previous batch, because GSA arrives before GSV in every cycle and
     * a batch start must not lose the flags. */
    uint8_t    view_cnt[NMEA_SYS_MAX];
    uint8_t    stash_n[NMEA_SYS_MAX];
    uint8_t    stash_prn[NMEA_SYS_MAX][12];
    uint8_t    stash_used[NMEA_SYS_MAX][12];
} nmea_parser_t;

/* Reset the parser and its gps_data_t. */
void nmea_init(nmea_parser_t *p);

/* Feed raw bytes as they arrive from the receiver (any chunk size).
 * Returns 1 when at least one sentence updated the snapshot (the caller
 * can then publish it), 0 otherwise. */
int nmea_feed(nmea_parser_t *p, const char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* NMEA_H */
