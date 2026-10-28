#include "plugin_manager.h"
#include "plugin_api.h"
#include "log.h"
#include "../Config/app_config.h"
#include "../Pages/app_registry.h"
#include "../Pages/page_app_drawer.h"

#include <dirent.h>
#include <dlfcn.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PLUGIN_LOAD_MAX  32
#define PLUGIN_PATH_SZ   512
#define MANIFEST_MAX     4096
#define ICON_W           40
#define ICON_H           40
#define ICON_BYTES       (ICON_W * ICON_H * 3)   /* RGB565 + A8 */

/* Fixed-format app manifest (key=value lines, "#" comments), see
 * PLUGIN_SDK.md. Values may use "\n" for line breaks in info/data. */
typedef struct {
    char name[40];      /* required: unique id ([a-z0-9_-])           */
    char label[64];     /* drawer label                                */
    char version[32];
    char page[40];      /* page name the plugin registers              */
    char exec[64];      /* .so file name inside the package            */
    char icon[64];      /* 40x40 RGB565+A8 icon file                   */
    char info[128];     /* grey drawer column                          */
    char data[128];     /* white drawer column                         */
    int  abi;           /* -1 when absent                              */
} Manifest;

typedef struct {
    char     path[PLUGIN_PATH_SZ];   /* dedup key (artifact/dir path)  */
    char     dir[PLUGIN_PATH_SZ];    /* "" for legacy bare .so         */
    Manifest man;                    /* zeroed when absent             */
    int      has_man;
} LoadedApp;

static LoadedApp s_apps[PLUGIN_LOAD_MAX];
static int s_n_apps;
static char s_dir[PLUGIN_PATH_SZ];      /* plugin root dir                */
static char s_cur_dir[PLUGIN_PATH_SZ];  /* plugin_init() context          */
static long s_dir_mtime;
static lv_timer_t *s_watch;

/* ---------- small string helpers ---------- */

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
        *--e = 0;
    while (*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));
}

