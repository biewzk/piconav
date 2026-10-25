#include "pages_builtin.h"
#include "page_home.h"
#include "page_mapselect.h"
#include "page_dialplate.h"
#include "page_settings.h"
#include "page_livemap.h"
#include "app_registry.h"

/*
 * The one place that knows the built-in pages. Everything else (drawer,
 * navigation) works on names from the registries, so new features - in
 * tree or loaded as plugins - never need to touch the callers.
 */
void pages_builtin_register(void)
{
    static const app_entry_t apps[] = {
        { "地图", "M", 0x00C2FF, &page_mapselect },
        { "行车", "D", 0x00FF88, &page_dialplate },
        { "设置", "S", 0xFFAA00, &page_settings  },
    };

    page_register(&page_home);
    page_register(&page_mapselect);
    page_register(&page_dialplate);
    page_register(&page_settings);
    page_register(&page_livemap);

    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++)
        app_registry_add(&apps[i]);
}
