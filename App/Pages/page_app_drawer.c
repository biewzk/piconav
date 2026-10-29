#include "page_app_drawer.h"
#include "app_registry.h"
#include "../Config/app_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_app_16);

/*
 * App drawer, X-TRACK SystemInfos style: a vertical rolling list. The
 * root is a flex column with pad = (screen - item) / 2 so the focused
 * item scrolls to the middle while its neighbours peek above and below.
 * Every item is a full-width BLACK icon box (app tile + name, flex
 * column) laid OVER grey info / white data labels; focusing an item
 * animates its icon box width 220 -> 70 (overshoot), revealing the
 * data column and an orange right-edge accent. Focus moves with
 * UP/DOWN and the list scrolls (LVGL scroll anim) to keep it centered.
 *
 * The list is fully registry-driven: entries come from pages_builtin
 * (core apps) and plugin_init() (plugins), and each entry carries its
 * own preview columns (app_entry_t info/data/data_get). The drawer
 * only refreshes the data_get callbacks once a second while visible.
 *
 * Names are CJK (lv_font_app_16); info/data columns are ASCII, matching
 * the X-TRACK reference.
 */

#define ITEM_H        130
#define ITEM_PAD      ((APP_SCR_VER - ITEM_H) / 2)   /* 95 */
#define ITEM_W        220
#define ICON_W_FULL   220    /* covers the data column of idle items */
#define ICON_W_FOCUS  70     /* reveals info + data on the focused item */
#define INFO_X        76
#define DATA_X        150

#define ACCENT        0xff931e   /* X-TRACK focus edge */
#define C_LIST_BG     0x000000
#define C_INFO        0x999999
#define C_DATA        0xffffff

#define MAX_ITEMS 16

static lv_obj_t *s_root;         /* scrollable flex column */
static lv_obj_t *s_items[MAX_ITEMS];
static lv_obj_t *s_icons[MAX_ITEMS];
static lv_obj_t *s_data[MAX_ITEMS];
static lv_style_t s_style_icon;  /* full-width black box, shared */
static lv_style_t s_style_focus; /* focused: narrow + orange edge */
static lv_style_t s_style_info;
static lv_style_t s_style_data;
static int s_count;
static int s_sel;
static lv_timer_t *s_timer;
static int s_visible;

static void data_refresh(void);

/* ---------- shared styles (initialized once) ---------- */

static void styles_init(void)
{
    static bool done;

    if (done)
        return;
    done = true;

    /* idle icon box: covers the whole item (hides the data column) */
    lv_style_init(&s_style_icon);
    lv_style_set_width(&s_style_icon, ICON_W_FULL);
    lv_style_set_bg_color(&s_style_icon, lv_color_black());
    lv_style_set_bg_opa(&s_style_icon, LV_OPA_COVER);
    lv_style_set_text_font(&s_style_icon, &lv_font_app_16);
    lv_style_set_text_color(&s_style_icon, lv_color_white());

    /* focused icon box: shrinks, orange accent on the right edge */
    lv_style_init(&s_style_focus);
    lv_style_set_width(&s_style_focus, ICON_W_FOCUS);
    lv_style_set_border_side(&s_style_focus, LV_BORDER_SIDE_RIGHT);
    lv_style_set_border_width(&s_style_focus, 2);
    lv_style_set_border_color(&s_style_focus, lv_color_hex(ACCENT));

    /* width 220 <-> 70 with the X-TRACK overshoot bounce */
    static const lv_style_prop_t trans_props[] = {
        LV_STYLE_WIDTH,
        LV_STYLE_PROP_INV,
    };
    static lv_style_transition_dsc_t trans;
    lv_style_transition_dsc_init(&trans, trans_props,
                                 lv_anim_path_overshoot, 200, 0, NULL);
    lv_style_set_transition(&s_style_focus, &trans);
    lv_style_set_transition(&s_style_icon, &trans);

    lv_style_init(&s_style_info);
    lv_style_set_text_font(&s_style_info, &lv_font_app_16);
    lv_style_set_text_color(&s_style_info, lv_color_hex(C_INFO));

    lv_style_init(&s_style_data);
    lv_style_set_text_font(&s_style_data, &lv_font_app_16);
    lv_style_set_text_color(&s_style_data, lv_color_hex(C_DATA));
}

/* ---------- live data columns ---------- */

/*
 * Poll every entry's data_get callback (about 1 Hz while visible).
 * Entries without a callback keep their static data column; the
 * callbacks themselves live with the entries (core: pages_builtin,
 * plugins: plugin_init), so the drawer knows nothing about trip or
 * GPS semantics.
 */
/* Read the preview file a standalone app publishes (fixed format: a
 * few ASCII lines, see app_runtime.h). Missing/empty file keeps the
 * static manifest text. */
static void preview_read(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f)
        return;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    if (!buf[0])
        buf[0] = 0;
}

static void data_refresh(void)
{
    char buf[64];
    char pbuf[96];

    for (int i = 0; i < s_count && i < MAX_ITEMS; i++) {
        const app_entry_t *app = app_registry_get(i);

        if (!app)
            continue;
        if (app->data_get) {
            buf[0] = '\0';
            app->data_get(buf, sizeof(buf));
            lv_label_set_text(s_data[i], buf);
        } else if (app->preview) {
            pbuf[0] = '\0';
            preview_read(app->preview, pbuf, sizeof(pbuf));
            if (pbuf[0])
                lv_label_set_text(s_data[i], pbuf);
        }
    }
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_visible)
        data_refresh();
}

/* ---------- item builder ---------- */

