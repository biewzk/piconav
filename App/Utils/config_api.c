/*
 * config_api.c - userspace config storage wrapper library
 *
 * Provides shared JSON key-value configuration on top of the /dev/config
 * kernel driver + cJSON.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "config_api.h"
#include "config_ioctl_user.h"
#include "../third_party/cJSON/cJSON.h"

#ifndef CONFIG_API_DEV_PATH
#define CONFIG_API_DEV_PATH "/dev/config"
#endif

struct config_handle {
	int fd;
	cJSON *root;      /* in-memory config root object */
	int dirty;        /* modified but not yet saved */
};

static struct config_handle g_cfg;

/* ------------------------------------------------------------------ */
/* Internal: JSON helpers                                               */
/* ------------------------------------------------------------------ */

static cJSON *config_get_node(const char *key)
{
	if (!g_cfg.root)
		return NULL;
	return cJSON_GetObjectItemCaseSensitive(g_cfg.root, key);
}

/* Recursively free a JSON root; tolerant of NULL */
static void config_free_root(cJSON *root)
{
	if (root)
		cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int config_init(void)
{
	struct config_handle *c = &g_cfg;
	char *buf;
	ssize_t got = 0;

	if (c->root)
		config_deinit();

	c->fd = open(CONFIG_API_DEV_PATH, O_RDWR);
	if (c->fd < 0) {
		c->dirty = 0;
		/* Driver not loaded / partition not ready: keep an empty
		 * config and fall back to defaults. */
		return 0;
	}

	buf = malloc(CONFIG_API_MAX_DATA);
	if (!buf) {
		close(c->fd);
		c->fd = -1;
		return -ENOMEM;
	}

	/* Read the whole payload in one call (driver returns one block) */
	got = read(c->fd, buf, CONFIG_API_MAX_DATA);

	if (got <= 0) {
		/* No config data yet */
		c->root = cJSON_CreateObject();
		c->dirty = 0;
		free(buf);
		return 0;
	}

	{
		cJSON *parsed;
		buf[got] = '\0';   /* ensure NUL termination for cJSON_Parse */
		parsed = cJSON_Parse(buf);
		if (!parsed || !cJSON_IsObject(parsed)) {
			/* Parse failure: fall back to an empty config */
			if (parsed)
				cJSON_Delete(parsed);
			c->root = cJSON_CreateObject();
			c->dirty = 0;
			free(buf);
			return 0;
		}
		c->root = parsed;
		c->dirty = 0;
	}

	free(buf);
	return 0;
}

int config_deinit(void)
{
	struct config_handle *c = &g_cfg;

	config_free_root(c->root);
	c->root = NULL;
	if (c->fd >= 0) {
		close(c->fd);
		c->fd = -1;
	}
	c->dirty = 0;
	return 0;
}

int config_get_int(const char *key, int *out, int def)
{
	cJSON *node;

	if (out)
		*out = def;
	node = config_get_node(key);
	if (!node || !cJSON_IsNumber(node))
		return 0;   /* missing or wrong type, return default */
	if (out)
		*out = node->valueint;
	return 0;
}

int config_get_uint(const char *key, unsigned int *out, unsigned int def)
{
	int v;

	config_get_int(key, &v, (int)def);
	if (out)
		*out = (unsigned int)v;
	return 0;
}

int config_get_str(const char *key, char *buf, size_t len, const char *def)
{
	cJSON *node;

	if (buf && len > 0)
		buf[0] = '\0';

	node = config_get_node(key);
	if (!node || !cJSON_IsString(node)) {
		if (buf && def && len > 0)
			snprintf(buf, len, "%s", def);
		return 0;
	}
	if (buf && len > 0)
		snprintf(buf, len, "%s", node->valuestring);
	return 0;
}

int config_get_bool(const char *key, int *out, int def)
{
	cJSON *node;

	if (out)
		*out = def;
	node = config_get_node(key);
	if (!node || !cJSON_IsBool(node))
		return 0;
	if (out)
		*out = node->type == cJSON_True;
	return 0;
}

int config_set_int(const char *key, int val)
{
	cJSON *node;

	if (!g_cfg.root) {
		g_cfg.root = cJSON_CreateObject();
		if (!g_cfg.root)
			return -ENOMEM;
	}
	node = config_get_node(key);
	if (node) {
		if (cJSON_IsNumber(node)) {
			node->valueint = val;
			node->valuedouble = val;
		} else {
			cJSON_DeleteItemFromObjectCaseSensitive(g_cfg.root, key);
			cJSON_AddNumberToObject(g_cfg.root, key, val);
		}
	} else {
		cJSON_AddNumberToObject(g_cfg.root, key, val);
	}
	g_cfg.dirty = 1;
	return 0;
}

int config_set_uint(const char *key, unsigned int val)
{
	return config_set_int(key, (int)val);
}

int config_set_str(const char *key, const char *val)
{
	if (!g_cfg.root) {
		g_cfg.root = cJSON_CreateObject();
		if (!g_cfg.root)
			return -ENOMEM;
	}
	/* This cJSON version has no cJSON_SetValuestring, so delete and
	 * re-create the string item instead */
	if (config_get_node(key))
		cJSON_DeleteItemFromObjectCaseSensitive(g_cfg.root, key);
	cJSON_AddStringToObject(g_cfg.root, key, val ? val : "");
	g_cfg.dirty = 1;
	return 0;
}

int config_set_bool(const char *key, int val)
{
	cJSON *node;

	if (!g_cfg.root) {
		g_cfg.root = cJSON_CreateObject();
		if (!g_cfg.root)
			return -ENOMEM;
	}
	node = config_get_node(key);
	if (node)
		cJSON_DeleteItemFromObjectCaseSensitive(g_cfg.root, key);
	cJSON_AddBoolToObject(g_cfg.root, key, val);
	g_cfg.dirty = 1;
	return 0;
}

int config_unset(const char *key)
{
	if (g_cfg.root && config_get_node(key)) {
		cJSON_DeleteItemFromObjectCaseSensitive(g_cfg.root, key);
		g_cfg.dirty = 1;
	}
	return 0;
}

int config_save(void)
{
	struct config_handle *c = &g_cfg;
	char *text;
	ssize_t w, n;

	if (c->fd < 0)
		return -EIO;

	/* Record the schema version */
	cJSON *sver = cJSON_GetObjectItemCaseSensitive(c->root,
						CONFIG_KEY_SCHEMA_VERSION);
	if (cJSON_IsNumber(sver)) {
		sver->valueint = CONFIG_SCHEMA_VERSION;
		sver->valuedouble = CONFIG_SCHEMA_VERSION;
	} else {
		if (sver)
			cJSON_DeleteItemFromObjectCaseSensitive(c->root,
						CONFIG_KEY_SCHEMA_VERSION);
		cJSON_AddNumberToObject(c->root, CONFIG_KEY_SCHEMA_VERSION,
					CONFIG_SCHEMA_VERSION);
	}

	text = cJSON_PrintUnformatted(c->root);
	if (!text)
		return -ENOMEM;

	n = (ssize_t)strlen(text);
	if (n > CONFIG_API_MAX_DATA) {
		free(text);
		return -E2BIG;
	}

	/* Stage the payload in the driver's RAM, then commit it to NAND.
	 * The /dev/config char device does not support lseek, and the
	 * driver ignores the file offset anyway. */
	w = write(c->fd, text, (size_t)n);
	free(text);

	if (w != n)
		return (w < 0) ? -errno : -EIO;

	if (ioctl(c->fd, CONFIG_IOC_COMMIT, 0) < 0)
		return -errno;

	/* Clear dirty only after a successful commit */
	c->dirty = 0;
	return 0;
}

int config_rollback(void)
{
	/* Explicit rollback = discard staged (uncommitted) config in the
	 * driver's RAM and reload from flash. */
	struct config_handle *c = &g_cfg;

	if (c->fd >= 0)
		(void)ioctl(c->fd, CONFIG_IOC_ROLLBACK, 0);
	return config_init();
}

int config_get_status(unsigned int *active_block, unsigned int *total_blocks,
                      unsigned int *data_len, unsigned int *seq)
{
	struct config_status st;

	if (g_cfg.fd < 0)
		return -EIO;
	memset(&st, 0, sizeof(st));
	if (ioctl(g_cfg.fd, CONFIG_IOC_GETSTATUS, &st) < 0)
		return -errno;
	if (active_block) *active_block = st.active_block;
	if (total_blocks) *total_blocks = st.total_blocks;
	if (data_len)     *data_len     = st.data_len;
	if (seq)          *seq          = st.seq;
	return 0;
}

int config_format(void)
{
	if (g_cfg.fd < 0)
		return -EIO;
	if (ioctl(g_cfg.fd, CONFIG_IOC_FORMAT, 0) < 0)
		return -errno;
	/* Rebuild an empty config root after formatting */
	config_free_root(g_cfg.root);
	g_cfg.root = cJSON_CreateObject();
	g_cfg.dirty = 0;
	return 0;
}

int config_is_dirty(void)
{
	return g_cfg.dirty;
}
