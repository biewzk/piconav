#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mf_render.h"
#include "mf_theme.h"
#include "../App/Utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RENDER_MARGIN   0.08    /* bbox margin ratio (avoids line-width clipping) */
#define DECIMATE_DIST2  2.25    /* node decimation: min squared pixel distance (1.5px) */
#define LBL_MAX         96      /* max labels drawn per frame */
#define LBL_PAD         2       /* label collision box padding (px) */
#define WAY_NODES_MAX   1024    /* skip absurd ways (corrupt data guard) */

/* ------------------------------------------------------------------ */
/* Label collision tracker (per frame)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    lv_coord_t x1, y1, x2, y2;
} lbl_rect_t;

typedef struct {
    lv_obj_t        *canvas;
    const mf_view_t *view;
    int32_t          zoom;
    lbl_rect_t       lbl[LBL_MAX];
    int32_t          lbl_cnt;
    struct { const char *name; lv_coord_t x, y; } road_names[48];
    int32_t          road_name_cnt;
} draw_ctx_t;

static bool lbl_box_free(draw_ctx_t *ctx, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2)
{
    x1 -= LBL_PAD; y1 -= LBL_PAD; x2 += LBL_PAD; y2 += LBL_PAD;
    for (int32_t i = 0; i < ctx->lbl_cnt; i++) {
        const lbl_rect_t *r = &ctx->lbl[i];
        if (x1 <= r->x2 && x2 >= r->x1 && y1 <= r->y2 && y2 >= r->y1)
            return false;
    }
    return true;
}

static void lbl_box_add(draw_ctx_t *ctx, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2)
{
    if (ctx->lbl_cnt >= LBL_MAX)
        return;
    lbl_rect_t *r = &ctx->lbl[ctx->lbl_cnt++];
    r->x1 = x1 - LBL_PAD; r->y1 = y1 - LBL_PAD;
    r->x2 = x2 + LBL_PAD; r->y2 = y2 + LBL_PAD;
}

/* Same road name already labeled near (x, y)? */
static bool road_name_seen(draw_ctx_t *ctx, const char *name, lv_coord_t x, lv_coord_t y)
{
    for (int32_t i = 0; i < ctx->road_name_cnt; i++) {
        if (strcmp(ctx->road_names[i].name, name) == 0) {
            int32_t dx = ctx->road_names[i].x - x;
            int32_t dy = ctx->road_names[i].y - y;
            if (dx * dx + dy * dy < 90 * 90)
                return true;
        }
    }
    if (ctx->road_name_cnt < 48) {
        ctx->road_names[ctx->road_name_cnt].name = name;
        ctx->road_names[ctx->road_name_cnt].x = x;
        ctx->road_names[ctx->road_name_cnt].y = y;
        ctx->road_name_cnt++;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Text drawing (with white halo, MAPS.ME style)                       */
/* ------------------------------------------------------------------ */

static const lv_font_t *pick_font(const char *txt, int32_t size)
{
#if LV_FONT_SIMSUN_16_CJK
    const uint8_t *p = (const uint8_t *)txt;
    while (*p) {
        if (*p >= 0x80)
            return &lv_font_simsun_16_cjk;
        p++;
    }
#endif
    switch (size) {
#if LV_FONT_MONTSERRAT_10
    case 10: return &lv_font_montserrat_10;
#endif
    case 12: return &lv_font_montserrat_12;
    case 14: return &lv_font_montserrat_14;
    case 16: return &lv_font_montserrat_16;
    default: return &lv_font_montserrat_12;
    }
}

static void text_raw(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                     const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.align = LV_TEXT_ALIGN_LEFT;
    lv_canvas_draw_text(canvas, x, y, max_w, &dsc, txt);
}

/* Draw text centered at (cx, cy) with a white halo. Returns true if drawn. */
static bool text_halo(draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy,
                      const char *txt, int32_t size, uint32_t color)
{
    const lv_font_t *font = pick_font(txt, size);
    lv_coord_t w = lv_txt_get_width(txt, (uint32_t)strlen(txt), font, 0, LV_TEXT_FLAG_NONE);
    lv_coord_t h = font->line_height;
    lv_coord_t x = cx - w / 2;
    lv_coord_t y = cy - h / 2;
    if (x + w < 0 || y + h < 0 || x > ctx->view->w || y > ctx->view->h)
        return false;
    if (!lbl_box_free(ctx, x, y, x + w, y + h))
        return false;

    lv_color_t fg = lv_color_hex(color);
    lv_color_t halo = lv_color_hex(C_LABEL_HALO);
    text_raw(ctx->canvas, x - 1, y, w + 8, font, halo, txt);
    text_raw(ctx->canvas, x + 1, y, w + 8, font, halo, txt);
    text_raw(ctx->canvas, x, y - 1, w + 8, font, halo, txt);
    text_raw(ctx->canvas, x, y + 1, w + 8, font, halo, txt);
    text_raw(ctx->canvas, x, y, w + 8, font, fg, txt);

    lbl_box_add(ctx, x, y, x + w, y + h);
    return true;
}

/* ------------------------------------------------------------------ */
/* Geometry helpers                                                    */
/* ------------------------------------------------------------------ */

/* Scanline even-odd polygon fill, direct into the canvas framebuffer.
 * Robust for arbitrary polygons (concave / self-intersecting),
 * no triangulation and no clipping needed. */
static void fb_fill_polygon(lv_obj_t *canvas, const lv_point_t *p, int n, lv_color_t color)
{
    lv_img_dsc_t *img = lv_canvas_get_img(canvas);
    lv_color_t *fb = (lv_color_t *)img->data;
    int32_t W = img->header.w;
    int32_t H = img->header.h;

    int32_t ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    if (ymin < 0) ymin = 0;
    if (ymax > H - 1) ymax = H - 1;
    if (ymin > ymax)
        return;

    int32_t *xs = malloc((size_t)n * sizeof(int32_t));
    if (!xs)
        return;

    for (int32_t y = ymin; y <= ymax; y++) {
        double yc = (double)y + 0.5;
        int nx = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            int32_t y0 = p[j].y, y1 = p[i].y;
            if ((y0 < yc && y1 >= yc) || (y1 < yc && y0 >= yc)) {
                double t = (yc - y0) / (double)(y1 - y0);
                xs[nx++] = (int32_t)llround(p[j].x + t * ((double)p[i].x - p[j].x));
            }
        }
        if (nx < 2)
            continue;
        /* insertion sort (nx small after projection) */
        for (int a = 1; a < nx; a++) {
            int32_t v = xs[a], b = a - 1;
            while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
            xs[b + 1] = v;
        }
        lv_color_t *row = fb + y * W;
        for (int a = 0; a + 1 < nx; a += 2) {
            int32_t x1 = xs[a], x2 = xs[a + 1];
            if (x2 < 0 || x1 > W - 1)
                continue;
            if (x1 < 0) x1 = 0;
            if (x2 > W - 1) x2 = W - 1;
            for (int32_t x = x1; x <= x2; x++)
                row[x] = color;
        }
    }
    free(xs);
}

/* Project way nodes to canvas pixels, with distance decimation.
 * Returns malloc'ed point array (caller frees) and count in *out_n. */
static lv_point_t *way_to_pixels(const mf_view_t *v, const mf_obj_t *o, int *out_n)
{
    int n = o->way_node_cnt;
    if (n > WAY_NODES_MAX) {
        LOG_D("way truncated: %d -> %d nodes", n, WAY_NODES_MAX);
        n = WAY_NODES_MAX;
    }
    lv_point_t *pts = malloc((size_t)n * sizeof(lv_point_t));
    if (!pts) {
        *out_n = 0;
        return NULL;
    }
    int out = 0;
    for (int i = 0; i < n; i++) {
        lv_point_t p;
        mf_view_project(v, o->way_nodes[i].lat_e6, o->way_nodes[i].lon_e6, &p);
        if (out > 0) {
            double dx = (double)p.x - pts[out - 1].x;
            double dy = (double)p.y - pts[out - 1].y;
            bool last = (i == n - 1);
            if (dx * dx + dy * dy < DECIMATE_DIST2 && !(last && o->way_is_closed))
                continue;
        }
        pts[out++] = p;
    }
    *out_n = out;
    return pts;
}

/* ------------------------------------------------------------------ */
/* Coastline land fill                                                 */
/*                                                                     */
/* The sea/nosea polygons in mapsforge files are crude tile rects at   */
/* low zoom and incomplete at high zoom, while natural=coastline ways  */
/* carry the true coast geometry.  When coastline data is present in   */
/* the frame we render the land from it: fill the canvas with sea      */
/* color, then fill land polygons built from coastline ways (OSM:      */
/* land is on the left of the way direction).                          */
/* ------------------------------------------------------------------ */

typedef struct {
    mf_coord_t *pts;
    int32_t    n;
    bool       closed;
} coast_chain_t;

/* double-precision projection (no rounding to pixels) */
static void project_d(const mf_view_t *v, int32_t lat_e6, int32_t lon_e6, double *x, double *y)
{
    double z = (double)(1LL << v->zoom);
    double px_per_deg_lon = 256.0 * z / 360.0;
    *x = ((double)lon_e6 / 1e6 - v->center_lon) * px_per_deg_lon + (double)v->w / 2.0;
    double lat_r = (double)lat_e6 / 1e6 * M_PI / 180.0;
    double ctr_r = v->center_lat * M_PI / 180.0;
    double y_lat = (1.0 - log(tan(lat_r) + 1.0 / cos(lat_r)) / M_PI) / 2.0;
    double y_ctr = (1.0 - log(tan(ctr_r) + 1.0 / cos(ctr_r)) / M_PI) / 2.0;
    *y = (y_lat - y_ctr) * 256.0 * z + (double)v->h / 2.0;
}

/* true endpoints match exactly in the data (sub-meter) */
static bool e6_near(const mf_coord_t *a, const mf_coord_t *b)
{
    int32_t dla = a->lat_e6 - b->lat_e6; if (dla < 0) dla = -dla;
    int32_t dlo = a->lon_e6 - b->lon_e6; if (dlo < 0) dlo = -dlo;
    return dla <= 2 && dlo <= 2;
}



/* ------------------------------------------------------------------ */
/* Flood-fill coastline (no polygon assembly needed)                    */
/*                                                                     */
/* Draw coastline ways as thick barrier lines on a 1-bit bitmap, then   */
/* BFS flood-fill from known sea reference points (water tile corners). */
/* Pixels reachable from sea without crossing a barrier are sea.        */
/* All bitmaps are sized dynamically from the canvas dimensions.        */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t  w;      /* width in pixels */
    int32_t  h;      /* height in pixels */
    int32_t  bw;     /* bytes per row */
    uint8_t *bits;
} coast_bm_t;

