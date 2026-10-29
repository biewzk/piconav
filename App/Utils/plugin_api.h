#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include <stdint.h>

#include "page_manager.h"
#include "../Pages/app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * plugin_api - contract between the pico_nav host (demo / pico_nav_sim)
 * and dynamically loaded plugins (/data/apps/*.so).
 *
 * A plugin is a shared object built against the same headers and ABI as
 * the host. It resolves everything else (LVGL, services) from the host
 * binary, which exports a curated symbol set (app.exports). HAL is not
 * exported: plugins consume services, not peripherals.
 *
 * Required exports (see PLUGIN_SDK.md):
 *   const uint32_t plugin_abi_version;  - must equal PLUGIN_ABI_VERSION
 *   const char *plugin_name;            - unique plugin name (log/lookup)
 *   const char *plugin_version;         - free-form version string
 *   int plugin_init(void);              - register pages + drawer apps
 *
 * plugin_init() runs once right after dlopen(); return 0 on success.
 * A successful plugin stays loaded for the app lifetime - never assume
 * unloading. A failing one is dlclose()d and skipped, the host keeps
 * running. Build pages as page_t with static storage and register them
 * via page_register(); add drawer entries via app_registry_add() -
 * entries can point at the host's app_icon_* images for their icon and
 * carry info/data preview columns for the drawer's rolling list.
 */

/* Directory of the app package currently being initialized ("" for
 * legacy bare .so plugins and outside plugin_init()). Packaged apps
 * use it to locate their installed resources; copy it if needed later. */
const char *app_plugin_dir(void);

/* Bump when the exported API surface changes shape (struct layout,
 * function signatures) or when the host stops exporting symbols
 * plugins used to rely on. Host and plugin must agree on this number.
 * History: 1 - initial contract; 2 - app_entry_t gained the img
 * field (drawer icon image, ABI 1 plugins are skipped until rebuilt);
 * 3 - app_entry_t gained the info/data/data_get preview columns
 * (ABI 2 plugins are skipped until rebuilt); 4 - app packages (.app):
 * the host no longer exports the app_icon_{gps,map,stopwatch}_img
 * tiles (each app now ships its own icon resource declared in
 * manifest.txt), added app_plugin_dir() for package resources, and the
 * host builds drawer entries from the package manifest; 5 - app_entry_t
 * gained the exec field: packaged apps with kind=exe ship their own
 * executable and run as a child process instead of a .so plugin; 6 -
 * app_entry_t gained the preview field (standalone apps publish their
 * drawer white column through a preview file). */
#define PLUGIN_ABI_VERSION 6

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_API_H */