/* Decode "\n" escapes into real newlines. */
static void unescape_nl(char *dst, size_t dstcap, const char *src)
{
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 1 < dstcap; i++) {
        if (src[i] == '\\' && src[i + 1] == 'n') {
            dst[o++] = '\n';
            i++;
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = 0;
}

/* ---------- manifest ---------- */

static void manifest_set(Manifest *m, char *key, char *val)
{
    trim(key);
    trim(val);
    if (!strcmp(key, "name"))          snprintf(m->name, sizeof m->name, "%s", val);
    else if (!strcmp(key, "label"))    snprintf(m->label, sizeof m->label, "%s", val);
    else if (!strcmp(key, "version"))  snprintf(m->version, sizeof m->version, "%s", val);
    else if (!strcmp(key, "page"))     snprintf(m->page, sizeof m->page, "%s", val);
    else if (!strcmp(key, "exec"))     snprintf(m->exec, sizeof m->exec, "%s", val);
    else if (!strcmp(key, "icon"))     snprintf(m->icon, sizeof m->icon, "%s", val);
    else if (!strcmp(key, "info"))     unescape_nl(m->info, sizeof m->info, val);
    else if (!strcmp(key, "data"))     unescape_nl(m->data, sizeof m->data, val);
    else if (!strcmp(key, "abi"))      m->abi = atoi(val);
}

static int manifest_read(const char *path, Manifest *m)
{
    FILE *f = fopen(path, "rb");
    char buf[MANIFEST_MAX];
    size_t n;

    if (!f)
        return -1;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;

    memset(m, 0, sizeof *m);
    m->abi = -1;
    snprintf(m->exec, sizeof m->exec, "app.so");

    for (char *p = buf; p && *p;) {
        char *nl = strchr(p, '\n');
        char *eq;
        if (nl)
            *nl++ = 0;
        trim(p);
        if (*p && *p != '#' && (eq = strchr(p, '=')) != NULL) {
            *eq++ = 0;
            manifest_set(m, p, eq);
        }
        p = nl;
    }
    return m->name[0] ? 0 : -1;
}

/* ---------- icon resource (fixed format) ---------- */

static const lv_img_dsc_t *icon_load(const char *dir, const char *file)
{
    char path[PLUGIN_PATH_SZ];
    uint8_t *px;
    lv_img_dsc_t *dsc;
    FILE *f;

    if (!file[0])
        return NULL;
    snprintf(path, sizeof path, "%s/%s", dir, file);
    f = fopen(path, "rb");
    if (!f) {
        LOG_W("plugin: icon %s missing", path);
        return NULL;
    }
    px = malloc(ICON_BYTES);
    if (!px || fread(px, 1, ICON_BYTES, f) != ICON_BYTES) {
        LOG_W("plugin: icon %s is not %dx%d RGB565+A8",
              path, ICON_W, ICON_H);
        fclose(f);
        free(px);
        return NULL;
    }
    fclose(f);

    dsc = calloc(1, sizeof *dsc);
    if (!dsc) {
        free(px);
        return NULL;
    }
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    dsc->header.always_zero = 0;
    dsc->header.w = ICON_W;
    dsc->header.h = ICON_H;
    dsc->data_size = ICON_BYTES;
    dsc->data = px;
    return dsc;
}

/* ---------- load one app ---------- */

const char *app_plugin_dir(void)
{
    return s_cur_dir;
}

static int already_loaded(const char *path)
{
    for (int i = 0; i < s_n_apps; i++)
        if (!strcmp(s_apps[i].path, path))
            return 1;
    return 0;
}

/*
 * Load one app: dlopen, validate the contract, run plugin_init(), then
 * fill the drawer presentation from the manifest (fixed format):
 *   - an entry the plugin registered for the manifest page keeps its
 *     dynamic columns (info/data/data_get) and gets the manifest icon;
 *   - otherwise the host builds the entry from the manifest itself.
 * Every failure path logs and leaves the host alive. A successfully
 * initialized app is intentionally never dlclosed.
 *
 * slot: reserved s_apps[] entry, already filled with the dedup path
 * and (for packages) the manifest, so the strings the drawer entry
 * borrows stay valid for the session. has_man == 0 for legacy .so.
 */
static int app_load(const char *path, LoadedApp *slot)
{
    const Manifest *man = slot->has_man ? &slot->man : NULL;
    const char *dir = slot->dir;
    void *h;
    const uint32_t *abi;
    const char **name, **version;
    int (*init_fn)(void);
    const lv_img_dsc_t *icon = NULL;
    app_entry_t *entry = NULL;
    page_t *pg = NULL;

    h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        LOG_W("plugin: dlopen %s failed: %s", path, dlerror());
        return -1;
    }

    abi = dlsym(h, "plugin_abi_version");
    name = dlsym(h, "plugin_name");
    version = dlsym(h, "plugin_version");
    init_fn = (int (*)(void))dlsym(h, "plugin_init");

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
    if (man && man->abi >= 0 && man->abi != PLUGIN_ABI_VERSION) {
        LOG_W("plugin: \"%s\" manifest ABI %d, host expects %u - skipped",
              man->name, man->abi, (unsigned)PLUGIN_ABI_VERSION);
        dlclose(h);
        return -1;
    }

    LOG_I("plugin: loading \"%s\" %s (%s)",
          *name, (version && *version) ? *version : "?", path);

    /* expose the package dir for resource lookups during init */
    snprintf(s_cur_dir, sizeof s_cur_dir, "%s", dir);
    if (init_fn() != 0) {
        s_cur_dir[0] = 0;
        LOG_W("plugin: \"%s\" init failed, unloaded", *name);
        dlclose(h);
        return -1;
    }
    s_cur_dir[0] = 0;

    if (man && dir[0]) {
        icon = icon_load(dir, man->icon);
        if (man->page[0])
            pg = page_find(man->page);

        if (pg)
            entry = app_registry_find_page(pg);

        if (entry) {
            if (icon) {
                entry->img = icon;
                entry->icon_color = 0;
            }
        } else if (pg && (man->label[0] || man->info[0])) {
            static app_entry_t e;
            memset(&e, 0, sizeof e);
            e.name = slot->man.label[0] ? slot->man.label : slot->man.name;
            e.page = pg;
            e.img = icon;
            e.info = slot->man.info[0] ? slot->man.info : NULL;
            e.data = slot->man.data[0] ? slot->man.data : NULL;
            if (app_registry_add(&e) < 0)
                LOG_W("plugin: \"%s\" drawer entry rejected (name in use)",
                      man->name);
        }
    }

    LOG_I("plugin: \"%s\" ready", *name);
    return 0;
}

/* ---------- directory scan ---------- */

