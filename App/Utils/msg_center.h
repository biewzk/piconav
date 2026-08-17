#ifndef MSG_CENTER_H
#define MSG_CENTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 消息主题表 (固定)
 * 新增主题时在此追加，并同步 MAX */
typedef enum {
    MSG_KEY = 0,       /* key_event_t*: 按键按下/抬起 */
    MSG_GPS,           /* gps_data_t*: 位置/速度更新 */
    MSG_COUNT
} msg_topic_t;

typedef struct msg_sub msg_sub_t;
typedef struct msg_center msg_center_t;

/* 订阅回调 */
typedef void (*msg_handler_t)(msg_topic_t topic, const void *data, void *user);

/* 创建消息中心 (topic 表 + 每个 topic 的订阅者链表) */
msg_center_t *msg_center_create(void);

/* 发布: 遍历该 topic 订阅者链表依次回调 */
void msg_center_publish(msg_center_t *mc, msg_topic_t topic, const void *data);

/* 订阅: 挂到 topic 的链表头; 返回订阅节点 (用于取消) */
msg_sub_t *msg_center_subscribe(msg_center_t *mc, msg_topic_t topic,
                                msg_handler_t handler, void *user);

/* 取消订阅 */
void msg_center_unsubscribe(msg_center_t *mc, msg_topic_t topic, msg_sub_t *sub);

/* 销毁 */
void msg_center_destroy(msg_center_t *mc);

#ifdef __cplusplus
}
#endif

#endif /* MSG_CENTER_H */
