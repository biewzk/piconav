#include "pages_builtin.h"
#include "page_home.h"
#include "page_dialplate.h"
#include "page_settings.h"
#include "app_registry.h"
#include "../Utils/app_icons.h"
#include "../Utils/trip_store.h"

#include <stdio.h>

/*
 * The one place that knows the built-in pages. Everything else (drawer,
 * navigation) works on names from the registries, so new features - in
 * tree or loaded as plugins - never need to touch the callers.
 *
 * Core = system UI: the trip dialplate and settings are built in; the
 * GPS receiver and the live map are plugins (app/plugin_gps,
 * app/plugin_map) and add their own drawer entries from plugin_init().
 */

/* Trip session counters for the drawer preview column. */
static void trip_data_get(char *buf, int len)
{
    const trip_stats_t *trip = trip_store_get();
    unsigned move_s = trip->move_ms / 1000;

    snprintf(buf, len, "%.2fkm\n%02u:%02u",
             trip->trip_km, move_s / 60, move_s % 60);
}

void pages_builtin_register(void)
{
    /* UI labels are UTF-8 escapes (code stays ASCII) */
    static const app_entry_t apps[] = {
        { .name = "\xE8\xA1\x8C\xE8\xBD\xA6",       /* trip */
          .icon = "D", .icon_color = 0x00FF88,
          .page = &page_dialplate, .img = &app_icon_trip_img,
          .info = "Trip\nMoving", .data_get = trip_data_get },
        { .name = "\xE8\xAE\xBE\xE7\xBD\xAE",       /* settings */
          .icon = "S", .icon_color = 0xFFAA00,
          .page = &page_settings, .img = &app_icon_settings_img,
          .info = "Firmware\nTheme", .data = "pico_nav\nDark" },
    };

    page_register(&page_home);
    page_register(&page_dialplate);
    page_register(&page_settings);

    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++)
        app_registry_add(&apps[i]);
}