static inline void bf_set(coast_bm_t *bm, int x, int y)
{
    bm->bits[y * bm->bw + x / 8] |= (uint8_t)(1u << (x & 7));
}

static inline bool bf_test(const coast_bm_t *bm, int x, int y)
{
    return (bm->bits[y * bm->bw + x / 8] >> (x & 7)) & 1;
}

/* Draw a thick line (rasterize with Bresenham, radius = thickness/2) */
static void bf_draw_line(coast_bm_t *bm, int x0, int y0, int x1, int y1, int rad)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    dx = dx >= 0 ? dx : -dx;
    dy = dy >= 0 ? dy : -dy;
    int err = dx - dy;
    for (;;) {
        /* draw a filled circle of radius rad at (x0, y0) */
        for (int ry = -rad; ry <= rad; ry++) {
            int yy = y0 + ry;
            if (yy < 0 || yy >= bm->h) continue;
            int xmax = (int)llround(sqrt((double)(rad * rad - ry * ry)));
            for (int rx = -xmax; rx <= xmax; rx++) {
                int xx = x0 + rx;
                if (xx >= 0 && xx < bm->w)
                    bf_set(bm, xx, yy);
            }
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* BFS queue (simple ring buffer) */
typedef struct {
    int32_t *buf;
    int32_t head, tail, cap;
} bfs_q_t;

static void bq_push(bfs_q_t *q, int32_t v)
{
    q->buf[q->tail] = v;
    q->tail = (q->tail + 1) % q->cap;
}

static int32_t bq_pop(bfs_q_t *q)
{
    int32_t v = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    return v;
}

static bool bq_empty(const bfs_q_t *q) { return q->head == q->tail; }

/* Fill coastline using flood-fill from sea reference points.
 * Returns true if any fill was done. */
static bool fill_coastline(draw_ctx_t *ctx, const mf_map_t *m, const mf_frame_t *frame,
                           const mf_style_t *styles)
{
    /* collect coastline ways into chains */
    int32_t nways = 0;
    for (int32_t i = 0; i < frame->count; i++)
        if (frame->objs[i].type == MF_OBJ_WAY &&
            mf_obj_has_tag(m, &frame->objs[i], "natural", "coastline"))
            nways++;
    if (nways == 0)
        return false;

    coast_chain_t *chains = calloc((size_t)nways, sizeof(coast_chain_t));
    if (!chains) {
        LOG_E("coastline: out of memory (chains)");
        return false;
    }
    int32_t nch = 0;
    for (int32_t i = 0; i < frame->count; i++) {
        const mf_obj_t *o = &frame->objs[i];
        if (o->type != MF_OBJ_WAY || o->way_node_cnt < 2)
            continue;
        if (!mf_obj_has_tag(m, o, "natural", "coastline"))
            continue;
        coast_chain_t *c = &chains[nch++];
        c->pts = malloc((size_t)o->way_node_cnt * sizeof(mf_coord_t));
        if (!c->pts) {
            LOG_W("coastline: way points alloc failed, way dropped");
            nch--;
            continue;
        }
        memcpy(c->pts, o->way_nodes, (size_t)o->way_node_cnt * sizeof(mf_coord_t));
        c->n = o->way_node_cnt;
        c->closed = o->way_is_closed;
    }

    /* join chains at matching endpoints */
    for (;;) {
        bool merged = false;
        for (int32_t i = 0; i < nch && !merged; i++) {
            coast_chain_t *a = &chains[i];
            if (a->n == 0 || a->closed)
                continue;
            for (int32_t j = 0; j < nch; j++) {
                if (i == j || chains[j].n == 0)
                    continue;
                coast_chain_t *b = &chains[j];
                const mf_coord_t *as = &a->pts[0], *ae = &a->pts[a->n - 1];
                const mf_coord_t *bs = &b->pts[0], *be = &b->pts[b->n - 1];
                bool rev = false, prepend = false;
                if (e6_near(ae, bs))       { rev = false; prepend = false; }
                else if (e6_near(ae, be))  { rev = true;  prepend = false; }
                else if (e6_near(as, be))  { rev = false; prepend = true;  }
                else if (e6_near(as, bs))  { rev = true;  prepend = true;  }
                else
                    continue;
                if (rev) {
                    for (int32_t k = 0; k < b->n / 2; k++) {
                        mf_coord_t tmp = b->pts[k];
                        b->pts[k] = b->pts[b->n - 1 - k];
                        b->pts[b->n - 1 - k] = tmp;
                    }
                }
                int32_t nn = a->n + b->n;
                mf_coord_t *np = realloc(a->pts, (size_t)nn * sizeof(mf_coord_t));
                if (np) {
                    a->pts = np;
                    if (prepend) {
                        memmove(np + b->n, np, (size_t)a->n * sizeof(mf_coord_t));
                        memcpy(np, b->pts, (size_t)b->n * sizeof(mf_coord_t));
                    } else {
                        memcpy(np + a->n, b->pts, (size_t)b->n * sizeof(mf_coord_t));
                    }
                    a->n = nn;
                }
                /* On allocation failure b is dropped to guarantee
                 * termination; a keeps its points. */
                free(b->pts);
                b->pts = NULL;
                b->n = 0;
                merged = true;
                break;
            }
        }
        if (!merged)
            break;
    }

    bool any_fill = false;
    int32_t islands = 0, sea_px = 0;

    /* islands (closed coastline rings): land fill */
    for (int32_t i = 0; i < nch; i++) {
        coast_chain_t *c = &chains[i];
        if (c->n < 3 || !c->closed)
            continue;
        lv_point_t *poly = malloc((size_t)c->n * sizeof(lv_point_t));
        if (!poly)
            continue;
        for (int32_t k = 0; k < c->n; k++) {
            double x, y;
            project_d(ctx->view, c->pts[k].lat_e6, c->pts[k].lon_e6, &x, &y);
            poly[k].x = (lv_coord_t)llround(x);
            poly[k].y = (lv_coord_t)llround(y);
        }
        fb_fill_polygon(ctx->canvas, poly, c->n, lv_color_hex(C_LAND_BG));
        free(poly);
        islands++;
        any_fill = true;
    }

    /* allocate barrier bitmap sized to the canvas and draw coastline
     * ways as thick lines */
    lv_img_dsc_t *img = lv_canvas_get_img(ctx->canvas);
    coast_bm_t barrier;
    barrier.w = img->header.w;
    barrier.h = img->header.h;
    barrier.bw = (barrier.w + 7) / 8;
    barrier.bits = calloc((size_t)barrier.bw * barrier.h, 1);
    if (!barrier.bits) {
        LOG_E("coastline: out of memory (barrier bitmap)");
        goto done;
    }

    int thickness = ctx->zoom >= 14 ? 3 : 2;
    int rad = thickness / 2 + 1;

    for (int32_t i = 0; i < nch; i++) {
        coast_chain_t *c = &chains[i];
        if (c->n < 2)
            continue;
        for (int32_t k = 0; k + 1 < c->n; k++) {
            double ax, ay, bx, by;
            project_d(ctx->view, c->pts[k].lat_e6, c->pts[k].lon_e6, &ax, &ay);
            project_d(ctx->view, c->pts[k + 1].lat_e6, c->pts[k + 1].lon_e6, &bx, &by);
            int x0 = (int)llround(ax), y0 = (int)llround(ay);
            int x1 = (int)llround(bx), y1 = (int)llround(by);
            bf_draw_line(&barrier, x0, y0, x1, y1, rad);
        }
    }

    /* BFS flood fill from sea seed points (water tile corners) */
    {
        coast_bm_t visited = { barrier.w, barrier.h, barrier.bw,
                               calloc((size_t)barrier.bw * barrier.h, 1) };
        if (!visited.bits) {
            LOG_E("coastline: out of memory (visited bitmap)");
            free(barrier.bits);
            goto done;
        }

        bfs_q_t q;
        q.cap = barrier.w * barrier.h;
        q.buf = malloc((size_t)q.cap * sizeof(int32_t));
        if (!q.buf) {
            LOG_E("coastline: out of memory (bfs queue)");
            free(visited.bits);
            free(barrier.bits);
            goto done;
        }
        q.head = q.tail = 0;

        /* seed from water tile corners */
        const mf_subfile_t *sf = mf_get_subfile(m, ctx->zoom);
        for (int32_t i = 0; i < frame->water_tile_cnt && sf; i++) {
            int32_t tx = frame->water_tiles[i].tx;
            int32_t ty = frame->water_tiles[i].ty;
            for (int k = 0; k < 4; k++) {
                double lon = mf_tile_x_to_lon(tx + (k == 1 || k == 2), sf->base_zoom);
                double lat = mf_tile_y_to_lat(ty + (k >= 2), sf->base_zoom);
                double x, y;
                project_d(ctx->view, (int32_t)llround(lat * 1e6), (int32_t)llround(lon * 1e6), &x, &y);
                int px = (int)llround(x), py = (int)llround(y);
                if (px >= 0 && px < barrier.w && py >= 0 && py < barrier.h &&
                    !bf_test(&barrier, px, py) && !bf_test(&visited, px, py)) {
                    bf_set(&visited, px, py);
                    bq_push(&q, py * barrier.w + px);
                }
            }
        }

        /* BFS 4-connected flood fill */
        static const int dx4[4] = { 1, -1, 0, 0 };
        static const int dy4[4] = { 0, 0, 1, -1 };
        while (!bq_empty(&q)) {
            int32_t v = bq_pop(&q);
            int cx = v % barrier.w, cy = v / barrier.w;
            for (int d = 0; d < 4; d++) {
                int nx = cx + dx4[d], ny = cy + dy4[d];
                if (nx < 0 || nx >= barrier.w || ny < 0 || ny >= barrier.h)
                    continue;
                if (bf_test(&barrier, nx, ny) || bf_test(&visited, nx, ny))
                    continue;
                bf_set(&visited, nx, ny);
                bq_push(&q, ny * barrier.w + nx);
            }
        }
        free(q.buf);

        /* DEBUG: dump barrier/visited bitmaps for offline inspection */
        if (getenv("MF_DUMP_COAST")) {
            FILE *fp = fopen("/tmp/coast_barrier.pbm", "wb");
            if (fp) {
                fprintf(fp, "P4\n%d %d\n", barrier.w, barrier.h);
                fwrite(barrier.bits, 1, (size_t)barrier.bw * barrier.h, fp);
                fclose(fp);
            }
            fp = fopen("/tmp/coast_visited.pbm", "wb");
            if (fp) {
                fprintf(fp, "P4\n%d %d\n", visited.w, visited.h);
                fwrite(visited.bits, 1, (size_t)visited.bw * visited.h, fp);
                fclose(fp);
            }
            fprintf(stderr, "mf: coast dump: water_tiles=%d chains=%d\n",
                    (int)frame->water_tile_cnt, (int)nch);
        }

        /* fill sea pixels on canvas with sea color */
        lv_color_t *fb = (lv_color_t *)img->data;
        int32_t W = img->header.w;
        lv_color_t sea = lv_color_hex(C_WATER);
        for (int y = 0; y < barrier.h; y++) {
            for (int x = 0; x < barrier.w; x++) {
                if (bf_test(&visited, x, y)) {
                    fb[y * W + x] = sea;
                    sea_px++;
                }
            }
        }

        free(visited.bits);
        any_fill = true;
    }

    free(barrier.bits);

done:
    {
        int32_t live = 0;
        for (int32_t i = 0; i < nch; i++)
            if (chains[i].n > 0)
                live++;
        LOG_D("coastline: ways=%d chains=%d islands=%d sea_px=%d (%d%%)",
              nways, live, islands, sea_px,
              (int)(sea_px * 100 / (barrier.w * barrier.h)));
    }
    for (int32_t i = 0; i < nch; i++)
        free(chains[i].pts);
    free(chains);
    return any_fill;
}

/* ------------------------------------------------------------------ */
/* Feature drawing                                                     */
/* ------------------------------------------------------------------ */

static void draw_area(draw_ctx_t *ctx, const mf_obj_t *o, const mf_style_t *s)
{
    int n = 0;
    lv_point_t *pts = way_to_pixels(ctx->view, o, &n);
    if (!pts || n < 3) {
        free(pts);
        return;
    }

    fb_fill_polygon(ctx->canvas, pts, n, lv_color_hex(s->fill));

    /* area outline (buildings, pedestrian squares) */
    if (s->outline && ctx->zoom >= 14) {
        lv_point_t *ring = malloc((size_t)(n + 1) * sizeof(lv_point_t));
        if (ring) {
            memcpy(ring, pts, (size_t)n * sizeof(lv_point_t));
            ring[n] = pts[0];
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = lv_color_hex(s->outline);
            ld.width = 1;
            ld.opa = LV_OPA_COVER;
            lv_canvas_draw_line(ctx->canvas, ring, (uint32_t)(n + 1), &ld);
            free(ring);
        }
    }

    free(pts);
}

static void draw_line_way(draw_ctx_t *ctx, const mf_obj_t *o, const mf_style_t *s, bool casing_pass)
{
    if (!casing_pass && s->line == 0)
        return;
    if (casing_pass && (s->casing == 0 || o->way_node_cnt < 2))
        return;

    int n = 0;
    lv_point_t *pts = way_to_pixels(ctx->view, o, &n);
    if (!pts || n < 2) {
        free(pts);
        return;
    }

    lv_coord_t w = (lv_coord_t)mf_zoom_width(s->width_q4, ctx->zoom);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(casing_pass ? s->casing : s->line);
    dsc.width = casing_pass ? (lv_coord_t)(w + 2) : w;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_canvas_draw_line(ctx->canvas, pts, (uint32_t)n, &dsc);
    free(pts);
}

/* ------------------------------------------------------------------ */
/* Labels                                                              */
/* ------------------------------------------------------------------ */

/* road name along the way midpoint (horizontal text) */
static void draw_way_label(draw_ctx_t *ctx, const mf_obj_t *o, const mf_style_t *s)
{
    if (!o->name || o->way_node_cnt < 2)
        return;

    int n = 0;
    lv_point_t *pts = way_to_pixels(ctx->view, o, &n);
    if (!pts || n < 2) {
        free(pts);
        return;
    }

    /* total pixel length */
    double total = 0;
    for (int i = 1; i < n; i++) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        total += sqrt(dx * dx + dy * dy);
    }

    const lv_font_t *font = pick_font(o->name, s->label_size);
    lv_coord_t tw = lv_txt_get_width(o->name, (uint32_t)strlen(o->name), font, 0, LV_TEXT_FLAG_NONE);

    if (total < (double)tw + 30.0) {
        free(pts);
        return;
    }

    /* midpoint of the polyline */
    double half = total / 2.0;
    lv_coord_t mx = pts[0].x, my = pts[0].y;
    double acc = 0;
    for (int i = 1; i < n; i++) {
        double dx = pts[i].x - pts[i - 1].x;
        double dy = pts[i].y - pts[i - 1].y;
        double seg = sqrt(dx * dx + dy * dy);
        if (acc + seg >= half && seg > 0) {
            double t = (half - acc) / seg;
            mx = (lv_coord_t)llround(pts[i - 1].x + t * dx);
            my = (lv_coord_t)llround(pts[i - 1].y + t * dy);
            break;
        }
        acc += seg;
    }
    free(pts);

    if (mx < -20 || my < -10 || mx > ctx->view->w + 20 || my > ctx->view->h + 10)
        return;
    if (road_name_seen(ctx, o->name, mx, my))
        return;

    text_halo(ctx, mx, my, o->name, s->label_size, s->label_color);
}

static void draw_area_label(draw_ctx_t *ctx, const mf_obj_t *o, const mf_style_t *s)
{
    if (!o->name || o->way_node_cnt < 1)
        return;
    /* centroid of the on-screen way nodes only (off-screen corners
     * would drag the label out of view) */
    int64_t sx = 0, sy = 0;
    int inside = 0;
    for (int i = 0; i < o->way_node_cnt; i++) {
        lv_point_t p;
        if (mf_view_project(ctx->view, o->way_nodes[i].lat_e6, o->way_nodes[i].lon_e6, &p) != 0)
            continue;
        sx += p.x;
        sy += p.y;
        inside++;
    }
    if (!inside)
        return;
    lv_coord_t cx = (lv_coord_t)(sx / inside);
    lv_coord_t cy = (lv_coord_t)(sy / inside);
    text_halo(ctx, cx, cy, o->name, s->label_size, s->label_color);
}

static void draw_poi(draw_ctx_t *ctx, const mf_obj_t *o, const mf_style_t *s)
{
    lv_point_t p;
    if (mf_view_project(ctx->view, o->pos.lat_e6, o->pos.lon_e6, &p) != 0)
        return;

    /* dot: white ring + colored center (reads like a MAPS.ME pin) */
    if (s->poi_dot && ctx->zoom >= s->poi_dot_zoom) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_opa = LV_OPA_COVER;
        dsc.bg_color = lv_color_hex(C_LABEL_HALO);
        dsc.radius = 4;
        lv_canvas_draw_rect(ctx->canvas, p.x - 4, p.y - 4, 8, 8, &dsc);
        dsc.bg_color = lv_color_hex(s->poi_dot);
        dsc.radius = 3;
        lv_canvas_draw_rect(ctx->canvas, p.x - 3, p.y - 3, 6, 6, &dsc);
    }

    if (o->name && s->label_zoom >= 0 && ctx->zoom >= s->label_zoom &&
        ctx->zoom <= s->label_max_zoom) {
        lv_coord_t cy = p.y;
        if (s->poi_dot && ctx->zoom >= s->poi_dot_zoom)
            cy += 10;   /* label below the dot */
        text_halo(ctx, p.x, cy, o->name, s->label_size, s->label_color);
    }
}

