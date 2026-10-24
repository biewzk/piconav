#ifndef PAGE_MAPSELECT_H
#define PAGE_MAPSELECT_H

#include "../Utils/page_manager.h"

/*
 * page_mapselect - map picker shown when the "map" app is opened.
 *
 * Lists the .map files found in APP_MAP_DIR_DEFAULT (or $MF_MAP_DIR),
 * lets the user pick one with the keys, persists the choice to config
 * key "map.selected" and opens it in page_livemap.
 */

extern page_t page_mapselect;

#endif /* PAGE_MAPSELECT_H */
