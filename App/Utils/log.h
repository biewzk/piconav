#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels (aligned with syslog levels) */
#define LOG_LVL_ERR   3
#define LOG_LVL_WARN  4
#define LOG_LVL_INFO  6
#define LOG_LVL_DEBUG 7

/* Initialize logging: open syslog and read LOG_CONSOLE env var.
 * (stderr mirror is enabled by default; set LOG_CONSOLE=0 to disable) */
void log_init(void);

/* Core write function (use the macros below) */
void log_write(int level, const char *fmt, ...);

#define LOG_E(fmt, ...) log_write(LOG_LVL_ERR,   fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) log_write(LOG_LVL_WARN,  fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) log_write(LOG_LVL_INFO,  fmt, ##__VA_ARGS__)
#define LOG_D(fmt, ...) log_write(LOG_LVL_DEBUG, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */