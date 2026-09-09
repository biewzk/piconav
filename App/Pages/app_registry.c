#include "app_registry.h"
#include "page_livemap.h"
#include "page_dialplate.h"
#include "page_settings.h"

static app_entry_t s_apps[] = {
    { "地图",   "M", 0x00C2FF, &page_livemap   },
    { "行车",   "D", 0x00FF88, &page_dialplate },
    { "设置",   "S", 0xFFAA00, &page_settings  },
};

int app_registry_count(void)
{
    return sizeof(s_apps) / sizeof(s_apps[0]);
}

app_entry_t *app_registry_get(int index)
{
    if (index < 0 || index >= app_registry_count())
        return NULL;
    return &s_apps[index];
}
