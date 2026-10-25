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
 * via page_register(); add drawer entries via app_registry_add().
 */

/* Bump when the exported API surface changes shape (struct layout,
 * function signatures). Host and plugin must agree on this number. */
#define PLUGIN_ABI_VERSION 1

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_API_H */
