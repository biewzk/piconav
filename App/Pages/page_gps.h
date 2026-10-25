#ifndef PAGE_GPS_H
#define PAGE_GPS_H

#include "../Utils/page_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GPS receiver status page: sky plot + SNR bars + position details,
 * 240x320 including the status bar. A regular drawer app: pushed from
 * the launcher, exited with the long-ENTER back gesture (or LEFT).
 * Claims a fast (1 s) GPS sample slot while shown.
 */
extern page_t page_gps;

#ifdef __cplusplus
}
#endif

#endif /* PAGE_GPS_H */