/* ------------------------------------------------------------------ */
/* Viewport projection                                                 */
/* ------------------------------------------------------------------ */

/*
 * Project a geographic position (micro-degrees) to canvas pixels.
 * Returns 0 if the point lies within (a padded margin around) the
 * canvas, otherwise 1.
 */
int mf_view_project(const mf_view_t *v, int32_t lat_e6, int32_t lon_e6, lv_point_t *p)
{
    double z = (double)(1LL << v->zoom);
    double px_per_deg_lon = 256.0 * z / 360.0;
    double x = ((double)lon_e6 / 1e6 - v->center_lon) * px_per_deg_lon + (double)v->w / 2.0;

    double lat_r = (double)lat_e6 / 1e6 * M_PI / 180.0;
    double ctr_r = v->center_lat * M_PI / 180.0;
    double y_lat = (1.0 - log(tan(lat_r) + 1.0 / cos(lat_r)) / M_PI) / 2.0;
    double y_ctr = (1.0 - log(tan(ctr_r) + 1.0 / cos(ctr_r)) / M_PI) / 2.0;
    double y = (y_lat - y_ctr) * 256.0 * z + (double)v->h / 2.0;

    /* lv_coord_t is int16 (LV_USE_LARGE_COORD 0): far points (e.g. the map
     * bbox corners at high zoom) would wrap around into bogus on-screen
     * coordinates. Clamp to a safe off-screen range instead — the
     * on-screen test below only cares about the +/-8 px margin. */
    const double lim = 30000.0;
    if (x < -lim) x = -lim; else if (x > lim) x = lim;
    if (y < -lim) y = -lim; else if (y > lim) y = lim;
    p->x = (lv_coord_t)llround(x);
    p->y = (lv_coord_t)llround(y);
    if (p->x < -8 || p->y < -8 || p->x > v->w + 8 || p->y > v->h + 8)
        return 1;
    return 0;
}

