#include "page_mapselect.h"
#include "../Config/app_config.h"
#include "../Utils/config_api.h"
#include "../Utils/page_manager.h"
#include "../Utils/log.h"
#include "../app.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAPSEL_MAX      16
#define MAPSEL_PATH_SZ  256
#define MAPSEL_NAME_SZ  64
#define MAPSEL_TITLE_H  40
#define MAPSEL_ITEM_H   34

typedef struct {
    char      paths[MAPSEL_MAX][MAPSEL_PATH_SZ];
    char      names[MAPSEL_MAX][MAPSEL_NAME_SZ];
    int       count;
    int       sel;
    lv_obj_t *list;                    /* scrollable item container */
    lv_obj_t *labels[MAPSEL_MAX];
    int       cfg_inited;              /* config_init/deinit pairing */
} mapselect_priv_t;

/*
 * Collect the .map files of dir (case-insensitive suffix), sorted by
 * name so the list is stable between runs. Returns the entry count,
 * or -1 when the directory cannot be opened.
 */
static int mapselect_scan(mapselect_priv_t *d, const char *dir)
{
    DIR *dp = opendir(dir);
    struct dirent *ent;

    if (!dp)
        return -1;

    d->count = 0;
    while ((ent = readdir(dp)) != NULL && d->count < MAPSEL_MAX) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcasecmp(dot, ".map") != 0)
            continue;
        if (snprintf(d->paths[d->count], MAPSEL_PATH_SZ, "%s/%s",
                     dir, ent->d_name) >= MAPSEL_PATH_SZ)
            continue;
        snprintf(d->names[d->count], MAPSEL_NAME_SZ, "%s", ent->d_name);
        d->count++;
    }
    closedir(dp);

    /* Insertion sort by name (case-insensitive). */
    for (int i = 1; i < d->count; i++) {
        char ppath[MAPSEL_PATH_SZ], pname[MAPSEL_NAME_SZ];
        int j = i;
        memcpy(ppath, d->paths[i], sizeof(ppath));
        memcpy(pname, d->names[i], sizeof(pname));
        while (j > 0 && strcasecmp(d->names[j - 1], pname) > 0) {
            memcpy(d->paths[j], d->paths[j - 1], sizeof(ppath));
            memcpy(d->names[j], d->names[j - 1], sizeof(pname));
            j--;
        }
        memcpy(d->paths[j], ppath, sizeof(ppath));
        memcpy(d->names[j], pname, sizeof(pname));
    }
    return d->count;
}

