#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

/*
 * plugin_manager - dlopen loader for /data/apps/*.so
 *
 * Scans the plugin directory (sorted by file name), dlopens each .so,
 * validates the plugin contract (plugin_api.h) and runs plugin_init().
 * Anything that fails to load, mismatches the ABI or fails to init is
 * logged and skipped - the host never goes down because of a plugin.
 * Loaded plugins stay resident for the app lifetime.
 */

/* Called once by app_init() after the built-in pages/apps registered. */
void plugin_manager_init(void);

#endif /* PLUGIN_MANAGER_H */