/* Initialize a viewport at the given zoom, center and canvas size. */
void mf_view_init(mf_view_t *v, int32_t zoom, double center_lat, double center_lon,
                  int32_t w, int32_t h)
{
    v->zoom = zoom;
    v->center_lat = center_lat;
    v->center_lon = center_lon;
    v->center_lat_e6 = (int32_t)llround(center_lat * 1e6);
    v->center_lon_e6 = (int32_t)llround(center_lon * 1e6);
    v->w = w;
    v->h = h;
}

/* Pan the viewport center by a screen-space offset (dx/dy in px). */
void mf_view_pan(mf_view_t *v, int32_t dx, int32_t dy)
{
    double z = (double)(1LL << v->zoom);
    double px_per_deg_lon = 256.0 * z / 360.0;
    v->center_lon += (double)dx / px_per_deg_lon;

    double lat_r = v->center_lat * M_PI / 180.0;
    double cy = (1.0 - log(tan(lat_r) + 1.0 / cos(lat_r)) / M_PI) / 2.0;
    cy += (double)dy / (256.0 * z);
    double n = M_PI - 2.0 * M_PI * cy;
    v->center_lat = atan((exp(n) - exp(-n)) / 2.0) * 180.0 / M_PI;

    v->center_lat_e6 = (int32_t)llround(v->center_lat * 1e6);
    v->center_lon_e6 = (int32_t)llround(v->center_lon * 1e6);
}

