#include "mf_theme.h"

#include <string.h>

/* Palette macros (C_*) live in mf_theme.h so that the renderer shares
 * the exact same color definitions. */

/* ------------------------------------------------------------------ */
/* Tag matching helpers                                                */
/* ------------------------------------------------------------------ */

/* tag: "key=value"; match key exactly, and value exactly if value != NULL */
static bool tag_match(const char *tag, const char *key, const char *value)
{
    if (!tag)
        return false;
    size_t kl = strlen(key);
    if (strncmp(tag, key, kl) != 0 || tag[kl] != '=')
        return false;
    if (value)
        return strcmp(tag + kl + 1, value) == 0;
    return true;
}

static bool obj_has_tag(const mf_map_t *m, const mf_obj_t *o, const char *key, const char *value)
{
    for (int i = 0; i < o->tag_cnt; i++) {
        const char *tag = (o->type == MF_OBJ_POI)
            ? mf_poi_tag(m, o->tag_ids[i])
            : mf_way_tag(m, o->tag_ids[i]);
        if (tag_match(tag, key, value))
            return true;
    }
    return false;
}

bool mf_obj_has_tag(const mf_map_t *m, const mf_obj_t *o, const char *key, const char *value)
{
    return obj_has_tag(m, o, key, value);
}

/* Match value against a NULL-terminated list of values */
static bool obj_has_tag_in(const mf_map_t *m, const mf_obj_t *o, const char *key,
                           const char *const *values)
{
    for (int i = 0; i < o->tag_cnt; i++) {
        const char *tag = (o->type == MF_OBJ_POI)
            ? mf_poi_tag(m, o->tag_ids[i])
            : mf_way_tag(m, o->tag_ids[i]);
        if (!tag)
            continue;
        size_t kl = strlen(key);
        if (strncmp(tag, key, kl) != 0 || tag[kl] != '=')
            continue;
        const char *v = tag + kl + 1;
        for (int j = 0; values[j]; j++)
            if (strcmp(v, values[j]) == 0)
                return true;
    }
    return false;
}

static void sty(mf_style_t *s, mf_group_t g, int32_t rank, int32_t min_zoom)
{
    memset(s, 0, sizeof(*s));
    s->group = g;
    s->rank = rank;
    s->min_zoom = min_zoom;
    s->label_zoom = -1;
    s->label_max_zoom = 99;
}

static void sty_area(mf_style_t *s, mf_group_t g, int32_t rank, uint32_t fill,
                     uint32_t outline, int32_t min_zoom)
{
    sty(s, g, rank, min_zoom);
    s->fill = fill;
    s->outline = outline;
    s->is_area = true;
}

static void sty_line(mf_style_t *s, int32_t rank, uint32_t line, uint32_t casing,
                     int32_t width_q4, int32_t min_zoom)
{
    sty(s, MF_G_LINE, rank, min_zoom);
    s->line = line;
    s->casing = casing;
    s->width_q4 = width_q4;
}

/* ------------------------------------------------------------------ */
/* Way classification                                                  */
/* ------------------------------------------------------------------ */

static const char *const k_green_forest[] = { "forest", NULL };
static const char *const k_green_park[]   = { "park", "garden", "dog_park", NULL };
static const char *const k_green_grass[]  = { "grass", "meadow", "greenfield", NULL };
static const char *const k_green_sport[]  = { "pitch", "playground", "stadium", "sports_centre", NULL };
static const char *const k_landuse_res[]  = { "residential", NULL };
static const char *const k_landuse_com[]  = { "commercial", "retail", NULL };
static const char *const k_landuse_ind[]  = { "industrial", "railway", NULL };
static const char *const k_landuse_con[]  = { "construction", "brownfield", NULL };

