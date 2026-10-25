#include "plugin_manager.h"
#include "plugin_api.h"
#include "log.h"
#include "../Config/app_config.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PLUGIN_LOAD_MAX  32
#define PLUGIN_PATH_SZ   512

/*
 * Load one plugin: dlopen, validate the contract, run plugin_init().
 * Every failure path logs and leaves the host alive. A successfully
 * initialized plugin is intentionally never dlclosed - its pages live
 * in the registry for the rest of the session.
 */
static int plugin_load(const char *path)
{
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        LOG_W("plugin: dlopen %s failed: %s", path, dlerror());
        return -1;
    }

    const uint32_t *abi = dlsym(h, "plugin_abi_version");
    const char **name = dlsym(h, "plugin_name");
    const char **version = dlsym(h, "plugin_version");
    int (*init_fn)(void) = (int (*)(void))dlsym(h, "plugin_init");

    if (!abi || !name || !*name || !init_fn) {
        LOG_W("plugin: %s misses the required exports, skipped", path);
        dlclose(h);
        return -1;
    }
    if (*abi != PLUGIN_ABI_VERSION) {
        LOG_W("plugin: \"%s\" ABI %u, host expects %u - rebuild against "
              "this firmware, skipped", *name, (unsigned)*abi,
              (unsigned)PLUGIN_ABI_VERSION);
        dlclose(h);
        return -1;
    }

    LOG_I("plugin: loading \"%s\" %s (%s)",
          *name, (version && *version) ? *version : "?", path);
    if (init_fn() != 0) {
        LOG_W("plugin: \"%s\" init failed, unloaded", *name);
        dlclose(h);
        return -1;
    }
    LOG_I("plugin: \"%s\" ready", *name);
    return 0;
}

/*
 * Scan dir for .so files and load them in name order (deterministic
 * drawer order). The directory is created when missing so a fresh
 * device has the mount point ready.
 */
void plugin_manager_init(void)
{
    const char *dir = getenv("PICO_NAV_APPS");
    if (!dir || !dir[0])
        dir = APP_PLUGIN_DIR_DEFAULT;

    mkdir(dir, 0755);   /* best effort; exists / unwritable both fine */
    DIR *dp = opendir(dir);
    if (!dp) {
        LOG_W("plugin: cannot open %s", dir);
        return;
    }

    char names[PLUGIN_LOAD_MAX][PLUGIN_PATH_SZ];
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && n < PLUGIN_LOAD_MAX) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".so") != 0)
            continue;
        snprintf(names[n], PLUGIN_PATH_SZ, "%s", ent->d_name);
        n++;
    }
    closedir(dp);

    /* Insertion sort by file name (case-insensitive-ish is overkill:
     * plain strcmp keeps it stable and predictable). */
    for (int i = 1; i < n; i++) {
        char tmp[PLUGIN_PATH_SZ];
        int j = i;
        memcpy(tmp, names[i], sizeof(tmp));
        while (j > 0 && strcmp(names[j - 1], tmp) > 0) {
            memcpy(names[j], names[j - 1], sizeof(tmp));
            j--;
        }
        memcpy(names[j], tmp, sizeof(tmp));
    }

    int ok = 0;
    for (int i = 0; i < n; i++) {
        char path[PLUGIN_PATH_SZ];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (plugin_load(path) == 0)
            ok++;
    }
    LOG_I("plugin: %d/%d loaded from %s", ok, n, dir);
}