/* Compute the visible bbox in micro-degrees (with render margin). */
void mf_view_bbox(const mf_view_t *v, int32_t *min_lat, int32_t *min_lon,
                  int32_t *max_lat, int32_t *max_lon)
{
    double deg_per_px = 360.0 / (double)(1LL << v->zoom) / 256.0;
    double span_lon = deg_per_px * v->w * (1.0 + RENDER_MARGIN);
    double half_lon = span_lon / 2.0;
    *min_lon = (int32_t)llround((v->center_lon - half_lon) * 1e6);
    *max_lon = (int32_t)llround((v->center_lon + half_lon) * 1e6);

    double lat_r = v->center_lat * M_PI / 180.0;
    double cy = (1.0 - log(tan(lat_r) + 1.0 / cos(lat_r)) / M_PI) / 2.0;
    double span_norm = (double)v->h * (1.0 + RENDER_MARGIN) / (double)((int64_t)1 << v->zoom) / 256.0;
    double y0 = cy - span_norm / 2.0;
    double y1 = cy + span_norm / 2.0;
    /* clamp to the Mercator range: beyond it exp()/tan() overflow */
    if (y0 < 0.0) y0 = 0.0;
    if (y1 > 1.0) y1 = 1.0;

    double lat_to_deg = 180.0 / M_PI;
    double n0 = M_PI - 2.0 * M_PI * y0;
    double n1 = M_PI - 2.0 * M_PI * y1;
    *max_lat = (int32_t)llround(atan((exp(n0) - exp(-n0)) / 2.0) * lat_to_deg * 1e6);
    *min_lat = (int32_t)llround(atan((exp(n1) - exp(-n1)) / 2.0) * lat_to_deg * 1e6);
}

