#include "msg_center.h"

#include <stdlib.h>

struct msg_sub {
    msg_handler_t   handler;
    void           *user;
    msg_sub_t      *next;
};

struct msg_center {
    msg_sub_t *topics[MSG_COUNT];
};

/* Create a message center with an empty topic table. */
msg_center_t *msg_center_create(void)
{
    msg_center_t *mc = calloc(1, sizeof(*mc));
    return mc;
}

/*
 * Publish an event on a topic: call every subscribed handler in order.
 */
void msg_center_publish(msg_center_t *mc, msg_topic_t topic, const void *data)
{
    if (!mc || topic >= MSG_COUNT)
        return;

    for (msg_sub_t *s = mc->topics[topic]; s; s = s->next) {
        if (s->handler)
            s->handler(topic, data, s->user);
    }
}

/*
 * Subscribe a handler to a topic; node is prepended to the list.
 */
msg_sub_t *msg_center_subscribe(msg_center_t *mc, msg_topic_t topic,
                                msg_handler_t handler, void *user)
{
    if (!mc || topic >= MSG_COUNT || !handler)
        return NULL;

    msg_sub_t *sub = calloc(1, sizeof(*sub));
    if (!sub)
        return NULL;

    sub->handler = handler;
    sub->user    = user;
    sub->next    = mc->topics[topic];
    mc->topics[topic] = sub;
    return sub;
}

/* Unlink and free a subscription node from the topic's list. */
void msg_center_unsubscribe(msg_center_t *mc, msg_topic_t topic, msg_sub_t *sub)
{
    if (!mc || topic >= MSG_COUNT || !sub)
        return;

    msg_sub_t **pp = &mc->topics[topic];
    while (*pp) {
        if (*pp == sub) {
            *pp = sub->next;
            free(sub);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Destroy the center: free all subscriptions and the center itself. */
void msg_center_destroy(msg_center_t *mc)
{
    if (!mc)
        return;

    for (int i = 0; i < MSG_COUNT; i++) {
        msg_sub_t *s = mc->topics[i];
        while (s) {
            msg_sub_t *next = s->next;
            free(s);
            s = next;
        }
        mc->topics[i] = NULL;
    }
    free(mc);
}
