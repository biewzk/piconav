#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mf_map.h"
#include "../App/Utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MF_MAGIC "mapsforge binary OSM"

/* ---------------- In-memory reader (for tile block parsing) ---------------- */

typedef struct {
    const uint8_t *buf;
    int64_t        len;
    int64_t        pos;
} mf_reader_t;

static int r_byte(mf_reader_t *r, uint8_t *out)
{
    if (r->pos >= r->len)
        return MF_ERR_RANGE;
    *out = r->buf[r->pos++];
    return MF_OK;
}

static int r_u16(mf_reader_t *r, uint16_t *out)
{
    if (r->pos + 2 > r->len)
        return MF_ERR_RANGE;
    *out = (uint16_t)((r->buf[r->pos] << 8) | r->buf[r->pos + 1]);
    r->pos += 2;
    return MF_OK;
}

/* VBE-U variable-length unsigned int (7 bits/byte, high bit = continuation) */
static int r_vbe_u(mf_reader_t *r, uint32_t *out)
{
    uint32_t v = 0;
    int shift = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t b;
        int rc = r_byte(r, &b);
        if (rc != MF_OK)
            return rc;
        v |= (uint32_t)(b & 0x7f) << shift;
        if (!(b & 0x80))
            break;
        shift += 7;
    }
    *out = v;
    return MF_OK;
}

/* VBE-S variable-length signed int (last byte: 6 data bits + sign);
 * at most 10 bytes, longer is treated as corrupted data. */
static int r_vbe_s(mf_reader_t *r, int32_t *out)
{
    int64_t v = 0;
    int shift = 0;
    uint8_t b;
    for (int i = 0; i < 10; i++) {
        int rc = r_byte(r, &b);
        if (rc != MF_OK)
            return rc;
        if (!(b & 0x80)) {
            if (b & 0x40)
                *out = (int32_t)(-(v | ((int64_t)(b & 0x3f) << shift)));
            else
                *out = (int32_t)(v | ((int64_t)(b & 0x3f) << shift));
            return MF_OK;
        }
        v |= (int64_t)(b & 0x7f) << shift;
        shift += 7;
    }
    return MF_ERR_RANGE;
}

/* Read a string: VBE-U length + UTF-8 bytes, returns a malloc'd copy. */
static int r_str(mf_reader_t *r, char **out)
{
    uint32_t len;
    int rc = r_vbe_u(r, &len);
    if (rc != MF_OK)
        return rc;
    if ((int64_t)len > r->len - r->pos)
        return MF_ERR_RANGE;
    char *s = malloc((size_t)len + 1);
    if (!s)
        return MF_ERR_LIMIT;
    memcpy(s, r->buf + r->pos, len);
    s[len] = 0;
    r->pos += len;
    *out = s;
    return MF_OK;
}

/* If the tag's value is a variable tag (value shaped like
 * %b/%i/%f/%h/%s), consume the corresponding bytes. */
static int r_consume_tag_value(mf_reader_t *r, const char *tag)
{
    const char *eq = strchr(tag, '=');
    if (!eq)
        return MF_OK;
    const char *val = eq + 1;
    if (val[0] != '%' || val[1] == 0 || val[2] != 0)
        return MF_OK;

    switch (val[1]) {
    case 'b': { /* 1 byte */
        uint8_t b;
        return r_byte(r, &b);
    }
    case 'h': { /* 2 byte short */
        uint16_t s;
        return r_u16(r, &s);
    }
    case 'i': /* 4 byte int */
    case 'f': /* 4 byte float */
        if (r->pos + 4 > r->len)
            return MF_ERR_RANGE;
        r->pos += 4;
        return MF_OK;
    case 's': { /* string */
        char *s = NULL;
        int rc = r_str(r, &s);
        free(s);
        return rc;
    }
    default:
        return MF_OK;
    }
}

/* Read n tag IDs (VBE-U) and consume their variable values. */
static int r_tags(mf_reader_t *r, int n, const char *const *table, int32_t table_len,
                  int32_t *ids, int32_t *out_n)
{
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        uint32_t id;
        int rc = r_vbe_u(r, &id);
        if (rc != MF_OK)
            return rc;
        if ((int32_t)id >= table_len)
            return MF_ERR_RANGE;
        ids[cnt++] = (int32_t)id;
        rc = r_consume_tag_value(r, table[id]);
        if (rc != MF_OK)
            return rc;
    }
    *out_n = cnt;
    return MF_OK;
}

/* ---------------- Mercator tile coordinates ---------------- */

double mf_lon_to_tile_x(double lon, int z)
{
    return (lon + 180.0) / 360.0 * (double)(1LL << z);
}

double mf_lat_to_tile_y(double lat, int z)
{
    double r = lat * M_PI / 180.0;
    double y = (1.0 - log(tan(r) + 1.0 / cos(r)) / M_PI) / 2.0;
    return y * (double)(1LL << z);
}

