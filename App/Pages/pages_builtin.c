#include "pages_builtin.h"
#include "page_home.h"
#include "page_dialplate.h"
#include "page_gps.h"
#include "page_livemap.h"
#include "page_mapselect.h"
#include "page_settings.h"
#include "app_registry.h"
#include "../Utils/app_icons.h"

/*
 * The one place that knows the built-in pages. Everything else (drawer,
 * navigation) works on names from the registries, so new features - in
 * tree or loaded as plugins - never need to touch the callers.
 *
 * Drawer apps: trip dialplate, GPS receiver, live map, settings. The
 * map picker (mapselect) is also registered - plugins may open it by
 * name; it then pushes the map page with the picked file.
 */
void pages_builtin_register(void)
{
    /* UI labels are UTF-8 escapes (code stays ASCII) */
    static const app_entry_t apps[] = {
        { .name = "\xE8\xA1\x8C\xE8\xBD\xA6",       /* trip */
          .icon = "D", .icon_color = 0x00FF88,
          .page = &page_dialplate, .img = &app_icon_trip_img },
        { .name = "GPS",
          .icon = "G", .icon_color = 0x00C2FF,
          .page = &page_gps, .img = &app_icon_gps_img },
        { .name = "\xE5\x9C\xB0\xE5\x9B\xBE",       /* map */
          .icon = "M", .icon_color = 0xFFB547,
          .page = &page_livemap, .img = &app_icon_map_img },
        { .name = "\xE8\xAE\xBE\xE7\xBD\xAE",       /* settings */
          .icon = "S", .icon_color = 0xFFAA00,
          .page = &page_settings, .img = &app_icon_settings_img },
    };

    page_register(&page_home);
    page_register(&page_dialplate);
    page_register(&page_gps);
    page_register(&page_livemap);
    page_register(&page_mapselect);
    page_register(&page_settings);

    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++)
        app_registry_add(&apps[i]);
}
