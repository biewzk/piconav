#ifndef PAGE_LIVEMAP_H
#define PAGE_LIVEMAP_H

#include "../Utils/page_manager.h"

extern page_t page_livemap;

/* Set the .map file to open on the next create() (used by the map picker).
 * Wins over the compiled-in default; the MF_MAP env var still wins over
 * this (sim/dev override). NULL or "" keeps the current setting. */
void page_livemap_set_map(const char *path);

#endif /* PAGE_LIVEMAP_H */