double mf_tile_x_to_lon(double tx, int z)
{
    return tx / (double)(1LL << z) * 360.0 - 180.0;
}

double mf_tile_y_to_lat(double ty, int z)
{
    double n = M_PI - 2.0 * M_PI * ty / (double)(1LL << z);
    return atan((exp(n) - exp(-n)) / 2.0) * 180.0 / M_PI;
}

/* ---------------- File reading ---------------- */

static int f_read_at(FILE *fp, int64_t off, void *dst, int64_t n)
{
    if (fseeko(fp, off, SEEK_SET) != 0)
        return MF_ERR_IO;
    return fread(dst, 1, (size_t)n, fp) == (size_t)n ? MF_OK : MF_ERR_IO;
}

/* ---------------- Public API ---------------- */

/* Return the POI tag string for the given tag id. */
const char *mf_poi_tag(const mf_map_t *m, int32_t id)
{
    if (id < 0 || id >= m->poi_tag_cnt)
        return NULL;
    return m->poi_tags[id];
}

/* Return the way tag string for the given tag id. */
const char *mf_way_tag(const mf_map_t *m, int32_t id)
{
    if (id < 0 || id >= m->way_tag_cnt)
        return NULL;
    return m->way_tags[id];
}

/* Find the subfile whose zoom range covers the given zoom. */
const mf_subfile_t *mf_get_subfile(const mf_map_t *m, int32_t zoom)
{
    for (int i = 0; i < m->subfile_cnt; i++) {
        if (zoom >= m->subfiles[i].zoom_min && zoom <= m->subfiles[i].zoom_max) {
            LOG_D("Currently using sub-file %d", i);
            return &m->subfiles[i];
        }
    }
    return NULL;
}

/*
 * Open the map file and parse its header: magic, version, bbox,
 * tile size, tags and subfile (zoom range) table.
 */
