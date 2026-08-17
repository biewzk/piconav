#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "lvgl.h"
#include "../Utils/msg_center.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_STACK_MAX 8

typedef struct page page_t;

typedef struct {
    void (*create)(page_t *self);          /* 构建 UI (self->scr) */
    void (*destroy)(page_t *self);
    void (*on_show)(page_t *self);         /* 成为当前页 */
    void (*on_hide)(page_t *self);
    void (*on_key)(page_t *self, const key_event_t *ev);
} page_ops_t;

struct page {
    const char     *name;
    const page_ops_t *ops;
    lv_obj_t       *scr;   /* 本页全屏容器 (lv_obj_create(NULL) 生成屏幕) */
    void           *user;  /* 页面私有数据 */
};

/* 页面管理器 (页面栈 + 生命周期 + 转场) */
void page_manager_init(msg_center_t *mc);

/* 压入新页面 (带转场动画) */
void page_manager_push(page_t *page);

/* 弹出当前页面, 回到上一个 (带反向转场) */
void page_manager_pop(void);

/* 用新页面替换栈顶 (不增加深度, 如 Startup -> Dialplate) */
void page_manager_replace(page_t *page);

/* 当前页面 */
page_t *page_manager_top(void);

/* 栈深度 */
int page_manager_depth(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_MANAGER_H */