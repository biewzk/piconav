#include "page_manager.h"
#include "log.h"
#include "../Config/app_config.h"

#include <string.h>

/*
 * Page host: every page root is a full-screen CONTAINER inside one
 * master LVGL screen (the X-TRACK PageManager architecture), stacked
 * as siblings - child order is the z-order. This is what makes the
 * X-TRACK transitions possible: on push the new page rises from the
 * bottom over the old one, on pop the old page slides down off the
 * screen revealing the page beneath (with one LVGL screen per page
 * the active screen always draws on top, so a sliding-away old page
 * can never be seen).
 */
static msg_center_t *s_mc;
static page_t *s_stack[PAGE_STACK_MAX];
static int s_depth;
static msg_sub_t *s_key_sub;
static lv_obj_t *s_master;
static bool s_switch_busy;   /* a push/pop/replace transition is running */
static void (*s_root_pop_cb)(void);   /* back on the first page */

#define SWITCH_ANIM_TIME PAGE_ANIM_TIME   /* X-TRACK default: 500 ms */

/* ---- page registry ---- */

#define PAGE_REG_MAX 24
static page_t *s_reg[PAGE_REG_MAX];
static int s_reg_n;

void page_register(page_t *page)
{
    if (!page || !page->name || !page->ops) {
        LOG_W("page register: bad page");
        return;
    }
    if (page_find(page->name)) {
        LOG_W("page register: \"%s\" already registered", page->name);
        return;
    }
    if (s_reg_n >= PAGE_REG_MAX) {
        LOG_W("page register: table full, dropped \"%s\"", page->name);
        return;
    }
    s_reg[s_reg_n++] = page;
    LOG_D("page register: \"%s\"", page->name);
}

page_t *page_find(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < s_reg_n; i++)
        if (strcmp(s_reg[i]->name, name) == 0)
            return s_reg[i];
    return NULL;
}

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

/* ENTER has been held past the threshold: by default this pops the top
 * page (the "hold OK = back" gesture); pages that opted out with
 * PAGE_FLAG_NO_AUTO_BACK (e.g. the map zooms out on a held ENTER)
 * receive the long-press event instead. */
static void enter_long_cb(lv_timer_t *timer)
{
    s_enter_timer = NULL;
    lv_timer_del(timer);
    if (!s_enter_down)
        return;

    page_t *top = page_manager_top();
    if (top && !(top->flags & PAGE_FLAG_NO_AUTO_BACK)) {
        LOG_I("auto back: pop from \"%s\" (long ENTER)", top->name);
        page_manager_pop();
        return;
    }

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

    /*
     * A long-ENTER threshold either popped the page (default back
     * gesture, see enter_long_cb) or went to an opt-out page. The
     * follow-up release event ({press=0, long=1}) is forwarded as
     * usual: opt-out pages need it to stop hold-to-repeat, the page
     * that got auto-popped simply ignores it.
     */
    page_deliver(&ev);
}

/*
 * Initialize the page manager: create the master screen that hosts all
 * page containers and subscribe to MSG_KEY so key events are dispatched
 * to the top page.
 */
void page_manager_init(msg_center_t *mc)
{
    s_mc = mc;
    s_depth = 0;
    s_switch_busy = false;
    memset(s_stack, 0, sizeof(s_stack));
    s_master = lv_obj_create(NULL);
    lv_obj_clear_flag(s_master, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_master, lv_color_hex(0x000000), 0);
    lv_scr_load(s_master);
    s_key_sub = msg_center_subscribe(mc, MSG_KEY, page_key_handler, NULL);
}

/* Create the page's full-screen container (once) and run its create
 * callback. The container is the last child of the master screen, i.e.
 * on top of everything already on the stack. */
static void page_build(page_t *page)
{
    if (page->scr)
        return;
    page->scr = lv_obj_create(s_master);
    lv_obj_set_size(page->scr, APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_pos(page->scr, 0, 0);
    lv_obj_clear_flag(page->scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page->scr, 0, 0);
    lv_obj_set_style_pad_all(page->scr, 0, 0);
    lv_obj_set_style_radius(page->scr, 0, 0);
    if (page->ops->create)
        page->ops->create(page);
    /* The nav parameter only has to live until create() consumed it. */
    page->param = NULL;
}

/* Fade the exiting page container out of the tree (pop/replace tail). */
static void switch_anim_y_cb(void *var, int32_t val)
{
    lv_obj_set_y((lv_obj_t *)var, val);
}

static void switch_anim_opa_cb(void *var, int32_t val)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)val, 0);
}

/* Push done: park the covered page (kept cached, hidden for render). */
static void push_anim_ready_cb(lv_anim_t *a)
{
    page_t *old = a->user_data;
    if (old && old->scr)
        lv_obj_add_flag(old->scr, LV_OBJ_FLAG_HIDDEN);
    s_switch_busy = false;
}

