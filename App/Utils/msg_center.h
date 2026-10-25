#ifndef MSG_CENTER_H
#define MSG_CENTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed message topic table.
 * Append new topics here and keep MSG_COUNT in sync. Runtime topics
 * registered via msg_topic_register() (e.g. by plugins) get ids from
 * MSG_COUNT upward; MSG_TOPIC_INVALID marks a failed lookup. */
typedef enum {
    MSG_KEY = 0,       /* key_event_t*: key press/release */
    MSG_CONFIG_CHANGED, /* NULL: persisted config changed via config_save */
    MSG_GPS,           /* gps_data_t*: fresh NMEA snapshot from the HAL */
    MSG_COUNT
} msg_topic_t;

#define MSG_DYNAMIC_MAX 8   /* runtime-registered topics per center */
#define MSG_TOPIC_MAX   (MSG_COUNT + MSG_DYNAMIC_MAX)
#define MSG_TOPIC_INVALID ((msg_topic_t)-1)

typedef struct msg_sub msg_sub_t;
typedef struct msg_center msg_center_t;

/* Subscription callback */
typedef void (*msg_handler_t)(msg_topic_t topic, const void *data, void *user);

/* Create a message center (topic table + per-topic subscriber list). */
msg_center_t *msg_center_create(void);

/* Register a named runtime topic (idempotent by name). The name is
 * copied. Returns the topic id or MSG_TOPIC_INVALID when full/bad. */
msg_topic_t msg_topic_register(msg_center_t *mc, const char *name);

/* Look up a runtime topic id by name; MSG_TOPIC_INVALID when absent.
 * Built-in enum topics are not findable here (use the enum directly). */
msg_topic_t msg_topic_find(msg_center_t *mc, const char *name);

/* Publish: walk the topic's subscriber list and call each handler. */
void msg_center_publish(msg_center_t *mc, msg_topic_t topic, const void *data);

/* Subscribe: prepend to the topic's list; returns the sub node (for cancel). */
msg_sub_t *msg_center_subscribe(msg_center_t *mc, msg_topic_t topic,
                                msg_handler_t handler, void *user);

/* Unsubscribe and free the sub node. */
void msg_center_unsubscribe(msg_center_t *mc, msg_topic_t topic, msg_sub_t *sub);

/* Destroy the center and all remaining subscriptions. */
void msg_center_destroy(msg_center_t *mc);

#ifdef __cplusplus
}
#endif

#endif /* MSG_CENTER_H */
