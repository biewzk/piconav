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

int          app_registry_count(void);
app_entry_t *app_registry_get(int index);

#endif /* APP_REGISTRY_H */
