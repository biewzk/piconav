#ifndef MF_THEME_H
#define MF_THEME_H

#include "mf_map.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Palette (light theme, close to MAPS.ME / OSM Carto)                 */
/* Single source of truth for map colors: mf_theme.c resolves styles   */
/* with them, mf_render.c draws backgrounds/coast/labels with them.    */
/* ------------------------------------------------------------------ */
#define C_LAND_BG     0xf5f3ee
#define C_WATER       0xa9d3e6

#define C_FOREST      0xb5dca6
#define C_PARK        0xc6e8b0
#define C_GRASS       0xd6ecb9
#define C_SCRUB       0xd2e4b4
#define C_BEACH       0xf3e9b7
#define C_CEMETERY    0xd2e1c2
#define C_PITCH       0xbfe0a0

#define C_RESIDENTIAL 0xece9e2
#define C_COMMERCIAL  0xefe8e2
#define C_INDUSTRIAL  0xe9e4dd
#define C_CONSTRUCT   0xe4dfd7
#define C_PARKING     0xe9e9e1
#define C_PIER        0xefece4

#define C_PEDAREA     0xefede6
#define C_PEDAREA_O   0xd8d2c4

#define C_BUILDING    0xddd4c7
#define C_BUILDING_O  0xc7b9a9

#define C_MOTORWAY    0xf7b54d
#define C_MOTORWAY_C  0xd1912f
#define C_PRIMARY     0xfdd45c
#define C_PRIMARY_C   0xd9ae3c
#define C_SECONDARY   0xfee087
#define C_SECONDARY_C 0xdcc06b
#define C_ROAD        0xffffff
#define C_ROAD_C      0xd4cec2
#define C_SERVICE_C   0xded8cc
#define C_FOOTWAY     0xc9c1b6
#define C_RACEWAY     0xf0c0c0
#define C_RACEWAY_C   0xd89898
#define C_RAIL        0x8e8e8e
#define C_BARRIER     0xb8b0a4

#define C_LABEL_ROAD  0x404040
#define C_LABEL_AREA  0x3f6b34
#define C_LABEL_POI   0x3c3c3c

/* POI dot colors */
#define C_POI_FOOD    0xe58a33
#define C_POI_SHOP    0x8a6cd9
#define C_POI_TOURISM 0x2f9e8f
#define C_POI_HEALTH  0xd94f4f
#define C_POI_TRANS   0x3f7fd9
#define C_POI_EDU     0x9c6b3f
#define C_POI_MISC    0x9a9a9a

/* Renderer infrastructure colors (backgrounds, grid, halo) */
#define C_NO_DATA_BG   0xd9d9d9   /* canvas base where no map data exists */
#define C_OUTSIDE_BG   0xc8d8e8   /* strip outside the map data bbox */
#define C_GRID_LINE    0xaabbcc   /* no-data grid lines */
#define C_LABEL_HALO   0xffffff   /* label glow / POI dot ring */

/* Draw groups, painted in this exact order (MAPS.ME-like light theme) */
typedef enum {
    MF_G_UNDEF = 0,
    MF_G_WATER,      /* water polygons (sea/lake/river) */
    MF_G_LANDUSE,    /* landuse polygons (residential/commercial/parking/pier...) */
    MF_G_GREEN,      /* green polygons (forest/park/grass/beach/pitch...) */
    MF_G_PEDAREA,    /* pedestrian squares */
    MF_G_BUILDING,   /* building footprints */
    MF_G_LINE,       /* linear features (waterway/road/rail), sorted by rank+layer */
    MF_G_POI,        /* point features */
} mf_group_t;

/* Resolved style of one map object */
typedef struct {
    mf_group_t group;
    int32_t    rank;         /* in-group z-order; roads: higher = more important (drawn later) */

    /* area fill */
    uint32_t   fill;         /* RGB888, 0 = no fill */
    uint32_t   outline;      /* area outline color, 0 = none */

    /* line */
    uint32_t   line;         /* line color */
    uint32_t   casing;       /* road casing color, 0 = no casing */
    int32_t    width_q4;     /* line width at zoom 15, in 1/16 px */

    /* visibility */
    int32_t    min_zoom;

    /* label */
    int32_t    label_zoom;   /* -1 = never labeled */
    int32_t    label_max_zoom; /* hide label above this zoom (99 = no limit) */
    uint32_t   label_color;
    int32_t    label_size;   /* 10/12/14/16 px */

    /* POI */
    uint32_t   poi_dot;      /* dot color, 0 = no dot */
    int32_t    poi_dot_zoom; /* min zoom for the dot */

    bool       is_area;      /* way should be filled as polygon */
} mf_style_t;

/* Resolve the style of an object from its tags.
 * Unrecognized objects get group = MF_G_UNDEF. */
void mf_style_get(const mf_map_t *m, const mf_obj_t *o, mf_style_t *s);

/* Check whether an object carries tag key[=value] (value NULL = any). */
bool mf_obj_has_tag(const mf_map_t *m, const mf_obj_t *o, const char *key, const char *value);

/* Scale a zoom-15 base width (1/16 px units) to a pixel width at zoom.
 * Result is clamped to >= 1 px. */
int32_t mf_zoom_width(int32_t base_q4, int32_t zoom);

/* Blend two RGB888 colors: f=0 -> a, f=255 -> b */
uint32_t mf_color_blend(uint32_t a, uint32_t b, int32_t f);

#ifdef __cplusplus
}
#endif

#endif /* MF_THEME_H */
