#ifndef CONFIG_WATCHER_H
#define CONFIG_WATCHER_H

#include "msg_center.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * config_watcher - makes external /dev/config changes visible in-app.
 *
 * The config char device has no change notification, so the watcher
 * cheap-probes the driver payload once a second (raw byte compare via
 * config_changed(), no JSON parsing) and, when another process staged
 * or committed something different, reloads the shared config root and
 * publishes MSG_CONFIG_CHANGED - the same event config_save() users
 * publish after in-app changes. Interested components just subscribe
 * to that topic and re-read their keys.
 *
 * Probing is skipped while this app holds unsaved edits
 * (config_is_dirty()), so the reload cannot clobber them.
 */
void config_watcher_init(msg_center_t *mc);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_WATCHER_H */