/* Repaint the selection highlight and keep it scrolled into view. */
static void mapselect_paint(mapselect_priv_t *d)
{
    for (int i = 0; i < d->count; i++) {
        int on = (i == d->sel);
        lv_obj_set_style_text_color(d->labels[i],
            on ? lv_color_hex(0x00c2ff) : lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_bg_color(d->labels[i],
            on ? lv_color_hex(0x223344) : lv_color_hex(0x0f0f1a), 0);
        lv_obj_set_style_bg_opa(d->labels[i],
            on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (d->count > 0)
        lv_obj_scroll_to_view(d->labels[d->sel], LV_ANIM_ON);
}

/*
 * Persist the choice, publish it on the runtime "map.selected" topic
 * (the map panel swaps the file live) and pop back. The map page itself
 * is a home carousel panel - not opened from here. A failed persist
 * (e.g. no /dev/config in the simulator) only logs: the picked map
 * still applies for this session.
 */
static void mapselect_open(mapselect_priv_t *d)
{
    const char *path = d->paths[d->sel];

    if (config_set_str(APP_CFG_KEY_MAP_SELECTED, path) == 0 && config_save() == 0)
        msg_center_publish(app_msg_center(), MSG_CONFIG_CHANGED, NULL);
    else
        LOG_W("mapselect: persist selection failed");

    msg_topic_t t = app_topic_map_selected();
    if (t != MSG_TOPIC_INVALID)
        msg_center_publish(app_msg_center(), t, path);

    LOG_I("mapselect: selected %s", path);
    page_manager_pop();
}

/*
 * Build the picker page: title, scrollable map list (or an empty-dir
 * notice with the scanned path) and a key hint line.
 */
static void mapselect_create(page_t *self)
{
    mapselect_priv_t *d = calloc(1, sizeof(*d));
    self->user = d;

    lv_obj_t *scr = self->scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Select Map");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00c2ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    d->list = lv_obj_create(scr);
    lv_obj_set_size(d->list, APP_SCR_HOR, APP_SCR_VER - MAPSEL_TITLE_H - 26);
    lv_obj_align(d->list, LV_ALIGN_TOP_MID, 0, MAPSEL_TITLE_H);
    lv_obj_set_style_bg_color(d->list, lv_color_hex(0x0f0f1a), 0);
    lv_obj_set_style_border_width(d->list, 0, 0);
    lv_obj_set_style_pad_all(d->list, 0, 0);

    const char *dir = getenv("MF_MAP_DIR");
    if (!dir || !dir[0])
        dir = APP_MAP_DIR_DEFAULT;

    int n = mapselect_scan(d, dir);
    if (n < 0) {
        lv_obj_t *msg = lv_label_create(scr);
        lv_label_set_text_fmt(msg, "Cannot open\n%s", dir);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(msg, lv_color_hex(0xff6666), 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, MAPSEL_TITLE_H + 40);
        LOG_W("mapselect: cannot open %s", dir);
    } else if (n == 0) {
        lv_obj_t *msg = lv_label_create(scr);
        lv_label_set_text_fmt(msg, "No .map files in\n%s", dir);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(msg, lv_color_hex(0xff6666), 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, MAPSEL_TITLE_H + 40);
        LOG_I("mapselect: no maps in %s", dir);
    } else {
        for (int i = 0; i < d->count; i++) {
            lv_obj_t *it = lv_label_create(d->list);
            lv_label_set_text(it, d->names[i]);
            lv_obj_set_style_text_font(it, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(it, lv_color_hex(0xdddddd), 0);
            lv_obj_set_style_pad_all(it, 6, 0);
            lv_obj_set_style_radius(it, 4, 0);
            lv_obj_align(it, LV_ALIGN_TOP_LEFT, 4, i * MAPSEL_ITEM_H);
            lv_obj_set_width(it, APP_SCR_HOR - 12);
            lv_label_set_long_mode(it, LV_LABEL_LONG_DOT);
            d->labels[i] = it;
        }
        LOG_I("mapselect: %d map(s) in %s", d->count, dir);
    }

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "OK:Open HOLD OK:Back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void mapselect_destroy(page_t *self)
{
    free(self->user);
    self->user = NULL;
}

/*
 * Load config on show and pre-select the persisted map (falls back to
 * the first entry when the stored path is gone).
 */
static void mapselect_on_show(page_t *self)
{
    mapselect_priv_t *d = self->user;
    char sel[MAPSEL_PATH_SZ];

    if (!d || d->count == 0)
        return;

    config_init();
    d->cfg_inited = 1;

    config_get_str(APP_CFG_KEY_MAP_SELECTED, sel, sizeof(sel), "");
    d->sel = 0;
    if (sel[0]) {
        for (int i = 0; i < d->count; i++) {
            if (strcmp(d->paths[i], sel) == 0) {
                d->sel = i;
                break;
            }
        }
    }
    mapselect_paint(d);
}

static void mapselect_on_hide(page_t *self)
{
    mapselect_priv_t *d = self->user;

    if (d && d->cfg_inited) {
        config_deinit();
        d->cfg_inited = 0;
    }
}

/*
 * Key handling: UP/DOWN move the selection, ENTER opens the selected
 * map; holding ENTER pops via the page manager's default back gesture,
 * LEFT/BACK pop directly.
 */
static void mapselect_on_key(page_t *self, const key_event_t *ev)
{
    if (!ev->is_press)
        return;

    mapselect_priv_t *d = self->user;
    switch (ev->id) {
    case KEY_BTN_UP:
        if (d->count > 0) {
            d->sel = (d->sel - 1 + d->count) % d->count;
            mapselect_paint(d);
        }
        break;
    case KEY_BTN_DOWN:
        if (d->count > 0) {
            d->sel = (d->sel + 1) % d->count;
            mapselect_paint(d);
        }
        break;
    case KEY_BTN_ENTER:
        if (!ev->is_long && d->count > 0)
            mapselect_open(d);
        break;
    case KEY_BTN_LEFT:
    case KEY_BTN_BACK:
        page_manager_pop();
        break;
    default:
        break;
    }
}

page_t page_mapselect = {
    .name = "mapselect",
    .ops  = &(page_ops_t){
        .create  = mapselect_create,
        .destroy = mapselect_destroy,
        .on_show = mapselect_on_show,
        .on_hide = mapselect_on_hide,
        .on_key  = mapselect_on_key,
    },
};
