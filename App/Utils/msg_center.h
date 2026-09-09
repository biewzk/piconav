#ifndef MSG_CENTER_H
#define MSG_CENTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed message topic table.
 * Append new topics here and keep MSG_COUNT in sync. */
typedef enum {
    MSG_KEY = 0,       /* key_event_t*: key press/release */
    MSG_CONFIG_CHANGED, /* NULL: persisted config changed via config_save */
    MSG_COUNT
} msg_topic_t;

typedef struct msg_sub msg_sub_t;
typedef struct msg_center msg_center_t;

/* Subscription callback */
typedef void (*msg_handler_t)(msg_topic_t topic, const void *data, void *user);

/* Create a message center (topic table + per-topic subscriber list). */
msg_center_t *msg_center_create(void);

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