int mf_open(mf_map_t *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    m->start_zoom = -1;
    m->start_pos.lat_e6 = -1;
    m->start_pos.lon_e6 = -1;

    m->fp = fopen(path, "rb");
    if (!m->fp)
        return MF_ERR_IO;
    if (fseeko(m->fp, 0, SEEK_END) != 0) {
        fclose(m->fp);
        m->fp = NULL;
        return MF_ERR_IO;
    }
    m->file_size = ftello(m->fp);

    uint8_t b0 = 0;

    /* magic + header size */
    char magic[24];
    int rc = f_read_at(m->fp, 0, magic, sizeof(magic));
    if (rc != MF_OK)
        goto fail;
    if (memcmp(magic, MF_MAGIC, 20) != 0) {
        rc = MF_ERR_MAGIC;
        goto fail;
    }
    /* remaining header size: 4 bytes right after the 20-byte magic */
    uint32_t header_size = ((uint8_t)magic[20] << 24) | ((uint8_t)magic[21] << 16) |
                           ((uint8_t)magic[22] << 8) | (uint8_t)magic[23];
    if (header_size < 70 || header_size > 2 * 1024 * 1024) {
        rc = MF_ERR_HEADER;
        goto fail;
    }
    LOG_D("map header_size: %d", header_size);

    /* Read the remaining header into memory */
    uint8_t *hdr = malloc(header_size);
    if (!hdr) {
        rc = MF_ERR_LIMIT;
        goto fail;
    }
    rc = f_read_at(m->fp, 24, hdr, header_size);
    if (rc != MF_OK) {
        free(hdr);
        goto fail;
    }
    mf_reader_t r = { hdr, header_size, 0 };

    /* file version (4 bytes BE) */
    {
        uint8_t v[4];
        for (int i = 0; i < 4; i++) {
            rc = r_byte(&r, &v[i]);
            if (rc != MF_OK) { free(hdr); goto fail; }
        }
        m->version = (int32_t)(((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3]);
        if (m->version < 1 || m->version > 5) {
            free(hdr);
            rc = MF_ERR_HEADER;
            goto fail;
        }
    }

    /* file size */
    {
        uint64_t fs = 0;
        uint8_t b[8];
        for (int i = 0; i < 8; i++) {
            rc = r_byte(&r, &b[i]);
            if (rc != MF_OK) { free(hdr); goto fail; }
        }
        for (int i = 0; i < 8; i++)
            fs = (fs << 8) | b[i];
        if ((int64_t)fs != m->file_size) {
            free(hdr);
            rc = MF_ERR_HEADER;
            goto fail;
        }
    }

    /* map date (8 bytes) - skip */
    for (int i = 0; i < 8; i++) {
        rc = r_byte(&r, &b0);
        if (rc != MF_OK) { free(hdr); goto fail; }
    }

    /* bounding box (4 * 4 bytes) */
    {
        int32_t v[4];
        for (int i = 0; i < 4; i++) {
            uint8_t b[4];
            for (int j = 0; j < 4; j++) {
                rc = r_byte(&r, &b[j]);
                if (rc != MF_OK) { free(hdr); goto fail; }
            }
            v[i] = (int32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]);
        }
        m->bbox_min.lat_e6 = v[0];
        m->bbox_min.lon_e6 = v[1];
        m->bbox_max.lat_e6 = v[2];
        m->bbox_max.lon_e6 = v[3];
    }

    /* tile size (2 bytes) */
    {
        uint16_t ts;
        rc = r_u16(&r, &ts);
        if (rc != MF_OK) { free(hdr); goto fail; }
        m->tile_size = ts;
    }

    /* projection name (string) - must be "Mercator" */
    {
        char *proj = NULL;
        rc = r_str(&r, &proj);
        if (rc != MF_OK) { free(hdr); goto fail; }
        if (strcmp(proj, "Mercator") != 0) {
            free(proj);
            free(hdr);
            rc = MF_ERR_HEADER;
            goto fail;
        }
        free(proj);
    }

    /* flags */
    rc = r_byte(&r, &b0);
    if (rc != MF_OK) { free(hdr); goto fail; }
    m->flags = b0;
    m->is_debug_file = (b0 & 0x80) != 0;

    /* optional: map start position */
    if (m->flags & 0x40) {
        uint8_t b[4];
        for (int i = 0; i < 4; i++) {
            rc = r_byte(&r, &b[i]);
            if (rc != MF_OK) { free(hdr); goto fail; }
        }
        m->start_pos.lat_e6 = (int32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]);
        for (int i = 0; i < 4; i++) {
            rc = r_byte(&r, &b[i]);
            if (rc != MF_OK) { free(hdr); goto fail; }
        }
        m->start_pos.lon_e6 = (int32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]);
        LOG_D("optional start position: %.4f,%.4f",
              m->start_pos.lat_e6 / 1e6, m->start_pos.lon_e6 / 1e6);
    }

    /* optional: start zoom */
    if (m->flags & 0x20) {
        rc = r_byte(&r, &b0);
        if (rc != MF_OK) { free(hdr); goto fail; }
        m->start_zoom = b0;
        // m->start_zoom = 10;
        LOG_D("optional start zoom: %d", m->start_zoom);
    }

    /* optional: languages preference / comment / created by (strings, skip) */
    if (m->flags & 0x10) {
        char *s = NULL;
        rc = r_str(&r, &s);
        free(s);
        if (rc != MF_OK) { free(hdr); goto fail; }
    }
    if (m->flags & 0x08) {
        char *s = NULL;
        rc = r_str(&r, &s);
        free(s);
        if (rc != MF_OK) { free(hdr); goto fail; }
    }
    if (m->flags & 0x04) {
        char *s = NULL;
        rc = r_str(&r, &s);
        free(s);
        if (rc != MF_OK) { free(hdr); goto fail; }
    }

    /* POI tags */
    {
        uint16_t n;
        rc = r_u16(&r, &n);
        if (rc != MF_OK) { free(hdr); goto fail; }
        m->poi_tag_cnt = n;
        m->poi_tags = calloc(n > 0 ? n : 1, sizeof(char *));
        if (!m->poi_tags) { free(hdr); rc = MF_ERR_LIMIT; goto fail; }
        for (int i = 0; i < n; i++) {
            rc = r_str(&r, &m->poi_tags[i]);
            if (rc != MF_OK) { free(hdr); goto fail; }
            LOG_D("POI[%02d]: %s", i, m->poi_tags[i]);
        }
    }

    /* way tags */
    {
        uint16_t n;
        rc = r_u16(&r, &n);
        if (rc != MF_OK) { free(hdr); goto fail; }
        m->way_tag_cnt = n;
        m->way_tags = calloc(n > 0 ? n : 1, sizeof(char *));
        if (!m->way_tags) { free(hdr); rc = MF_ERR_LIMIT; goto fail; }
        for (int i = 0; i < n; i++) {
            rc = r_str(&r, &m->way_tags[i]);
            if (rc != MF_OK) { free(hdr); goto fail; }
            LOG_D("WAY[%02d]: %s", i, m->way_tags[i]);
        }
    }

    LOG_I("header: tile_size=%d projection=Mercator flags=0x%02x poi_tags=%d way_tags=%d",
          m->tile_size, m->flags, m->poi_tag_cnt, m->way_tag_cnt);
    if (m->is_debug_file)
        LOG_W("debug map file: 16/32-byte debug sections will be skipped");

    /* number of sub-files */
    rc = r_byte(&r, &b0);
    if (rc != MF_OK) { free(hdr); goto fail; }
    m->subfile_cnt = b0;
    if (m->subfile_cnt < 1 || m->subfile_cnt > MF_MAX_SUBFILES) {
        free(hdr);
        rc = MF_ERR_HEADER;
        goto fail;
    }
    m->zoom_min = 22;
    m->zoom_max = 0;

    for (int i = 0; i < m->subfile_cnt; i++) {
        mf_subfile_t *sf = &m->subfiles[i];

        rc = r_byte(&r, &b0); sf->base_zoom = b0;
        rc |= r_byte(&r, &b0); sf->zoom_min = b0;
        rc |= r_byte(&r, &b0); sf->zoom_max = b0;
        if (rc != MF_OK) { free(hdr); goto fail; }

        {
            uint64_t sa = 0, ss = 0;
            uint8_t b[8];
            for (int j = 0; j < 8; j++) {
                rc = r_byte(&r, &b[j]);
                if (rc != MF_OK) { free(hdr); goto fail; }
            }
            for (int j = 0; j < 8; j++) sa = (sa << 8) | b[j];
            for (int j = 0; j < 8; j++) {
                rc = r_byte(&r, &b[j]);
                if (rc != MF_OK) { free(hdr); goto fail; }
            }
            for (int j = 0; j < 8; j++) ss = (ss << 8) | b[j];
            sf->start_addr = (int64_t)sa;
            sf->sub_file_size = (int64_t)ss;
        }

        if (sf->zoom_min < m->zoom_min) m->zoom_min = sf->zoom_min;
        if (sf->zoom_max > m->zoom_max) m->zoom_max = sf->zoom_max;

        sf->index_start_addr = sf->start_addr + (m->is_debug_file ? 16 : 0);

        /* Compute tile boundaries */
        double bl = (double)m->bbox_min.lon_e6 / 1e6;
        double br = (double)m->bbox_max.lon_e6 / 1e6;
        double bt = (double)m->bbox_max.lat_e6 / 1e6;
        double bb = (double)m->bbox_min.lat_e6 / 1e6;
        sf->boundary_tile_left   = (int64_t)floor(mf_lon_to_tile_x(bl, sf->base_zoom));
        sf->boundary_tile_right  = (int64_t)floor(mf_lon_to_tile_x(br, sf->base_zoom));
        sf->boundary_tile_top    = (int64_t)floor(mf_lat_to_tile_y(bt, sf->base_zoom));
        sf->boundary_tile_bottom = (int64_t)floor(mf_lat_to_tile_y(bb, sf->base_zoom));
        sf->blocks_width  = sf->boundary_tile_right - sf->boundary_tile_left + 1;
        sf->blocks_height = sf->boundary_tile_bottom - sf->boundary_tile_top + 1;
        sf->num_blocks    = sf->blocks_width * sf->blocks_height;
        sf->index_end_addr = sf->index_start_addr + sf->num_blocks * 5;

        LOG_I("subfile[%d]: zoom %d-%d base=%d offset=%lld size=%lld "
              "grid=%lldx%lld blocks=%lld origin_tile=(%lld,%lld) index@%lld",
              i, sf->zoom_min, sf->zoom_max, sf->base_zoom,
              (long long)sf->start_addr, (long long)sf->sub_file_size,
              (long long)sf->blocks_width, (long long)sf->blocks_height,
              (long long)sf->num_blocks,
              (long long)sf->boundary_tile_left,
              (long long)sf->boundary_tile_top,
              (long long)sf->index_start_addr);
    }

    free(hdr);
    LOG_I("map '%s' opened: v%d subfiles=%d zoom %d-%d tile=%d tags=%d/%d%s "
          "bbox(%.4f,%.4f)-(%.4f,%.4f)",
          path, m->version, m->subfile_cnt, m->zoom_min, m->zoom_max,
          m->tile_size, m->poi_tag_cnt, m->way_tag_cnt,
          m->is_debug_file ? " [debug]" : "",
          m->bbox_min.lat_e6 / 1e6, m->bbox_min.lon_e6 / 1e6,
          m->bbox_max.lat_e6 / 1e6, m->bbox_max.lon_e6 / 1e6);
    return MF_OK;

