#include "page_manager.h"
#include "log.h"
#include "../Config/app_config.h"

#include <string.h>

static msg_center_t *s_mc;
static page_t *s_stack[PAGE_STACK_MAX];
static int s_depth;
static msg_sub_t *s_key_sub;

/* Dispatch MSG_KEY events to the top page's on_key handler. */
static void page_key_handler(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    (void)user;
    const key_event_t *ev = data;
    page_t *top = page_manager_top();
    LOG_D("dispatch key id=%d press=%d -> top=%s (depth=%d)",
          ev->id, ev->is_press, top ? top->name : "(null)", s_depth);
    if (top && top->ops->on_key)
        top->ops->on_key(top, ev);
}

/*
 * Initialize the page manager: store the message center and subscribe
 * to MSG_KEY so key events are dispatched to the top page.
 */
void page_manager_init(msg_center_t *mc)
{
    s_mc = mc;
    s_depth = 0;
    memset(s_stack, 0, sizeof(s_stack));
    s_key_sub = msg_center_subscribe(mc, MSG_KEY, page_key_handler, NULL);
}

/* Create the page's screen (once) and run its create callback. */
static void page_build(page_t *page)
{
    if (page->scr)
        return;
    page->scr = lv_obj_create(NULL);   /* NULL parent => screen */
    lv_obj_clear_flag(page->scr, LV_OBJ_FLAG_SCROLLABLE);
    if (page->ops->create)
        page->ops->create(page);
}

/*
 * Push a page onto the stack: build it, hide the old page, show the
 * new one and run the slide-in transition.
 */
void page_manager_push(page_t *page)
{
    if (s_depth >= PAGE_STACK_MAX) {
        LOG_W("push rejected: page stack full (%s, depth=%d)", page->name, s_depth);
        return;
    }
    for (int i = 0; i < s_depth; i++) {
        if (s_stack[i] == page) {
            LOG_W("push rejected: page already on stack (%s, depth=%d)", page->name, s_depth);
            return;
        }
    }

    page_t *old = page_manager_top();
    page_build(page);
    s_stack[s_depth++] = page;
    LOG_I("push: %s (depth=%d)", page->name, s_depth);

    if (old && old->ops->on_hide)
        old->ops->on_hide(old);

    if (page->ops->on_show)
        page->ops->on_show(page);

    if (s_depth == 1)
        lv_scr_load(page->scr);
    else
        lv_scr_load_anim(page->scr, LV_SCR_LOAD_ANIM_OVER_LEFT, PAGE_ANIM_TIME, 0, false);
}

/*
 * Replace the top page in place: same depth, fade-in transition.
 */
void page_manager_replace(page_t *page)
{
    if (s_depth == 0) {
        page_manager_push(page);
        return;
    }

    page_t *old = s_stack[s_depth - 1];
    if (old == page) {
        LOG_W("replace rejected: page is already on top (%s)", page->name);
        return;
    }
    page_build(page);
    s_stack[s_depth - 1] = page;
    LOG_I("replace: %s -> %s (depth=%d)", old->name, page->name, s_depth);

    if (old && old->ops->on_hide)
        old->ops->on_hide(old);
    if (page->ops->on_show)
        page->ops->on_show(page);

    lv_scr_load_anim(page->scr, LV_SCR_LOAD_ANIM_FADE_ON, PAGE_ANIM_TIME, 0, true);
    if (old->ops->destroy)
        old->ops->destroy(old);
    old->scr = NULL;
}

/*
 * Pop the top page: hide it, show the new top and run the
 * slide-back transition.
 */
void page_manager_pop(void)
{
    if (s_depth <= 1)
        return;

    page_t *old = s_stack[s_depth - 1];
    s_depth--;
    page_t *new_top = s_stack[s_depth - 1];
    LOG_I("pop: %s -> %s (depth=%d)", old->name, new_top->name, s_depth);

    if (old->ops->on_hide)
        old->ops->on_hide(old);
    if (new_top->ops->on_show)
        new_top->ops->on_show(new_top);

    lv_scr_load_anim(new_top->scr, LV_SCR_LOAD_ANIM_OVER_RIGHT, PAGE_ANIM_TIME, 0, true);
    if (old->ops->destroy)
        old->ops->destroy(old);
    old->scr = NULL;
}

/* Return the current (top) page, or NULL if the stack is empty. */
page_t *page_manager_top(void)
{
    return s_depth > 0 ? s_stack[s_depth - 1] : NULL;
}

/* Return the current page stack depth. */
int page_manager_depth(void)
{
    return s_depth;
}