static void classify_way(const mf_map_t *m, const mf_obj_t *o, mf_style_t *s)
{
    sty(s, MF_G_UNDEF, 0, 0);

    /* --- water polygons ---
     * rank 0: sea tile rects (crude, drawn before the coastline land fill)
     * rank 1: nosea land-claim polygons (drawn after the coastline fill)
     * rank 2: inland water / lakes (drawn after the coastline fill) */
    if (obj_has_tag(m, o, "natural", "sea") || obj_has_tag(m, o, "natural", "bay")) {
        sty_area(s, MF_G_WATER, 0, C_WATER, 0, 0);
        return;
    }
    /* coastline ways themselves are rendered by the dedicated coastline pass */
    if (obj_has_tag(m, o, "natural", "coastline"))
        return;
    /* land claiming polygons (natural=nosea): paint with land color */
    if (obj_has_tag(m, o, "natural", "nosea")) {
        sty_area(s, MF_G_WATER, 1, C_LAND_BG, 0, 0);
        return;
    }
    if (obj_has_tag(m, o, "natural", "water") ||
        obj_has_tag(m, o, "landuse", "reservoir") ||
        obj_has_tag(m, o, "landuse", "basin")) {
        sty_area(s, MF_G_WATER, 2, C_WATER, 0, 0);
        return;
    }

    /* --- green areas --- */
    if (obj_has_tag_in(m, o, "landuse", k_green_forest) ||
        obj_has_tag(m, o, "natural", "wood")) {
        sty_area(s, MF_G_GREEN, 0, C_FOREST, 0, 10);
        return;
    }
    if (obj_has_tag_in(m, o, "leisure", k_green_park) || obj_has_tag(m, o, "tourism", "zoo")) {
        sty_area(s, MF_G_GREEN, 1, C_PARK, 0, 12);
        s->label_zoom = 14; s->label_color = C_LABEL_AREA; s->label_size = 10;
        return;
    }
    if (obj_has_tag_in(m, o, "landuse", k_green_grass) ||
        obj_has_tag(m, o, "landuse", "recreation_ground") ||
        obj_has_tag(m, o, "leisure", "common")) {
        sty_area(s, MF_G_GREEN, 2, C_GRASS, 0, 13);
        return;
    }
    if (obj_has_tag(m, o, "natural", "scrub") || obj_has_tag(m, o, "natural", "heath")) {
        sty_area(s, MF_G_GREEN, 2, C_SCRUB, 0, 13);
        return;
    }
    if (obj_has_tag(m, o, "natural", "beach") || obj_has_tag(m, o, "natural", "sand")) {
        sty_area(s, MF_G_GREEN, 3, C_BEACH, 0, 13);
        return;
    }
    if (obj_has_tag(m, o, "landuse", "cemetery")) {
        sty_area(s, MF_G_GREEN, 4, C_CEMETERY, 0, 13);
        return;
    }
    if (obj_has_tag_in(m, o, "leisure", k_green_sport)) {
        sty_area(s, MF_G_GREEN, 5, C_PITCH, 0, 14);
        return;
    }
    if (obj_has_tag(m, o, "leisure", "swimming_pool")) {
        sty_area(s, MF_G_GREEN, 6, C_WATER, 0, 15);
        return;
    }

    /* --- built-up / landuse areas --- */
    if (obj_has_tag_in(m, o, "landuse", k_landuse_res)) {
        sty_area(s, MF_G_LANDUSE, 0, C_RESIDENTIAL, 0, 11);
        return;
    }
    if (obj_has_tag_in(m, o, "landuse", k_landuse_com)) {
        sty_area(s, MF_G_LANDUSE, 1, C_COMMERCIAL, 0, 12);
        return;
    }
    if (obj_has_tag_in(m, o, "landuse", k_landuse_ind)) {
        sty_area(s, MF_G_LANDUSE, 2, C_INDUSTRIAL, 0, 12);
        return;
    }
    if (obj_has_tag_in(m, o, "landuse", k_landuse_con)) {
        sty_area(s, MF_G_LANDUSE, 3, C_CONSTRUCT, 0, 12);
        return;
    }
    if (obj_has_tag(m, o, "amenity", "parking") ||
        obj_has_tag(m, o, "amenity", "motorcycle_parking")) {
        sty_area(s, MF_G_LANDUSE, 4, C_PARKING, 0, 13);
        return;
    }
    if (obj_has_tag(m, o, "man_made", "pier") || obj_has_tag(m, o, "military", "barracks")) {
        sty_area(s, MF_G_LANDUSE, 5, C_PIER, 0, 13);
        return;
    }

    /* --- institutions / tourism areas (before generic buildings) --- */
    if (obj_has_tag(m, o, "amenity", "hospital")) {
        sty_area(s, MF_G_LANDUSE, 6, 0xf2e2e2, 0, 13);
        return;
    }
    if (obj_has_tag(m, o, "amenity", "school") || obj_has_tag(m, o, "amenity", "university") ||
        obj_has_tag(m, o, "amenity", "kindergarten") || obj_has_tag(m, o, "amenity", "college")) {
        sty_area(s, MF_G_LANDUSE, 7, 0xefe8d4, 0, 13);
        return;
    }

    /* --- buildings --- */
    if (obj_has_tag(m, o, "building", NULL) && !obj_has_tag(m, o, "building", "no")) {
        sty_area(s, MF_G_BUILDING, 0, C_BUILDING, C_BUILDING_O, 14);
        return;
    }
    if (obj_has_tag(m, o, "aeroway", "terminal") || obj_has_tag(m, o, "tourism", "hotel") ||
        obj_has_tag(m, o, "tourism", "museum") || obj_has_tag(m, o, "historic", "castle")) {
        sty_area(s, MF_G_BUILDING, 0, C_BUILDING, C_BUILDING_O, 13);
        return;
    }

    /* --- pedestrian squares (closed ways tagged as area) --- */
    if (obj_has_tag(m, o, "area", "yes") &&
        (obj_has_tag(m, o, "highway", "pedestrian") || obj_has_tag(m, o, "highway", "footway"))) {
        sty_area(s, MF_G_PEDAREA, 0, C_PEDAREA, C_PEDAREA_O, 14);
        return;
    }

    /* --- waterways --- */
    if (obj_has_tag(m, o, "waterway", NULL)) {
        sty_line(s, 5, C_WATER, 0, 32, 12);
        return;
    }

    /* --- railways --- */
    if (obj_has_tag(m, o, "railway", "rail")) {
        sty_line(s, 35, C_RAIL, 0, 24, 11);
        return;
    }
    if (obj_has_tag(m, o, "railway", NULL))
        return; /* abandoned/platform/light_rail: skip */

    /* --- barriers --- */
    if (obj_has_tag(m, o, "barrier", NULL)) {
        sty_line(s, 8, C_BARRIER, 0, 16, 15);
        return;
    }

    /* --- roads (rank: higher = more important, drawn later) --- */
    static const struct { const char *v; int32_t rank; uint32_t line, casing; int32_t w_q4; int32_t min_z; } roads[] = {
        { "motorway",       80, C_MOTORWAY,  C_MOTORWAY_C,  96, 5 },
        { "motorway_link",  79, C_MOTORWAY,  C_MOTORWAY_C,  64, 10 },
        { "trunk",          70, C_MOTORWAY,  C_MOTORWAY_C,  88, 6 },
        { "trunk_link",     69, C_MOTORWAY,  C_MOTORWAY_C,  64, 10 },
        { "primary",        60, C_PRIMARY,   C_PRIMARY_C,   80, 7 },
        { "primary_link",   59, C_PRIMARY,   C_PRIMARY_C,   56, 10 },
        { "secondary",      50, C_SECONDARY, C_SECONDARY_C, 72, 10 },
        { "secondary_link", 49, C_SECONDARY, C_SECONDARY_C, 48, 12 },
        { "tertiary",       40, C_ROAD,      C_ROAD_C,      64, 12 },
        { "tertiary_link",  39, C_ROAD,      C_ROAD_C,      48, 13 },
        { "unclassified",   30, C_ROAD,      C_ROAD_C,      48, 13 },
        { "residential",    30, C_ROAD,      C_ROAD_C,      48, 13 },
        { "living_street",  25, C_ROAD,      C_ROAD_C,      48, 14 },
        { "service",        20, C_ROAD,      C_SERVICE_C,   40, 14 },
        { "pedestrian",     20, C_ROAD,      C_SERVICE_C,   48, 14 },
        { "raceway",        18, C_RACEWAY,   C_RACEWAY_C,   64, 11 },
        { "track",          15, C_FOOTWAY,   0,             19, 15 },
        { "construction",   15, 0xd0d0d0,    0,             40, 14 },
        { "footway",        10, C_FOOTWAY,   0,             19, 15 },
        { "path",           10, C_FOOTWAY,   0,             19, 15 },
        { "steps",          10, C_FOOTWAY,   0,             19, 15 },
        { "bridleway",      10, C_FOOTWAY,   0,             19, 15 },
        { "cycleway",       10, C_FOOTWAY,   0,             19, 15 },
    };
    for (unsigned i = 0; i < sizeof(roads) / sizeof(roads[0]); i++) {
        if (obj_has_tag(m, o, "highway", roads[i].v)) {
            sty_line(s, roads[i].rank, roads[i].line, roads[i].casing,
                     roads[i].w_q4, roads[i].min_z);
            /* tunnels: soften fill, no casing */
            if (obj_has_tag(m, o, "tunnel", "yes")) {
                s->line = mf_color_blend(s->line, C_LAND_BG, 110);
                s->casing = 0;
            }
            /* labels for named roads */
            if (roads[i].rank >= 50)      { s->label_zoom = 12; }
            else if (roads[i].rank >= 39) { s->label_zoom = 13; }
            else if (roads[i].rank >= 25) { s->label_zoom = 15; }
            else if (roads[i].rank >= 20) { s->label_zoom = 16; }
            if (s->label_zoom >= 0) {
                s->label_color = C_LABEL_ROAD;
                s->label_size = 10;
            }
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* POI classification                                                  */
/* ------------------------------------------------------------------ */

static void classify_poi(const mf_map_t *m, const mf_obj_t *o, mf_style_t *s)
{
    sty(s, MF_G_POI, 0, 0);

    /* place labels: no dot, prominent text, hidden again at high zoom */
    static const struct { const char *v; int32_t zoom; int32_t maxz; uint32_t color; int32_t size; } places[] = {
        { "country",   2,  11, 0x555555, 16 },
        { "city",      4,  13, 0x444444, 16 },
        { "town",      6,  14, 0x444444, 14 },
        { "village",   10, 15, 0x555555, 12 },
        { "suburb",    12, 16, 0x666666, 12 },
        { "hamlet",    13, 17, 0x666666, 10 },
    };
    for (unsigned i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
        if (obj_has_tag(m, o, "place", places[i].v)) {
            s->label_zoom = places[i].zoom;
            s->label_max_zoom = places[i].maxz;
            s->label_color = places[i].color;
            s->label_size = places[i].size;
            s->rank = 90;
            return;
        }
    }

    static const struct { const char *k; const char *v; uint32_t dot; int32_t dot_z; int32_t lbl_z; } cats[] = {
        { "amenity",  "hospital",          C_POI_HEALTH,  13, 15 },
        { "amenity",  "pharmacy",          C_POI_HEALTH,  15, 16 },
        { "amenity",  "restaurant",        C_POI_FOOD,    16, 16 },
        { "amenity",  "cafe",              C_POI_FOOD,    16, 16 },
        { "amenity",  "fast_food",         C_POI_FOOD,    16, 16 },
        { "amenity",  "bar",               C_POI_FOOD,    16, 17 },
        { "amenity",  "pub",               C_POI_FOOD,    16, 17 },
        { "shop",     "bakery",            C_POI_FOOD,    16, 17 },
        { "shop",     "supermarket",       C_POI_SHOP,    15, 16 },
        { "shop",     "mall",              C_POI_SHOP,    15, 16 },
        { "shop",     NULL,                C_POI_SHOP,    16, 17 },
        { "tourism",  "hotel",             C_POI_TOURISM, 15, 16 },
        { "tourism",  "attraction",        C_POI_TOURISM, 14, 15 },
        { "tourism",  "museum",            C_POI_TOURISM, 14, 15 },
        { "tourism",  "viewpoint",         C_POI_TOURISM, 15, 16 },
        { "historic", "memorial",          C_POI_TOURISM, 15, 16 },
        { "historic", "monument",          C_POI_TOURISM, 15, 16 },
        { "historic", "castle",            C_POI_TOURISM, 14, 15 },
        { "railway",  "station",           C_POI_TRANS,   13, 14 },
        { "amenity",  "fuel",              C_POI_TRANS,   15, 16 },
        { "amenity",  "parking",           C_POI_TRANS,   15, -1 },
        { "highway",  "bus_stop",          C_POI_TRANS,   16, -1 },
        { "amenity",  "school",            C_POI_EDU,     15, 16 },
        { "amenity",  "university",        C_POI_EDU,     14, 15 },
        { "amenity",  "kindergarten",      C_POI_EDU,     15, 16 },
        { "amenity",  "library",           C_POI_EDU,     15, 16 },
        { "amenity",  "place_of_worship",  C_POI_SHOP,    15, 16 },
        { "amenity",  "bank",              C_POI_MISC,    16, 17 },
        { "amenity",  "post_office",       C_POI_MISC,    16, 17 },
        { "natural",  "peak",              0x7a5c3d,      13, 14 },
    };
    for (unsigned i = 0; i < sizeof(cats) / sizeof(cats[0]); i++) {
        if (obj_has_tag(m, o, cats[i].k, cats[i].v)) {
            s->poi_dot = cats[i].dot;
            s->poi_dot_zoom = cats[i].dot_z;
            s->label_zoom = cats[i].lbl_z;
            s->label_color = C_LABEL_POI;
            s->label_size = 10;
            s->rank = 10;
            return;
        }
    }

    /* remaining POIs (bench/bollard/ATM/...): tiny grey dot at high zoom, no label */
    s->poi_dot = C_POI_MISC;
    s->poi_dot_zoom = 17;
    s->rank = 0;
}

void mf_style_get(const mf_map_t *m, const mf_obj_t *o, mf_style_t *s)
{
    if (o->type == MF_OBJ_POI)
        classify_poi(m, o, s);
    else
        classify_way(m, o, s);
}

/* ------------------------------------------------------------------ */
/* Zoom-dependent widths                                               */
/* ------------------------------------------------------------------ */

/* scale factors in Q8 relative to zoom 15: factor = 2^((z-15) * 0.75) */
static const uint16_t k_zscale_q8[22] = {
    /* z0..z7  */    2,   3,   4,   6,   9,  13,  19,  27,
    /* z8..z15 */   38,  51,  68,  91, 122, 162, 215, 256,
    /* z16..z21 */ 362, 512, 724, 1024, 1448, 2048,
};

int32_t mf_zoom_width(int32_t base_q4, int32_t zoom)
{
    if (zoom < 0) zoom = 0;
    if (zoom > 21) zoom = 21;
    int32_t w = (base_q4 * k_zscale_q8[zoom]) >> 8;  /* 1/16 px */
    w = (w + 8) >> 4;                                /* round to px */
    return w < 1 ? 1 : w;
}

uint32_t mf_color_blend(uint32_t a, uint32_t b, int32_t f)
{
    int32_t ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    int32_t br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    int32_t r = ar + (br - ar) * f / 255;
    int32_t g = ag + (bg - ag) * f / 255;
    int32_t bl = ab + (bb - ab) * f / 255;
    return (uint32_t)((r << 16) | (g << 8) | bl);
}