/* ------------------------------------------------------------------ */
/* Frame rendering                                                     */
/* ------------------------------------------------------------------ */

/* draw one area group filtered by rank range */
static void draw_area_group(draw_ctx_t *ctx, const mf_frame_t *frame, const mf_style_t *styles,
                            mf_group_t g, int32_t rank_min, int32_t rank_max)
{
    for (int32_t i = 0; i < frame->count; i++) {
        const mf_obj_t *o = &frame->objs[i];
        if (o->type != MF_OBJ_WAY)
            continue;
        const mf_style_t *s = &styles[i];
        if (s->group != g || s->rank < rank_min || s->rank > rank_max || ctx->zoom < s->min_zoom)
            continue;
        draw_area(ctx, o, s);
    }
}

/* sortable line index */
typedef struct {
    int32_t key;    /* (layer+5)*100 + rank */
    int32_t idx;    /* index into frame->objs */
} line_ref_t;

static int line_ref_cmp(const void *a, const void *b)
{
    int32_t ka = ((const line_ref_t *)a)->key;
    int32_t kb = ((const line_ref_t *)b)->key;
    return (ka > kb) - (ka < kb);
}

static void fill_water_tiles(draw_ctx_t *ctx, const mf_map_t *m, const mf_frame_t *frame)
{
    if (frame->water_tile_cnt <= 0)
        return;
    const mf_subfile_t *sf = mf_get_subfile(m, ctx->zoom);
    if (!sf)
        return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(C_WATER);
    dsc.bg_opa = LV_OPA_COVER;

    for (int32_t i = 0; i < frame->water_tile_cnt; i++) {
        int32_t tx = frame->water_tiles[i].tx;
        int32_t ty = frame->water_tiles[i].ty;
        double lon0 = mf_tile_x_to_lon(tx, sf->base_zoom);
        double lat0 = mf_tile_y_to_lat(ty, sf->base_zoom);
        double lon1 = mf_tile_x_to_lon(tx + 1, sf->base_zoom);
        double lat1 = mf_tile_y_to_lat(ty + 1, sf->base_zoom);
        lv_point_t p0, p1;
        mf_view_project(ctx->view, (int32_t)llround(lat0 * 1e6), (int32_t)llround(lon0 * 1e6), &p0);
        mf_view_project(ctx->view, (int32_t)llround(lat1 * 1e6), (int32_t)llround(lon1 * 1e6), &p1);
        lv_coord_t x1 = p0.x < p1.x ? p0.x : p1.x;
        lv_coord_t y1 = p0.y < p1.y ? p0.y : p1.y;
        lv_coord_t x2 = p0.x > p1.x ? p0.x : p1.x;
        lv_coord_t y2 = p0.y > p1.y ? p0.y : p1.y;
        if (x2 < 0 || y2 < 0 || x1 > ctx->view->w || y1 > ctx->view->h)
            continue;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > ctx->view->w) x2 = ctx->view->w;
        if (y2 > ctx->view->h) y2 = ctx->view->h;
        lv_canvas_draw_rect(ctx->canvas, x1, y1, x2 - x1 + 1, y2 - y1 + 1, &dsc);
    }
}

