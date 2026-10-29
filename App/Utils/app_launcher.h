#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

/*
 * app_launcher - run a standalone app executable as a child process.
 *
 * The drawer entry of a packaged app whose manifest says kind=exe does
 * not point at a page inside the GUI; it carries the installed
 * executable path. Pressing ENTER only queues a request (the key
 * handler runs inside an LVGL callback); app_run() services it between
 * frames:
 *
 *   1. release the hardware the child needs (UART, key device)
 *   2. fork + exec the app (it opens /dev/fb0 itself)
 *   3. wait for it to exit
 *   4. re-acquire keys/UART and force a full repaint
 *
 * The child is a normal program: it can be started from a shell too,
 * which is the point of shipping apps as executables instead of .so
 * plugins.
 */

/* Queue a launch request (path must be an executable). */
void app_launcher_request(const char *path);

/* 1 when a request is waiting to be serviced. */
int app_launcher_pending(void);

/* Service the pending request: suspend, fork/exec, wait, resume. */
void app_launcher_run(void);

#endif /* APP_LAUNCHER_H */
