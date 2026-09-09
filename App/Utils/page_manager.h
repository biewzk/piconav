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
    void (*create)(page_t *self);          /* Build the UI (self->scr) */
    void (*destroy)(page_t *self);
    void (*on_show)(page_t *self);         /* Page becomes current */
    void (*on_hide)(page_t *self);
    void (*on_key)(page_t *self, const key_event_t *ev);
} page_ops_t;

struct page {
    const char     *name;
    const page_ops_t *ops;
    lv_obj_t       *scr;   /* Full-screen container (lv_obj_create(NULL) screen) */
    void           *user;  /* Page private data */
};

/* Page manager (page stack + lifecycle + transitions). */
void page_manager_init(msg_center_t *mc);

/* Push a new page (with transition animation). */
void page_manager_push(page_t *page);

/* Pop the current page back to the previous one (reverse transition). */
void page_manager_pop(void);

/* Replace the top page without changing depth (e.g. Startup -> Dialplate). */
void page_manager_replace(page_t *page);

/* Current (top) page */
page_t *page_manager_top(void);

/* Stack depth */
int page_manager_depth(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_MANAGER_H */