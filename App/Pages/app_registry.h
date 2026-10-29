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
    /* Drawer preview columns (X-TRACK rolling-list style), revealed on
     * focus. info: static grey lines (e.g. "Lat\nLon\nSats"), data:
     * static white column used only when data_get is NULL, data_get:
     * called about once a second while the drawer is visible and fills
     * the white column (ASCII, '\n' line breaks). All may be NULL. */
    const char *info;
    const char *data;
    void (*data_get)(char *buf, int len);
    /* Standalone app executable (packaged kind=exe). When set, the
     * drawer starts this program as a child process instead of pushing
     * a page - the app owns the screen while it runs. page is NULL
     * then; either page or exec must be present. */
    const char *exec;
    /* Preview file for standalone apps (manifest key preview=, path
     * inside the package). The app writes the fixed-format white
     * column ("<=3 ASCII lines") there; the drawer re-reads it about
     * once a second, so a process-isolated app still shows its last
     * state without the host calling into it. data_get wins if set. */
    const char *preview;
} app_entry_t;

/*
 * Runtime app list: built-ins register from pages_builtin_register(),
 * plugins add their entries from plugin_init(). Entries are copied into
 * a static table; the strings stay owned by the registrant.
 */

/* Add a drawer entry (page for an in-process page, exec for a
 * standalone app executable). Returns the index, -1 on bad
 * entry/table full, -2 on duplicate name. */
int app_registry_add(const app_entry_t *entry);

int          app_registry_count(void);
app_entry_t *app_registry_get(int index);

/* Lookup helpers (NULL when absent). Used by the plugin manager to
 * fill in drawer presentation from an app package manifest. */
app_entry_t *app_registry_find(const char *name);
app_entry_t *app_registry_find_page(const page_t *page);

#endif /* APP_REGISTRY_H */
