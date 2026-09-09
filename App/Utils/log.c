#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

/* Mirror to stderr by default: on device the demo is started by init so
 * stderr is empty (no side effect); the simulator prints to the terminal.
 * Set LOG_CONSOLE=0 to disable the mirror. */
static int s_console = 1;

void log_init(void)
{
    const char *c = getenv("LOG_CONSOLE");
    if (c && c[0] == '0')
        s_console = 0;

    /* LOG_PID: prefix each message with the pid; LOG_USER: user-level tool */
    openlog("pico_nav", LOG_PID | LOG_NDELAY, LOG_USER);
}

void log_write(int level, const char *fmt, ...)
{
    char msg[512];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Go through syslog: on device collected by BusyBox syslogd
     * (default /var/log/messages) */
    syslog(level, "%s", msg);

    if (s_console) {
        static const char *names[] = {
            "", "", "", "E", "W", "", "I", "D",
        };
        const char *n = (level >= 0 && level <= 7) ? names[level] : "?";
        fprintf(stderr, "[pico_nav][%s] %s\n", n, msg);
    }
}