fail:
    mf_close(m);
    LOG_E("map open failed '%s': rc=%d (%s)", path, rc,
          rc == MF_ERR_IO     ? "io" :
          rc == MF_ERR_MAGIC  ? "bad magic" :
          rc == MF_ERR_HEADER ? "bad header" :
          rc == MF_ERR_LIMIT  ? "out of memory" : "?");
    return rc;
}

/* Close the map file and free all tag strings. */
void mf_close(mf_map_t *m)
{
    if (m->fp) {
        fclose(m->fp);
        m->fp = NULL;
    }
    for (int i = 0; i < m->poi_tag_cnt; i++)
        free(m->poi_tags[i]);
    free(m->poi_tags);
    m->poi_tags = NULL;
    m->poi_tag_cnt = 0;
    for (int i = 0; i < m->way_tag_cnt; i++)
        free(m->way_tags[i]);
    free(m->way_tags);
    m->way_tags = NULL;
    m->way_tag_cnt = 0;
}

/* ---------------- Frame (query result) ---------------- */

static mf_obj_t *frame_push(mf_frame_t *f)
{
    if (f->count >= f->cap) {
        int new_cap = f->cap ? f->cap * 2 : 256;
        mf_obj_t *no = realloc(f->objs, (size_t)new_cap * sizeof(mf_obj_t));
        if (!no)
            return NULL;
        f->objs = no;
        f->cap = new_cap;
    }
    return &f->objs[f->count++];
}

