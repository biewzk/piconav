#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

/*
 * plugin_manager - loads GUI apps from the plugin directory.
 *
 * Two layouts are supported:
 *   1. app packages:  <dir>/<name>/manifest.txt + app.so + resources
 *      (installed by webctl from a .app package, see PLUGIN_SDK.md).
 *      The manifest is the fixed-format drawer descriptor: the host
 *      parses label/info/data/icon from it, so the app's presentation
 *      does not depend on its code.
 *   2. legacy bare plugins: <dir>/*.so (the old
 *      "upload a single .so" layout; entry comes from plugin_init()).
 *
 * A 1 Hz watcher re-scans the directory: installing an app package
 * makes it appear in the drawer without restarting the GUI.
 */
void plugin_manager_init(void);

/* Directory of the plugin currently being initialized ("" outside
 * plugin_init). Exported to plugins as app_plugin_dir() so packaged
 * apps can locate their installed resources. */
const char *app_plugin_dir(void);

#endif /* PLUGIN_MANAGER_H */
