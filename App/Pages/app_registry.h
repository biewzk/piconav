#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "../Utils/page_manager.h"
#include <stdint.h>

typedef struct {
    const char *name;
    const char *icon;
    uint32_t    icon_color;
    page_t     *page;
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