static char *frame_name(mf_frame_t *f, const char *s)
{
    (void)f;
    if (!s)
        return NULL;
    char *d = strdup(s);
    if (!d)
        return NULL;
    char *cr = strchr(d, '\r');
    if (cr)
        *cr = 0;
    return d;
}
/* Free all objects and water tiles in a query result frame. */
void mf_frame_free(mf_frame_t *frame)
{
    for (int32_t i = 0; i < frame->count; i++) {
        free(frame->objs[i].name);
        free(frame->objs[i].way_nodes);
    }
    free(frame->objs);
    free(frame->water_tiles);
    memset(frame, 0, sizeof(*frame));
}

static void frame_push_water_tile(mf_frame_t *f, int32_t tx, int32_t ty)
{
    /* Adjacent tiles often hit the same water tile; deduplicate. */
    for (int32_t i = 0; i < f->water_tile_cnt; i++) {
        if (f->water_tiles[i].tx == tx && f->water_tiles[i].ty == ty)
            return;
    }
    if (f->water_tile_cnt >= f->water_tile_cap) {
        int32_t new_cap = f->water_tile_cap ? f->water_tile_cap * 2 : 32;
        mf_water_tile_t *nt = realloc(f->water_tiles, (size_t)new_cap * sizeof(mf_water_tile_t));
        if (!nt)
            return;
        f->water_tiles = nt;
        f->water_tile_cap = new_cap;
    }
    f->water_tiles[f->water_tile_cnt].tx = tx;
    f->water_tiles[f->water_tile_cnt].ty = ty;
    f->water_tile_cnt++;
}

/* ---------------- In-block parsing ---------------- */

