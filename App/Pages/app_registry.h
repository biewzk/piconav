#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "../Utils/page_manager.h"
#include "lvgl.h"
#include <stdint.h>

typedef struct {
    const char *name;
    const char *icon;              /* letter glyph fallback (no img)  */
    uint32_t    icon_color;        /* letter glyph color              */
    page_t     *page;
    /* Full icon image (40x40 rounded tile, see Utils/app_icons.h).
     * NULL = fall back to the letter glyph. Plugins reference the
     * host's app_icon_* images by name. */
    const lv_img_dsc_t *img;
} app_entry_t;

/*
 * Runtime app list: built-ins register from pages_builtin_register(),
 * plugins add their entries from plugin_init(). Entries are copied into
 * a static table; the strings stay owned by the registrant.
 */

/* Add a drawer entry. Returns the index, -1 on bad entry/table full,
 * -2 on duplicate name. */
int app_registry_add(const app_entry_t *entry);

int          app_registry_count(void);
app_entry_t *app_registry_get(int index);

#endif /* APP_REGISTRY_H */
