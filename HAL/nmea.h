#ifndef NMEA_H
#define NMEA_H

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal NMEA-0183 receiver parser shared by the device UART HAL and
 * the simulator replay HAL. Only the sentences needed by the UI are
 * decoded: GGA (fix/sats/altitude/position/time) and RMC (status,
 * position, speed, course, time). Talker IDs are ignored, so $GNGGA,
 * $GPGGA, $BDGGA, ... are all accepted. */
#define NMEA_LINE_MAX 128

typedef struct {
    char       line[NMEA_LINE_MAX];  /* sentence being assembled, no '$' */
    int        len;
    int        overflow;             /* current sentence too long: drop it */
    gps_data_t data;                 /* merged result of parsed sentences */
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