/* Parse a single block: all POIs and ways go into the frame. */
static int parse_block(const mf_map_t *m, const mf_subfile_t *sf,
                       int32_t query_zoom, int32_t bbox_min_lat_e6, int32_t bbox_min_lon_e6,
                       int32_t bbox_max_lat_e6, int32_t bbox_max_lon_e6,
                       double tile_lat, double tile_lon,
                       const uint8_t *data, int64_t size, mf_frame_t *frame)
{
    mf_reader_t r = { data, size, 0 };

    if (m->is_debug_file) {
        if (r.len - r.pos < 32)
            return MF_ERR_RANGE;
        r.pos += 32;
    }

    /* zoom table: (zoomMax-zoomMin+1) rows x 2 cols, cumulative counts */
    int rows = sf->zoom_max - sf->zoom_min + 1;
    int32_t *zoom_table = malloc((size_t)rows * 2 * sizeof(int32_t));
    if (!zoom_table)
        return MF_ERR_LIMIT;
    uint32_t cum_pois = 0, cum_ways = 0;
    for (int i = 0; i < rows; i++) {
        uint32_t p, w;
        int rc = r_vbe_u(&r, &p);
        rc |= r_vbe_u(&r, &w);
        if (rc != MF_OK) { free(zoom_table); return rc; }
        cum_pois += p;
        cum_ways += w;
        zoom_table[i * 2]     = (int32_t)cum_pois;
        zoom_table[i * 2 + 1] = (int32_t)cum_ways;
    }

    int query_row = query_zoom - sf->zoom_min;
    if (query_row < 0) query_row = 0;
    if (query_row >= rows) query_row = rows - 1;
    int pois_on_zoom = zoom_table[query_row * 2];
    int ways_on_zoom = zoom_table[query_row * 2 + 1];
    free(zoom_table);

    /* first way offset (relative to current) */
    uint32_t first_way_off;
    int rc = r_vbe_u(&r, &first_way_off);
    if (rc != MF_OK)
        return rc;
    int64_t first_way_pos = r.pos + first_way_off;
    if (first_way_pos > r.len)
        return MF_ERR_RANGE;

    /* Parse POIs */
    for (int i = 0; i < pois_on_zoom; i++) {
        if (m->is_debug_file) {
            if (r.len - r.pos < 32)
                return MF_ERR_RANGE;
            r.pos += 32;
        }
        int32_t dlat, dlon;
        rc = r_vbe_s(&r, &dlat);
        rc |= r_vbe_s(&r, &dlon);
        if (rc != MF_OK)
            return rc;
        double lat = tile_lat + (double)dlat / 1e6;
        double lon = tile_lon + (double)dlon / 1e6;

        uint8_t sp;
        rc = r_byte(&r, &sp);
        if (rc != MF_OK)
            return rc;
        /* Special byte: high nibble = layer + 5 (biased, 0..10),
         * low nibble = number of tags. */
        int32_t layer = (sp >> 4) & 0x0f;
        if (layer > 10)
            layer = 10;
        layer -= 5;
        int32_t ntags = sp & 0x0f;

        int32_t ids[16];
        int32_t got = 0;
        rc = r_tags(&r, ntags, (const char *const *)m->poi_tags, m->poi_tag_cnt, ids, &got);
        if (rc != MF_OK)
            return rc;

        uint8_t fb;
        rc = r_byte(&r, &fb);
        if (rc != MF_OK)
            return rc;

        char *name = NULL;
        if (fb & 0x80) {
            rc = r_str(&r, &name);
            if (rc != MF_OK)
                return rc;
        }
        if (fb & 0x40) {
            char *h = NULL;
            rc = r_str(&r, &h);
            free(h);
            if (rc != MF_OK) { free(name); return rc; }
        }
        int32_t elevation = 0;
        if (fb & 0x20) {
            rc = r_vbe_s(&r, &elevation);
            if (rc != MF_OK) { free(name); return rc; }
        }

        /* Skip if outside the query range */
        int32_t lat_e6 = (int32_t)llround(lat * 1e6);
        int32_t lon_e6 = (int32_t)llround(lon * 1e6);
        if (lat_e6 < bbox_min_lat_e6 || lat_e6 > bbox_max_lat_e6 ||
            lon_e6 < bbox_min_lon_e6 || lon_e6 > bbox_max_lon_e6) {
            free(name);
            continue;
        }

        mf_obj_t *o = frame_push(frame);
        if (!o) { free(name); return MF_ERR_LIMIT; }
        o->type = MF_OBJ_POI;
        o->layer = layer;
        o->tag_cnt = got;
        for (int t = 0; t < got; t++)
            o->tag_ids[t] = ids[t];
        o->name = frame_name(frame, name);
        free(name);
        o->pos.lat_e6 = lat_e6;
        o->pos.lon_e6 = lon_e6;
        o->poi_elevation = elevation;
        o->way_node_cnt = 0;
        o->way_nodes = NULL;
        o->way_is_closed = false;
    }

    /* Parse ways */
    if (ways_on_zoom > 0) {
        if (first_way_pos > r.len)
            return MF_ERR_RANGE;
        r.pos = first_way_pos;

        for (int i = 0; i < ways_on_zoom; i++) {
            if (m->is_debug_file) {
                if (r.len - r.pos < 32)
                    return MF_ERR_RANGE;
                r.pos += 32;
            }

            uint32_t way_data_size;
            rc = r_vbe_u(&r, &way_data_size);
            if (rc != MF_OK)
                return rc;

            /* Subtile bitmap (2 bytes) */
            uint16_t bitmask;
            rc = r_u16(&r, &bitmask);
            (void)bitmask;
            if (rc != MF_OK)
                return rc;

            uint8_t sp;
            rc = r_byte(&r, &sp);
            if (rc != MF_OK)
                return rc;
            /* Special byte: high nibble = layer + 5 (biased, 0..10),
             * low nibble = number of tags. */
            int32_t layer = (sp >> 4) & 0x0f;
            if (layer > 10)
                layer = 10;
            layer -= 5;
            int32_t ntags = sp & 0x0f;

            int32_t ids[16];
            int32_t got = 0;
            rc = r_tags(&r, ntags, (const char *const *)m->way_tags, m->way_tag_cnt, ids, &got);
            if (rc != MF_OK)
                return rc;

            uint8_t fb;
            rc = r_byte(&r, &fb);
            if (rc != MF_OK)
                return rc;

            char *name = NULL;
            if (fb & 0x80) {
                rc = r_str(&r, &name);
                if (rc != MF_OK)
                    return rc;
            }
            if (fb & 0x40) {
                char *h = NULL;
                rc = r_str(&r, &h);
                free(h);
                if (rc != MF_OK) { free(name); return rc; }
            }
            if (fb & 0x20) {
                char *ref = NULL;
                rc = r_str(&r, &ref);
                free(ref);
                if (rc != MF_OK) { free(name); return rc; }
            }
            if (fb & 0x10) {
                int32_t a, b2;
                rc = r_vbe_s(&r, &a);
                rc |= r_vbe_s(&r, &b2);
                if (rc != MF_OK) { free(name); return rc; }
            }
            uint32_t n_way_blocks;
            if (fb & 0x08) {
                rc = r_vbe_u(&r, &n_way_blocks);
                if (rc != MF_OK) { free(name); return rc; }
            } else {
                n_way_blocks = 1;
            }
            bool double_delta = (fb & 0x04) != 0;

            /* way data blocks: coordinate blocks + nodes */
            mf_coord_t *nodes = NULL;
            int node_cnt = 0;

            for (uint32_t wb = 0; wb < n_way_blocks; wb++) {
                uint32_t n_coord_blocks;
                rc = r_vbe_u(&r, &n_coord_blocks);
                if (rc != MF_OK) { free(name); free(nodes); return rc; }

                for (uint32_t cb = 0; cb < n_coord_blocks; cb++) {
                    uint32_t n_nodes;
                    rc = r_vbe_u(&r, &n_nodes);
                    if (rc != MF_OK) { free(name); free(nodes); return rc; }
                    if (n_nodes < 2 || n_nodes > MF_MAX_WAY_NODES) {
                        free(name); free(nodes);
                        return MF_ERR_RANGE;
                    }

                    if (node_cnt + (int)n_nodes > MF_MAX_WAY_NODES) {
                        free(name); free(nodes);
                        return MF_ERR_LIMIT;
                    }

                    mf_coord_t *nn = realloc(nodes, (size_t)(node_cnt + n_nodes) * sizeof(mf_coord_t));
                    if (!nn) { free(name); free(nodes); return MF_ERR_LIMIT; }
                    nodes = nn;

                    int32_t dlat, dlon;
                    rc = r_vbe_s(&r, &dlat);
                    rc |= r_vbe_s(&r, &dlon);
                    if (rc != MF_OK) { free(name); free(nodes); return rc; }
                    double lat = tile_lat + (double)dlat / 1e6;
                    double lon = tile_lon + (double)dlon / 1e6;
                    nodes[node_cnt].lat_e6 = (int32_t)llround(lat * 1e6);
                    nodes[node_cnt].lon_e6 = (int32_t)llround(lon * 1e6);
                    node_cnt++;

                    int32_t prev_lat = 0, prev_lon = 0;
                    for (uint32_t k = 1; k < n_nodes; k++) {
                        int32_t dl, dn;
                        rc = r_vbe_s(&r, &dl);
                        rc |= r_vbe_s(&r, &dn);
                        if (rc != MF_OK) { free(name); free(nodes); return rc; }
                        if (double_delta) {
                            dl += prev_lat;
                            dn += prev_lon;
                        }
                        lat += (double)dl / 1e6;
                        lon += (double)dn / 1e6;
                        nodes[node_cnt].lat_e6 = (int32_t)llround(lat * 1e6);
                        nodes[node_cnt].lon_e6 = (int32_t)llround(lon * 1e6);
                        node_cnt++;
                        prev_lat = dl;
                        prev_lon = dn;
                    }
                }
            }

            /* Bounding-box filter */
            if (node_cnt > 0) {
                int32_t mn_lat = nodes[0].lat_e6, mx_lat = nodes[0].lat_e6;
                int32_t mn_lon = nodes[0].lon_e6, mx_lon = nodes[0].lon_e6;
                for (int k = 1; k < node_cnt; k++) {
                    if (nodes[k].lat_e6 < mn_lat) mn_lat = nodes[k].lat_e6;
                    if (nodes[k].lat_e6 > mx_lat) mx_lat = nodes[k].lat_e6;
                    if (nodes[k].lon_e6 < mn_lon) mn_lon = nodes[k].lon_e6;
                    if (nodes[k].lon_e6 > mx_lon) mx_lon = nodes[k].lon_e6;
                }
                if (mx_lat < bbox_min_lat_e6 || mn_lat > bbox_max_lat_e6 ||
                    mx_lon < bbox_min_lon_e6 || mn_lon > bbox_max_lon_e6) {
                    free(name);
                    free(nodes);
                    continue;
                }
            }

            mf_obj_t *o = frame_push(frame);
            if (!o) { free(name); free(nodes); return MF_ERR_LIMIT; }
            o->type = MF_OBJ_WAY;
            o->layer = layer;
            o->tag_cnt = got;
            for (int t = 0; t < got; t++)
                o->tag_ids[t] = ids[t];
            o->name = frame_name(frame, name);
            free(name);
            o->way_node_cnt = node_cnt;
            o->way_nodes = nodes;
            o->way_is_closed = node_cnt >= 2 &&
                nodes[0].lat_e6 == nodes[node_cnt - 1].lat_e6 &&
                nodes[0].lon_e6 == nodes[node_cnt - 1].lon_e6;
        }
    }

    return MF_OK;
}

