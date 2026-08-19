#ifndef MF_RENDER_H
#define MF_RENDER_H

#include "mf_map.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Viewport: center point + zoom + canvas size */
typedef struct {
    int32_t  zoom;
    double   center_lat;      /* degrees */
    double   center_lon;
    int32_t  center_lat_e6;   /* micro-degrees (for tracking) */
    int32_t  center_lon_e6;
    int32_t  w;               /* canvas width in px */
    int32_t  h;
} mf_view_t;

/* Initialize the viewport. */
void mf_view_init(mf_view_t *v, int32_t zoom, double center_lat, double center_lon,
                  int32_t w, int32_t h);

/* Pan the viewport center: dx/dy in pixels (screen coords, right/down positive). */
void mf_view_pan(mf_view_t *v, int32_t dx, int32_t dy);

/* Compute the visible bbox in micro-degrees (with margin). */
void mf_view_bbox(const mf_view_t *v, int32_t *min_lat, int32_t *min_lon,
                  int32_t *max_lat, int32_t *max_lon);

/* Project micro-degree coords to canvas pixel coords; returns 0 if inside. */
int mf_view_project(const mf_view_t *v, int32_t lat_e6, int32_t lon_e6, lv_point_t *p);

/* Render one frame of objects onto the canvas. */
void mf_render(mf_map_t *m, const mf_view_t *v, mf_frame_t *frame, lv_obj_t *canvas);

#ifdef __cplusplus
}
#endif

#endif /* MF_RENDER_H */