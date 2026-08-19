#ifndef MF_MAP_H
#define MF_MAP_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MF_MAX_SUBFILES  16
#define MF_MAX_TAGS      4096
#define MF_MAX_WAY_NODES 4096
#define MF_MAX_OBJ       65536

/* Geographic coordinate, in micro-degrees (1e-6 deg). */
typedef struct {
    int32_t lat_e6;
    int32_t lon_e6;
} mf_coord_t;

/* Tile index entry: 5 bytes, high bit = water flag, low 39 bits = block offset */
#define MF_INDEX_WATER_BIT (1LL << 39)
#define MF_INDEX_OFFSET_MASK (MF_INDEX_WATER_BIT - 1)

/* Subfile (one zoom range) */
typedef struct {
    int32_t base_zoom;
    int32_t zoom_min;
    int32_t zoom_max;
    int64_t start_addr;        /* Absolute address of the subfile in the file */
    int64_t sub_file_size;
    int64_t index_start_addr;  /* Absolute address of the tile index */
    int64_t index_end_addr;
    int64_t boundary_tile_left;
    int64_t boundary_tile_top;
    int64_t boundary_tile_right;
    int64_t boundary_tile_bottom;
    int64_t blocks_width;
    int64_t blocks_height;
    int64_t num_blocks;
} mf_subfile_t;

/* Map file handle */
typedef struct {
    FILE        *fp;
    int64_t      file_size;
    int32_t      version;
    int32_t      tile_size;
    int32_t      flags;         /* Optional header field flags */
    int32_t      start_zoom;    /* -1 = none */
    mf_coord_t   start_pos;     /* -1 = none */
    bool         is_debug_file;
    mf_coord_t   bbox_min;      /* Map bounding box */
    mf_coord_t   bbox_max;

    /* Tag table */
    int32_t      poi_tag_cnt;
    char       **poi_tags;
    int32_t      way_tag_cnt;
    char       **way_tags;

    int32_t      zoom_min;
    int32_t      zoom_max;
    int32_t      subfile_cnt;
    mf_subfile_t subfiles[MF_MAX_SUBFILES];
} mf_map_t;

/* Single object in a render-query result */
typedef enum {
    MF_OBJ_POI,
    MF_OBJ_WAY,
} mf_obj_type_t;

typedef struct {
    mf_obj_type_t type;
    int32_t  layer;              /* Layer (-5..5) */
    int32_t  tag_cnt;
    int32_t  tag_ids[16];        /* Index into the tag array */
    char    *name;               /* May be NULL */

    /* POI: position */
    mf_coord_t pos;
    int32_t    poi_elevation;

    /* WAY: nodes (absolute micro-degree coordinates) */
    int32_t    way_node_cnt;
    mf_coord_t *way_nodes;
    bool       way_is_closed;    /* First and last nodes coincide */
} mf_obj_t;

/* Full-water tile (from the tile index water flag;
 * coordinates are tile row/col at the subfile base zoom). */
typedef struct {
    int32_t tx;
    int32_t ty;
} mf_water_tile_t;

/* Query result: one render frame (filled/freed by mf_render) */
typedef struct {
    mf_obj_t *objs;
    int32_t   count;
    int32_t   cap;

    /* Full-water tiles covered by the query (fill water color first) */
    mf_water_tile_t *water_tiles;
    int32_t          water_tile_cnt;
    int32_t          water_tile_cap;
} mf_frame_t;

/* Parse error codes */
#define MF_OK           0
#define MF_ERR_IO       -1
#define MF_ERR_MAGIC    -2
#define MF_ERR_HEADER   -3
#define MF_ERR_RANGE    -4
#define MF_ERR_NO_SUBFILE -5
#define MF_ERR_LIMIT    -6

/* Open a map file and parse its header. */
int mf_open(mf_map_t *m, const char *path);

/* Close the map file. */
void mf_close(mf_map_t *m);

/*
 * Read one frame of visible objects for (subfile, tile).
 * Returns MF_OK or an error code. bbox is the micro-degree rectangle
 * to query, zoom is the query zoom level.
 */
int mf_query(const mf_map_t *m, int32_t zoom,
             int32_t bbox_min_lat_e6, int32_t bbox_min_lon_e6,
             int32_t bbox_max_lat_e6, int32_t bbox_max_lon_e6,
             mf_frame_t *frame);

/* Free a mf_query result. */
void mf_frame_free(mf_frame_t *frame);

/* Find the subfile covering the given zoom level. */
const mf_subfile_t *mf_get_subfile(const mf_map_t *m, int32_t zoom);

/* Tag-table lookup: returns the tag string "key=value". */
const char *mf_poi_tag(const mf_map_t *m, int32_t id);
const char *mf_way_tag(const mf_map_t *m, int32_t id);

/* Lat/lon <-> tile coordinates (z is the zoom level). */
double mf_lon_to_tile_x(double lon, int32_t z);
double mf_lat_to_tile_y(double lat, int32_t z);
double mf_tile_x_to_lon(double tx, int32_t z);
double mf_tile_y_to_lat(double ty, int32_t z);

#ifdef __cplusplus
}
#endif

#endif /* MF_MAP_H */
