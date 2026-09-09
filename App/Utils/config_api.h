#ifndef CONFIG_API_H
#define CONFIG_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * config_api.h - userspace config storage wrapper library
 *
 * Provides JSON-based key-value config storage shared by all apps, on top
 * of the kernel config character device (/dev/config).
 *
 * Features:
 *   - key-value access (config_get_int / config_set_str ...)
 *   - atomic persistence (config_save) triggering low-level wear leveling
 *     + power-loss-safe writes
 *   - schema version management (CONFIG_SCHEMA_VERSION) for forward-
 *     compatible config evolution
 *
 * Usage (typical app lifecycle):
 *   config_init();                 // open and load existing config
 *   int v; config_get_int("k", &v, 5);
 *   config_set_int("k", 20);
 *   config_save();                 // persist to NAND
 *   config_deinit();
 */

/* Bump whenever config semantics change (add/remove keys), for migration */
#define CONFIG_SCHEMA_VERSION 1

/* Schema field name kept in every write */
#define CONFIG_KEY_SCHEMA_VERSION "schema_version"

/* Max config length for a read of the open descriptor (matches driver) */
#define CONFIG_API_MAX_DATA (64 * 1024)

/* Lifecycle */
int config_init(void);            /* open /dev/config and load existing config (re-callable) */
int config_deinit(void);          /* release resources */

/* Key-value access (operates on the in-memory config root, not yet persisted) */
int config_get_int(const char *key, int *out, int def);
int config_get_uint(const char *key, unsigned int *out, unsigned int def);
int config_get_str(const char *key, char *buf, size_t len, const char *def);
int config_get_bool(const char *key, int *out, int def);

int config_set_int(const char *key, int val);
int config_set_uint(const char *key, unsigned int val);
int config_set_str(const char *key, const char *val);
int config_set_bool(const char *key, int val);

/* Remove a config key */
int config_unset(const char *key);

/* Persist: serialize the in-memory config and write to the driver (0 on success) */
int config_save(void);

/* Roll back to the last known valid version (done automatically on the
 * read path; explicit call discards unsaved changes and reloads) */
int config_rollback(void);

/* Query the underlying driver status */
int config_get_status(unsigned int *active_block, unsigned int *total_blocks,
                      unsigned int *data_len, unsigned int *seq);

/* Format the whole config partition (clears all config, dangerous) */
int config_format(void);

/* Whether config has been modified but not yet saved */
int config_is_dirty(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_API_H */
