#include "msg_center.h"
#include "../Utils/log.h"

#include <stdlib.h>
#include <string.h>

struct msg_sub {
    msg_handler_t   handler;
    void           *user;
    msg_sub_t      *next;
};

struct msg_center {
    msg_sub_t *topics[MSG_TOPIC_MAX];
    /* Runtime topics: ids MSG_COUNT .. MSG_COUNT + dyn_count - 1. */
    char      *dyn_names[MSG_DYNAMIC_MAX];
    int        dyn_count;
};

/* True when topic is a valid, registered topic of this center. */
static int topic_valid(const msg_center_t *mc, msg_topic_t topic)
{
    return topic >= 0 && topic < MSG_COUNT + mc->dyn_count;
}

/* Create a message center with an empty topic table. */
msg_center_t *msg_center_create(void)
{
    msg_center_t *mc = calloc(1, sizeof(*mc));
    return mc;
}

msg_topic_t msg_topic_register(msg_center_t *mc, const char *name)
{
    if (!mc || !name || !name[0])
        return MSG_TOPIC_INVALID;

    msg_topic_t found = msg_topic_find(mc, name);
    if (found != MSG_TOPIC_INVALID)
        return found;

    if (mc->dyn_count >= MSG_DYNAMIC_MAX) {
        LOG_W("msg center: topic table full, dropped \"%s\"", name);
        return MSG_TOPIC_INVALID;
    }

    char *copy = malloc(strlen(name) + 1);
    if (!copy)
        return MSG_TOPIC_INVALID;
    strcpy(copy, name);

    msg_topic_t id = (msg_topic_t)(MSG_COUNT + mc->dyn_count);
    mc->dyn_names[mc->dyn_count++] = copy;
    LOG_D("msg center: topic \"%s\" = %d", name, (int)id);
    return id;
}

msg_topic_t msg_topic_find(msg_center_t *mc, const char *name)
{
    if (!mc || !name)
        return MSG_TOPIC_INVALID;
    for (int i = 0; i < mc->dyn_count; i++)
        if (strcmp(mc->dyn_names[i], name) == 0)
            return (msg_topic_t)(MSG_COUNT + i);
    return MSG_TOPIC_INVALID;
}

/*
 * Publish an event on a topic: call every subscribed handler in order.
 */
void msg_center_publish(msg_center_t *mc, msg_topic_t topic, const void *data)
{
    if (!mc || !topic_valid(mc, topic))
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
    if (!mc || !topic_valid(mc, topic) || !handler)
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
    if (!mc || !topic_valid(mc, topic) || !sub)
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

    for (int i = 0; i < MSG_TOPIC_MAX; i++) {
        msg_sub_t *s = mc->topics[i];
        while (s) {
            msg_sub_t *next = s->next;
            free(s);
            s = next;
        }
        mc->topics[i] = NULL;
    }
    for (int i = 0; i < mc->dyn_count; i++)
        free(mc->dyn_names[i]);
    free(mc);
}