/*
 * One list item: full-width black icon box (tile + name) over the grey
 * info / white data labels. The icon box stays the topmost child;
 * focusing shrinks it and uncovers the columns (X-TRACK's trick).
 */
static void item_build(int i, const app_entry_t *app)
{
    lv_obj_t *cont = lv_obj_create(s_root);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, ITEM_W, ITEM_H);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    s_items[i] = cont;

    /* icon box */
    lv_obj_t *icon = lv_obj_create(cont);
    lv_obj_remove_style_all(icon);
    lv_obj_add_style(icon, &s_style_icon, 0);
    lv_obj_add_style(icon, &s_style_focus, LV_STATE_FOCUSED);
    lv_obj_set_style_align(icon, LV_ALIGN_LEFT_MID, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(icon, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(icon, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_icons[i] = icon;

    if (app->img) {
        lv_obj_t *img = lv_img_create(icon);
        lv_img_set_src(img, app->img);
    } else {
        /* letter-glyph fallback for plugins without an icon image */
        lv_obj_t *g = lv_label_create(icon);
        lv_label_set_text(g, app->icon ? app->icon : "?");
        lv_obj_set_style_text_font(g, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(g, lv_color_hex(app->icon_color), 0);
    }

    lv_obj_t *lbl = lv_label_create(icon);
    lv_label_set_text(lbl, app->name);

    /* info + data columns (revealed on focus; content comes from the
     * entry itself) */
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, app->info ? app->info : "");
    lv_obj_add_style(lbl, &s_style_info, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, INFO_X, 0);

    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, app->data ? app->data : "-");
    lv_obj_add_style(lbl, &s_style_data, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, DATA_X, 0);
    s_data[i] = lbl;

    lv_obj_move_foreground(icon);
}

/* ---------- public API ---------- */

void app_drawer_create(lv_obj_t *parent)
{
    styles_init();

    lv_obj_set_style_bg_color(parent, lv_color_hex(C_LIST_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* rolling list root: the pad centers the focused item */
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, APP_SCR_HOR, APP_SCR_VER);
    lv_obj_set_style_pad_ver(s_root, ITEM_PAD, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_root, LV_SCROLLBAR_MODE_OFF);

    s_count = app_registry_count();
    if (s_count > MAX_ITEMS)
        s_count = MAX_ITEMS;
    for (int i = 0; i < s_count; i++)
        item_build(i, app_registry_get(i));

    s_sel = 0;
    if (s_count > 0)
        lv_obj_add_state(s_icons[0], LV_STATE_FOCUSED);

    s_timer = lv_timer_create(timer_cb, 1000, NULL);
    data_refresh();
}

void app_drawer_set_sel(int sel)
{
    if (s_count == 0)
        return;
    if (sel < 0)
        sel = s_count - 1;
    if (sel >= s_count)
        sel = 0;
    if (sel == s_sel)
        return;

    lv_obj_clear_state(s_icons[s_sel], LV_STATE_FOCUSED);
    s_sel = sel;
    lv_obj_add_state(s_icons[s_sel], LV_STATE_FOCUSED);

    /* Scroll the item to center: child y is content-relative, so
     * scrolling by it lands the item at the pad offset (= center).
     * Same formula as the X-TRACK reference. */
    lv_obj_scroll_to_y(s_root, lv_obj_get_y(s_items[s_sel]), LV_ANIM_ON);
}

int app_drawer_get_sel(void)
{
    return s_sel;
}

/* Called by home when the drawer panel becomes visible / hidden. */
void app_drawer_set_visible(int on)
{
    s_visible = on ? 1 : 0;
    if (on && s_root && s_count > 0) {
        /* re-assert focus + position (e.g. after returning from an app) */
        for (int i = 0; i < s_count && i < MAX_ITEMS; i++)
            lv_obj_clear_state(s_icons[i], LV_STATE_FOCUSED);
        lv_obj_add_state(s_icons[s_sel], LV_STATE_FOCUSED);
        lv_obj_scroll_to_y(s_root, lv_obj_get_y(s_items[s_sel]),
                           LV_ANIM_OFF);
        data_refresh();
    }
}

/* ---------- hot install ---------- */

/*
 * Rebuild the item list after the plugin manager hot-loaded new apps.
 * The registry is append-only, so existing items could be kept, but a
 * full rebuild is simpler and guarantees fresh pointers (item widgets
 * cache s_data labels for the 1 Hz refresh). No-op before the drawer
 * exists (the create path builds everything from the registry).
 */
void app_drawer_refresh(void)
{
    if (!s_root)
        return;

    lv_obj_clean(s_root);
    memset(s_items, 0, sizeof s_items);
    memset(s_icons, 0, sizeof s_icons);
    memset(s_data, 0, sizeof s_data);

    s_count = app_registry_count();
    if (s_count > MAX_ITEMS)
        s_count = MAX_ITEMS;
    for (int i = 0; i < s_count; i++)
        item_build(i, app_registry_get(i));

    /* Fresh children have no layout yet: without this, get_y() below
     * returns stale/zero positions and the list stays scrolled to the
     * top instead of centering the selected item. */
    lv_obj_update_layout(s_root);

    if (s_sel >= s_count)
        s_sel = s_count > 0 ? s_count - 1 : 0;
    if (s_count > 0) {
        lv_obj_add_state(s_icons[s_sel], LV_STATE_FOCUSED);
        lv_obj_scroll_to_y(s_root, lv_obj_get_y(s_items[s_sel]),
                           LV_ANIM_OFF);
    }
    data_refresh();
}