/* Pop done: the slid-away page leaves the stack AND the object tree.
 * Replace reuses it - its old page tears down the same way. */
static void pop_anim_ready_cb(lv_anim_t *a)
{
    page_t *old = a->user_data;
    lv_obj_t *scr = old->scr;

    if (old->ops->destroy)
        old->ops->destroy(old);
    old->scr = NULL;
    if (scr)
        lv_obj_del(scr);
    s_switch_busy = false;
}

static void replace_anim_ready_cb(lv_anim_t *a)
{
    pop_anim_ready_cb(a);
}

/*
 * Push a page onto the stack: build it, hide the old page, show the
 * new one and run the X-TRACK push transition - the new page rises
 * from the bottom edge and covers the old one (500 ms ease-out).
 */
void page_manager_push(page_t *page)
{
    if (s_switch_busy) {
        LOG_W("push rejected: switch in progress (%s)", page->name);
        return;
    }
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

    if (s_depth == 1 || !old) {
        /* First page: the master screen is already up, just place it. */
        lv_obj_set_pos(page->scr, 0, 0);
        return;
    }

    /* The old page shows underneath while the new one rises over it. */
    s_switch_busy = true;
    lv_obj_clear_flag(old->scr, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_y(page->scr, APP_SCR_VER);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, page->scr);
    lv_anim_set_values(&a, APP_SCR_VER, 0);
    lv_anim_set_time(&a, SWITCH_ANIM_TIME);
    lv_anim_set_exec_cb(&a, switch_anim_y_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, push_anim_ready_cb);
    lv_anim_set_user_data(&a, old);
    lv_anim_start(&a);
}

/*
 * Replace the top page in place: fade the new page in over the old one
 * (X-TRACK LOAD_ANIM_FADE_ON), then tear the old page down.
 */
void page_manager_replace(page_t *page)
{
    if (s_depth == 0) {
        page_manager_push(page);
        return;
    }
    if (s_switch_busy) {
        LOG_W("replace rejected: switch in progress (%s)", page->name);
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

    if (old->ops->on_hide)
        old->ops->on_hide(old);
    if (page->ops->on_show)
        page->ops->on_show(page);

    s_switch_busy = true;
    lv_obj_set_style_opa(page->scr, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, page->scr);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, SWITCH_ANIM_TIME);
    lv_anim_set_exec_cb(&a, switch_anim_opa_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, replace_anim_ready_cb);
    lv_anim_set_user_data(&a, old);
    lv_anim_start(&a);
}

/*
 * Pop the top page: it slides down off the bottom edge (X-TRACK pop),
 * revealing the page beneath, which is torn down only after the
 * transition so it stays visible while moving.
 */
void page_manager_pop(void)
{
    if (s_depth <= 1) {
        /* Long ENTER on the root page: no page below to reveal. The
         * launcher ignores this; a standalone app ends its loop. */
        if (s_root_pop_cb)
            s_root_pop_cb();
        return;
    }
    if (s_switch_busy) {
        LOG_W("pop rejected: switch in progress");
        return;
    }

    page_t *old = s_stack[s_depth - 1];
    s_depth--;
    page_t *new_top = s_stack[s_depth - 1];
    LOG_I("pop: %s -> %s (depth=%d)", old->name, new_top->name, s_depth);

    if (old->ops->on_hide)
        old->ops->on_hide(old);
    if (new_top->ops->on_show)
        new_top->ops->on_show(new_top);

    s_switch_busy = true;
    lv_obj_clear_flag(new_top->scr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(new_top->scr, 0, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, old->scr);
    lv_anim_set_values(&a, 0, APP_SCR_VER);
    lv_anim_set_time(&a, SWITCH_ANIM_TIME);
    lv_anim_set_exec_cb(&a, switch_anim_y_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, pop_anim_ready_cb);
    lv_anim_set_user_data(&a, old);
    lv_anim_start(&a);
}

/*
 * Find a registered page by name, hand over the navigation parameter and
 * push it. The parameter is borrowed: the target page copies it in its
 * create() callback (page_build clears page->param afterwards).
 */
page_t *page_manager_open(const char *name, const char *param)
{
    page_t *page = page_find(name);
    if (!page) {
        LOG_W("page open: unknown page \"%s\"", name);
        return NULL;
    }
    page->param = param;
    page_manager_push(page);
    return page;
}

/* Return the current (top) page, or NULL if the stack is empty. */
page_t *page_manager_top(void)
{
    return s_depth > 0 ? s_stack[s_depth - 1] : NULL;
}

void page_manager_set_root_pop_cb(void (*cb)(void))
{
    s_root_pop_cb = cb;
}

/* Return the current page stack depth. */
int page_manager_depth(void)
{
    return s_depth;
}