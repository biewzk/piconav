#include "app_registry.h"
#include "../Utils/log.h"

#include <string.h>

#define APP_REGISTRY_MAX 32

static app_entry_t s_apps[APP_REGISTRY_MAX];
static int s_count;

int app_registry_add(const app_entry_t *entry)
{
    if (!entry || !entry->name || (!entry->page && !entry->exec))
        return -1;
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_apps[i].name, entry->name) == 0)
            return -2;
    if (s_count >= APP_REGISTRY_MAX)
        return -1;
    s_apps[s_count] = *entry;
    LOG_I("app registry: + \"%s\"", entry->name);
    return s_count++;
}

int app_registry_count(void)
{
    return s_count;
}

app_entry_t *app_registry_get(int index)
{
    if (index < 0 || index >= s_count)
        return NULL;
    return &s_apps[index];
}

app_entry_t *app_registry_find(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_apps[i].name, name) == 0)
            return &s_apps[i];
    return NULL;
}

app_entry_t *app_registry_find_page(const page_t *page)
{
    if (!page)
        return NULL;
    for (int i = 0; i < s_count; i++)
        if (s_apps[i].page == page)
            return &s_apps[i];
    return NULL;
}
