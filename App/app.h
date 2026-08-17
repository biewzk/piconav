#ifndef APP_H
#define APP_H

#include "Utils/msg_center.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 全局消息中心 (由 app_init 创建) */
msg_center_t *app_msg_center(void);

/* 初始化框架: 消息中心 + 页面管理器 + HAL */
void app_init(void);

/* 主循环 (由平台 main 调用) */
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */