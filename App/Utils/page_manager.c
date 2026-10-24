#include "page_manager.h"
#include "log.h"
#include "../Config/app_config.h"

#include <string.h>

static msg_center_t *s_mc;
static page_t *s_stack[PAGE_STACK_MAX];
static int s_depth;
static msg_sub_t *s_key_sub;

/*
 * ENTER gesture layer: the hardware has only five keys (up/down/left/
 * right/enter), so ENTER doubles as the "back"/secondary key via a
 * long press. The raw press is withheld; pages receive:
 *   {ENTER, press=1, long=0}  - short click (delivered on release)
 *   {ENTER, press=1, long=1}  - long press (at the threshold)
 *   {ENTER, press=0, long=1}  - end of a long press (stop repeats)
 * Arrows and BACK keep raw press/release semantics (pages implement
 * hold-to-repeat themselves, like the map pan).
 */
#define ENTER_LONGPRESS_MS 500

static lv_timer_t *s_enter_timer;   /* long-press detection timer */
static int s_enter_down;            /* ENTER currently held */

/* Forward a key event to the top page's on_key handler. */
static void page_deliver(const key_event_t *ev)
{
    page_t *top = page_manager_top();
    LOG_D("dispatch key id=%d press=%d long=%d -> top=%s (depth=%d)",
          ev->id, ev->is_press, ev->is_long, top ? top->name : "(null)", s_depth);
    if (top && top->ops->on_key)
        top->ops->on_key(top, ev);
}

/* ENTER has been held past the threshold: deliver the long-press gesture. */
static void enter_long_cb(lv_timer_t *timer)
{
    s_enter_timer = NULL;
    lv_timer_del(timer);
    if (!s_enter_down)
        return;

    key_event_t ev = { .id = KEY_BTN_ENTER, .is_press = 1, .is_long = 1 };
    page_deliver(&ev);
}

/* Dispatch MSG_KEY events to the top page's on_key handler. */
static void page_key_handler(msg_topic_t topic, const void *data, void *user)
{
    (void)topic;
    (void)user;

    key_event_t ev = *(const key_event_t *)data;

    if (ev.id == KEY_BTN_ENTER && !ev.is_long) {
        if (ev.is_press) {
            /* Withhold the raw press; start/restart the long-press timer. */
            s_enter_down = 1;
            if (s_enter_timer)
                lv_timer_reset(s_enter_timer);
            else
                s_enter_timer = lv_timer_create(enter_long_cb,
                                                ENTER_LONGPRESS_MS, NULL);
        } else {
            s_enter_down = 0;
            if (s_enter_timer) {
                /* Released before the threshold: this was a short click. */
                lv_timer_del(s_enter_timer);
                s_enter_timer = NULL;
                ev.is_press = 1;
            } else {
                /* Release ending a long press (stops hold-to-repeat). */
                ev.is_long = 1;
            }
            page_deliver(&ev);
        }
        return;
    }

    page_deliver(&ev);
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