/*
 * Query the map for all objects visible in the given bbox at the
 * given zoom. Reads every covering block and fills the frame with
 * POIs, ways and full-water tiles.
 */
int mf_query(const mf_map_t *m, int32_t zoom,
             int32_t bbox_min_lat_e6, int32_t bbox_min_lon_e6,
             int32_t bbox_max_lat_e6, int32_t bbox_max_lon_e6,
             mf_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));

    const mf_subfile_t *sf = mf_get_subfile(m, zoom);
    if (!sf) {
        LOG_D("query z%d: no subfile covers this zoom", zoom);
        return MF_ERR_NO_SUBFILE;
    }

    /* Query tile range (base zoom) */
    int64_t tmin_x = (int64_t)floor(mf_lon_to_tile_x((double)bbox_min_lon_e6 / 1e6, sf->base_zoom));
    int64_t tmax_x = (int64_t)floor(mf_lon_to_tile_x((double)bbox_max_lon_e6 / 1e6, sf->base_zoom));
    int64_t tmin_y = (int64_t)floor(mf_lat_to_tile_y((double)bbox_max_lat_e6 / 1e6, sf->base_zoom));
    int64_t tmax_y = (int64_t)floor(mf_lat_to_tile_y((double)bbox_min_lat_e6 / 1e6, sf->base_zoom));

    int64_t from_x = tmin_x - sf->boundary_tile_left;
    int64_t to_x   = tmax_x - sf->boundary_tile_left;
    int64_t from_y = tmin_y - sf->boundary_tile_top;
    int64_t to_y   = tmax_y - sf->boundary_tile_top;
    if (from_x < 0) from_x = 0;
    if (from_y < 0) from_y = 0;
    if (to_x >= sf->blocks_width)  to_x = sf->blocks_width - 1;
    if (to_y >= sf->blocks_height) to_y = sf->blocks_height - 1;
    if (from_x > to_x || from_y > to_y) {
        LOG_D("query z%d: bbox outside map tile range", zoom);
        return MF_OK;
    }

    int32_t blocks_read = 0;
    LOG_I("The current tile range: %d/%d@%d, %d/%d@%d", from_x, to_x, sf->blocks_width, from_y, to_y, sf->blocks_height);
    for (int64_t row = from_y; row <= to_y; row++) {
        for (int64_t col = from_x; col <= to_x; col++) {
            int64_t block = row * sf->blocks_width + col;

            /* Index entry */
            uint8_t idx[5];
            int rc = f_read_at(m->fp, sf->index_start_addr + block * 5, idx, 5);
            if (rc != MF_OK)
                return rc;
            int64_t entry = 0;
            for (int i = 0; i < 5; i++)
                entry = (entry << 8) | idx[i];
            if (entry & MF_INDEX_WATER_BIT) {
                frame_push_water_tile(frame,
                                      (int32_t)(sf->boundary_tile_left + col),
                                      (int32_t)(sf->boundary_tile_top + row));
            }
            int64_t off = entry & MF_INDEX_OFFSET_MASK;
            if (off < 1 || off > sf->sub_file_size)
                continue;

            /* The block extends up to the next non-empty block: entries
             * with offset 0 (empty or pure-water tiles) must be skipped,
             * otherwise the size would go negative and this block would
             * be silently dropped. */
            int64_t next_off = sf->sub_file_size;
            for (int64_t nb = block + 1; nb < sf->num_blocks; nb++) {
                uint8_t nidx[5];
                rc = f_read_at(m->fp, sf->index_start_addr + nb * 5, nidx, 5);
                if (rc != MF_OK) {
                    LOG_E("index read failed: subfile block %lld",
                          (long long)nb);
                    return rc;
                }
                int64_t ne = 0;
                for (int i = 0; i < 5; i++)
                    ne = (ne << 8) | nidx[i];
                int64_t noff = ne & MF_INDEX_OFFSET_MASK;
                if (noff >= 1) {
                    next_off = noff;
                    break;
                }
            }
            int64_t blk_size = next_off - off;
            if (blk_size <= 0 || blk_size > 8 * 1024 * 1024) {
                LOG_W("block(%lld,%lld): bad size %lld, skipped",
                      (long long)row, (long long)col, (long long)blk_size);
                continue;
            }

            uint8_t *data = malloc((size_t)blk_size);
            if (!data)
                return MF_ERR_LIMIT;
            rc = f_read_at(m->fp, sf->start_addr + off, data, blk_size);
            if (rc != MF_OK) {
                LOG_E("block(%lld,%lld): read failed at offset %lld",
                      (long long)row, (long long)col, (long long)off);
                free(data);
                return rc;
            }

            double tile_lat = mf_tile_y_to_lat((double)(sf->boundary_tile_top + row), sf->base_zoom);
            double tile_lon = mf_tile_x_to_lon((double)(sf->boundary_tile_left + col), sf->base_zoom);

            rc = parse_block(m, sf, zoom, bbox_min_lat_e6, bbox_min_lon_e6,
                             bbox_max_lat_e6, bbox_max_lon_e6,
                             tile_lat, tile_lon, data, blk_size, frame);
            free(data);
            blocks_read++;
            if (rc == MF_ERR_RANGE)
                LOG_W("block(%lld,%lld): malformed data, rest of block skipped",
                      (long long)row, (long long)col);
            else if (rc != MF_OK)
                return rc;
        }
    }

    LOG_D("query z%d done: blocks=%d objs=%d water_tiles=%d",
          zoom, blocks_read, frame->count, frame->water_tile_cnt);

    return MF_OK;
}