/*
 * Render a query frame onto the canvas: background grid, land/water
 * fills (index water tiles, sea/nosea polygons, coastline flood-fill),
 * area fills, roads and labels, ordered by style group and layer.
 */
void mf_render(mf_map_t *m, const mf_view_t *v, mf_frame_t *frame, lv_obj_t *canvas)
{
    draw_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.canvas = canvas;
    ctx.view = v;
    ctx.zoom = v->zoom;

    /* resolve styles once for the whole frame */
    mf_style_t *styles = NULL;
    if (frame->count > 0) {
        styles = malloc((size_t)frame->count * sizeof(mf_style_t));
        if (!styles)
            return;
        for (int32_t i = 0; i < frame->count; i++)
            mf_style_get(m, &frame->objs[i], &styles[i]);
    }

    /* background: semi-transparent grid pattern (marks areas with no map data) */
    lv_canvas_fill_bg(canvas, lv_color_hex(C_NO_DATA_BG), LV_OPA_COVER);

    /* project map data bbox to pixel coords */
    lv_coord_t bx1, by1, bx2, by2;
    {
        lv_point_t tl, br;
        mf_view_project(v, m->bbox_min.lat_e6, m->bbox_min.lon_e6, &tl);
        mf_view_project(v, m->bbox_max.lat_e6, m->bbox_max.lon_e6, &br);
        bx1 = tl.x < br.x ? tl.x : br.x;
        by1 = tl.y < br.y ? tl.y : br.y;
        bx2 = tl.x > br.x ? tl.x : br.x;
        by2 = tl.y > br.y ? tl.y : br.y;
        if (bx1 < 0) bx1 = 0;
        if (by1 < 0) by1 = 0;
        if (bx2 > v->w) bx2 = v->w;
        if (by2 > v->h) by2 = v->h;
    }

    /* fill map data bbox with opaque land color (covers the grid in data area) */
    {
        lv_draw_rect_dsc_t ld;
        lv_draw_rect_dsc_init(&ld);
        ld.bg_color = lv_color_hex(C_LAND_BG);
        ld.bg_opa = LV_OPA_COVER;
        lv_canvas_draw_rect(canvas, bx1, by1, bx2 - bx1, by2 - by1, &ld);
    }

    /* full-water tiles from the index (covers open sea) */
    fill_water_tiles(&ctx, m, frame);

    /* sea tile rects (crude, needed where no coastline data exists) */
    draw_area_group(&ctx, frame, styles, MF_G_WATER, 0, 0);

    /* sea + land from coastline ways where available (true coast shape) */
    {
        static int8_t nocoast = -1;
        if (nocoast < 0)
            nocoast = getenv("MF_NOCOAST") != NULL;
        if (!nocoast)
            fill_coastline(&ctx, m, frame, styles);
    }

    /* nosea land-claim polygons + inland water (on top of the coastline fill) */
    draw_area_group(&ctx, frame, styles, MF_G_WATER, 1, 1000);

    /* remaining area passes, in group order */
    static const mf_group_t area_order[] = {
        MF_G_LANDUSE, MF_G_GREEN, MF_G_PEDAREA, MF_G_BUILDING
    };
    for (unsigned a = 0; a < sizeof(area_order) / sizeof(area_order[0]); a++) {
        for (int32_t i = 0; i < frame->count; i++) {
            const mf_obj_t *o = &frame->objs[i];
            if (o->type != MF_OBJ_WAY)
                continue;
            const mf_style_t *s = &styles[i];
            if (s->group != area_order[a] || v->zoom < s->min_zoom)
                continue;
            draw_area(&ctx, o, s);
        }
    }

    /* linear features sorted by (layer, rank): minor first, major on top */
    line_ref_t *lines = NULL;
    int32_t line_cnt = 0;
    if (frame->count > 0) {
        lines = malloc((size_t)frame->count * sizeof(line_ref_t));
        if (lines) {
            for (int32_t i = 0; i < frame->count; i++) {
                const mf_obj_t *o = &frame->objs[i];
                const mf_style_t *s = &styles[i];
                if (o->type != MF_OBJ_WAY || s->group != MF_G_LINE || v->zoom < s->min_zoom)
                    continue;
                lines[line_cnt].key = (o->layer + 5) * 100 + s->rank;
                lines[line_cnt].idx = i;
                line_cnt++;
            }
            qsort(lines, (size_t)line_cnt, sizeof(line_ref_t), line_ref_cmp);
        }
    }

    {
        static int8_t dbg = -1;
        if (dbg < 0)
            dbg = getenv("MF_DEBUG") != NULL;
        if (dbg)
            fprintf(stderr, "mf: frame=%d lines=%d zoom=%d\n",
                    (int)frame->count, (int)line_cnt, (int)v->zoom);
    }

    /* pass 1: all casings */
    for (int32_t i = 0; i < line_cnt; i++)
        draw_line_way(&ctx, &frame->objs[lines[i].idx], &styles[lines[i].idx], true);
    /* pass 2: all fills */
    for (int32_t i = 0; i < line_cnt; i++)
        draw_line_way(&ctx, &frame->objs[lines[i].idx], &styles[lines[i].idx], false);

    /* labels: place names first (most important) */
    for (int32_t i = 0; i < frame->count; i++) {
        const mf_obj_t *o = &frame->objs[i];
        const mf_style_t *s = &styles[i];
        if (o->type != MF_OBJ_POI || s->rank < 90 || !o->name)
            continue;
        if (v->zoom < s->label_zoom || v->zoom > s->label_max_zoom)
            continue;
        lv_point_t p;
        if (mf_view_project(v, o->pos.lat_e6, o->pos.lon_e6, &p) != 0)
            continue;
        text_halo(&ctx, p.x, p.y, o->name, s->label_size, s->label_color);
    }

    /* road names, major roads first */
    for (int32_t i = line_cnt - 1; i >= 0; i--) {
        const mf_obj_t *o = &frame->objs[lines[i].idx];
        const mf_style_t *s = &styles[lines[i].idx];
        if (s->label_zoom < 0 || v->zoom < s->label_zoom)
            continue;
        draw_way_label(&ctx, o, s);
    }

    /* green area names (parks) */
    for (int32_t i = 0; i < frame->count; i++) {
        const mf_obj_t *o = &frame->objs[i];
        const mf_style_t *s = &styles[i];
        if (o->type != MF_OBJ_WAY || s->group != MF_G_GREEN)
            continue;
        if (s->label_zoom < 0 || v->zoom < s->label_zoom || v->zoom < s->min_zoom)
            continue;
        draw_area_label(&ctx, o, s);
    }

    /* POI dots + names */
    for (int32_t i = 0; i < frame->count; i++) {
        const mf_obj_t *o = &frame->objs[i];
        if (o->type != MF_OBJ_POI)
            continue;
        draw_poi(&ctx, o, &styles[i]);
    }

    free(lines);
    free(styles);

    /* post-process: restore no-data grid in areas outside the map data bbox.
     * First fill with the no-data base color, then draw grid lines on top.
     * This overwrites any coastline/water/land leakage into no-data zones. */
    {
        lv_draw_rect_dsc_t nd;
        lv_draw_rect_dsc_init(&nd);
        nd.bg_color = lv_color_hex(C_OUTSIDE_BG);
        nd.bg_opa = LV_OPA_COVER;
        /* left strip */
        if (bx1 > 0)
            lv_canvas_draw_rect(canvas, 0, 0, bx1, v->h, &nd);
        /* right strip */
        if (bx2 < v->w)
            lv_canvas_draw_rect(canvas, bx2, 0, v->w - bx2, v->h, &nd);
        /* top strip (between left/right) */
        if (by1 > 0 && bx1 < bx2)
            lv_canvas_draw_rect(canvas, bx1, 0, bx2 - bx1, by1, &nd);
        /* bottom strip (between left/right) */
        if (by2 < v->h && bx1 < bx2)
            lv_canvas_draw_rect(canvas, bx1, by2, bx2 - bx1, v->h - by2, &nd);

        lv_draw_line_dsc_t gd;
        lv_draw_line_dsc_init(&gd);
        gd.color = lv_color_hex(C_GRID_LINE);
        gd.width = 1;
        gd.opa = LV_OPA_70;
        lv_point_t p[2];
        int32_t step = 16;
        for (int32_t y = 0; y < v->h; y += step) {
            if (y >= by1 && y <= by2) {
                if (bx1 > 0) {
                    p[0] = (lv_point_t){0, (lv_coord_t)y};
                    p[1] = (lv_point_t){(lv_coord_t)(bx1 - 1), (lv_coord_t)y};
                    lv_canvas_draw_line(canvas, p, 2, &gd);
                }
                if (bx2 < v->w - 1) {
                    p[0] = (lv_point_t){(lv_coord_t)(bx2 + 1), (lv_coord_t)y};
                    p[1] = (lv_point_t){(lv_coord_t)(v->w - 1), (lv_coord_t)y};
                    lv_canvas_draw_line(canvas, p, 2, &gd);
                }
            } else {
                p[0] = (lv_point_t){0, (lv_coord_t)y};
                p[1] = (lv_point_t){(lv_coord_t)(v->w - 1), (lv_coord_t)y};
                lv_canvas_draw_line(canvas, p, 2, &gd);
            }
        }
        for (int32_t x = 0; x < v->w; x += step) {
            if (x >= bx1 && x <= bx2) {
                if (by1 > 0) {
                    p[0] = (lv_point_t){(lv_coord_t)x, 0};
                    p[1] = (lv_point_t){(lv_coord_t)x, (lv_coord_t)(by1 - 1)};
                    lv_canvas_draw_line(canvas, p, 2, &gd);
                }
                if (by2 < v->h - 1) {
                    p[0] = (lv_point_t){(lv_coord_t)x, (lv_coord_t)(by2 + 1)};
                    p[1] = (lv_point_t){(lv_coord_t)x, (lv_coord_t)(v->h - 1)};
                    lv_canvas_draw_line(canvas, p, 2, &gd);
                }
            } else {
                p[0] = (lv_point_t){(lv_coord_t)x, 0};
                p[1] = (lv_point_t){(lv_coord_t)x, (lv_coord_t)(v->h - 1)};
                lv_canvas_draw_line(canvas, p, 2, &gd);
            }
        }
    }
}
