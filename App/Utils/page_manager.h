#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "lvgl.h"
#include "../Utils/msg_center.h"
#include "../../HAL/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_STACK_MAX 8

/* page_t flags */
#define PAGE_FLAG_NO_AUTO_BACK  0x0001u  /* page handles long-ENTER itself */

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
    uint32_t        flags;  /* PAGE_FLAG_* */
    lv_obj_t       *scr;   /* Full-screen container (lv_obj_create(NULL) screen) */
    void           *user;  /* Page private data */
    /* Navigation parameter handed over by page_manager_open(). Borrowed
     * pointer: a page that needs it past create() must copy it into its
     * private data there. Cleared after create() ran. */
    const char     *param;
};

/* ---- page registry (pages are addressed by name) ----
 * Built-ins register in pages_builtin_register(); plugins register the
 * same way from plugin_init(). Duplicate names are logged and ignored. */
void    page_register(page_t *page);
page_t *page_find(const char *name);

/* Page manager (page stack + lifecycle + transitions). */
void page_manager_init(msg_center_t *mc);

/* Push a new page (with transition animation). */
void page_manager_push(page_t *page);

/* Pop the current page back to the previous one (reverse transition). */
void page_manager_pop(void);

/* Replace the top page without changing depth (e.g. Startup -> Dialplate). */
void page_manager_replace(page_t *page);

/* Find a registered page by name, hand over param and push it. Returns
 * the page or NULL when the name is unknown; conflicting pushes (stack
 * full / page already on stack) are rejected as in page_manager_push. */
page_t *page_manager_open(const char *name, const char *param);

/* Current (top) page */
page_t *page_manager_top(void);

/* Stack depth */
int page_manager_depth(void);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_MANAGER_H */