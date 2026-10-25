#ifndef PAGE_LIVEMAP_H
#define PAGE_LIVEMAP_H

#include "../Utils/page_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Live map page: full-bleed map_view widget (with its GPS HUD) plus the
 * shared status bar. A regular drawer app: pushed from the launcher,
 * optionally with a .map path as the page_manager_open() parameter
 * (the map picker opens it that way); without a parameter the map is
 * resolved from the persisted config selection, else the first .map in
 * the data dir.
 *
 * Key model (PAGE_FLAG_NO_AUTO_BACK: long ENTER reaches the page):
 *   arrows            pan (hold to repeat)
 *   ENTER click       zoom in; ENTER double-click: recenter + follow
 *   ENTER held        zoom out; at min zoom: close -> pop to the drawer
 */
extern page_t page_livemap;

#ifdef __cplusplus
}
#endif

#endif /* PAGE_LIVEMAP_H */