static int load_packaged(const char *dirpath, const char *name, int reload)
{
    char mpath[PLUGIN_PATH_SZ], spath[PLUGIN_PATH_SZ];
    Manifest man;
    LoadedApp *slot;

    (void)name;
    (void)reload;
    if (already_loaded(dirpath) || s_n_apps >= PLUGIN_LOAD_MAX)
        return 0;

    snprintf(mpath, sizeof mpath, "%s/manifest.txt", dirpath);
    if (manifest_read(mpath, &man) != 0) {
        LOG_W("plugin: %s has no usable manifest.txt, skipped", dirpath);
        return 0;
    }
    snprintf(spath, sizeof spath, "%s/%s", dirpath,
             man.exec[0] ? man.exec : "app.so");

    slot = &s_apps[s_n_apps];
    memset(slot, 0, sizeof *slot);
    snprintf(slot->path, sizeof slot->path, "%s", dirpath);
    snprintf(slot->dir, sizeof slot->dir, "%s", dirpath);
    slot->man = man;
    slot->has_man = 1;

    if (app_load(spath, slot) != 0) {
        memset(slot, 0, sizeof *slot);
        return 0;
    }
    s_n_apps++;
    return 1;
}

static int load_legacy(const char *path)
{
    LoadedApp *slot;

    if (already_loaded(path) || s_n_apps >= PLUGIN_LOAD_MAX)
        return 0;

    slot = &s_apps[s_n_apps];
    memset(slot, 0, sizeof *slot);
    snprintf(slot->path, sizeof slot->path, "%s", path);

    if (app_load(path, slot) != 0) {
        memset(slot, 0, sizeof *slot);
        return 0;
    }
    s_n_apps++;
    return 1;
}

static int plugin_scan(int reload)
{
    DIR *dp;
    struct dirent *ent;
    char names[PLUGIN_LOAD_MAX][128];
    int n = 0, added = 0;

    mkdir(s_dir, 0755);   /* best effort; exists / unwritable both fine */
    dp = opendir(s_dir);
    if (!dp) {
        LOG_W("plugin: cannot open %s", s_dir);
        return 0;
    }
    while ((ent = readdir(dp)) != NULL && n < PLUGIN_LOAD_MAX) {
        if (ent->d_name[0] == '.')
            continue;
        snprintf(names[n], sizeof names[n], "%s", ent->d_name);
        n++;
    }
    closedir(dp);

    /* deterministic order: plain strcmp */
    for (int i = 1; i < n; i++) {
        char tmp[128];
        int j = i;
        memcpy(tmp, names[i], sizeof tmp);
        while (j > 0 && strcmp(names[j - 1], tmp) > 0) {
            memcpy(names[j], names[j - 1], sizeof tmp);
            j--;
        }
        memcpy(names[j], tmp, sizeof tmp);
    }

    for (int i = 0; i < n; i++) {
        char path[PLUGIN_PATH_SZ];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", s_dir, names[i]);
        if (stat(path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            added += load_packaged(path, names[i], reload);
        } else {
            const char *dot = strrchr(names[i], '.');
            if (!dot || strcmp(dot, ".so") != 0)
                continue;
            added += load_legacy(path);
        }
    }
    return added;
}

/* 1 Hz watcher: a new/removed app package changes the directory mtime. */
static void watch_cb(lv_timer_t *timer)
{
    struct stat st;

    (void)timer;
    if (stat(s_dir, &st) != 0 || st.st_mtime == s_dir_mtime)
        return;
    s_dir_mtime = st.st_mtime;
    if (plugin_scan(1) > 0) {
        LOG_I("plugin: hot-installed app(s) detected, drawer refreshed");
        app_drawer_refresh();
    }
}

void plugin_manager_init(void)
{
    struct stat st;
    int ok;

    const char *dir = getenv("PICO_NAV_APPS");
    if (!dir || !dir[0])
        dir = APP_PLUGIN_DIR_DEFAULT;
    snprintf(s_dir, sizeof s_dir, "%s", dir);

    ok = plugin_scan(0);
    LOG_I("plugin: %d app(s) loaded from %s", ok, s_dir);

    if (stat(s_dir, &st) == 0)
        s_dir_mtime = st.st_mtime;
    if (!s_watch)
        s_watch = lv_timer_create(watch_cb, 1000, NULL);
}
