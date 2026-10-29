#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

/*
 * app_runtime - the runtime a standalone app executable links against.
 *
 * A pico_nav "app" is a normal program: it owns the framebuffer, the
 * key device and (optionally) the GPS UART while it runs, then exits and
 * the launcher GUI comes back. The runtime provides everything the
 * launcher GUI provides to an in-process page, minus the launcher UI:
 *
 *   - LVGL initialisation on /dev/fb0 (same driver/buffer as demo)
 *   - key polling -> MSG_KEY -> page manager dispatch
 *   - message center, page manager (page_register / push / pop)
 *   - optional GPS service (UART) and config watcher
 *   - long ENTER on the root page ends the loop and returns 0
 *
 * App code is unchanged: it registers page_t objects and is built into
 * the executable (see app/pico_nav/PLUGIN_SDK.md).
 */
typedef struct {
    const char *name;            /* log tag, e.g. "flora"              */
    const char *root_page;       /* page opened first (its name)       */
    void (*register_pages)(void);/* page_register() calls              */
    int  use_gps;                /* start the GPS service (UART)       */
    int  use_config_watcher;     /* publish MSG_CONFIG_CHANGED on edits*/
    /* Optional: called once after the root page is up (create timers,
     * publish the first drawer preview, ...). */
    void (*after_start)(void);
} app_runtime_cfg_t;

/* Run the app: initialise, build the UI, loop until the user backs out
 * of the root page (long ENTER) or hal_should_quit(). Returns the
 * process exit code (0 = clean). */
int app_runtime_run(const app_runtime_cfg_t *cfg);

/* Directory of the running executable (its installed package dir).
 * Exposed to app code as app_plugin_dir(), so packaged resources such
 * as flora.bin can be located the same way as in a .so plugin. */
const char *app_self_dir(void);
const char *app_plugin_dir(void);

/* Publish the grey/white column the launcher shows for this app in the
 * app drawer (fixed format: up to 3 short ASCII lines, "\n" separated).
 * Written atomically to <app dir>/preview.txt, which the launcher
 * re-reads about once a second while the app is not running - this is
 * how a process-isolated app keeps its preview up to date without the
 * host calling into it. Returns 0 on success. */
int app_preview_write(const char *text);

#endif /* APP_RUNTIME_H */
