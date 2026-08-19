#ifndef MF_THEME_H
#define MF_THEME_H

#include "mf_map.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
