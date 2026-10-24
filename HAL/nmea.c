#include "nmea.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NMEA_FIELDS_MAX 24

/* Reset the parser and the merged snapshot. */
void nmea_init(nmea_parser_t *p)
{
    if (!p)
        return;
    memset(p, 0, sizeof(*p));
    p->data.utc = 0xFFFFFFFFu;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* Validate the "*hh" checksum tail; a sentence without one is accepted
 * (some receivers omit it in proprietary modes). */
static int checksum_ok(const char *line)
{
    const char *star = strchr(line, '*');
    if (!star)
        return 1;

    int hi = hex_val(star[1]);
    int lo = hi < 0 ? -1 : hex_val(star[2]);
    if (lo < 0)
        return 0;

    unsigned sum = 0;
    for (const char *c = line; c < star; c++)
        sum ^= (unsigned char)*c;

    return (int)(sum & 0xFF) == (hi << 4 | lo);
}

/* Split a NUL-terminated sentence body (checksum already cut off) into
 * comma-separated fields in place. Returns the field count. */
static int split_fields(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (char *c = line; *c && n < max; c++) {
        if (*c == ',') {
            *c = 0;
            f[n++] = c + 1;
        }
    }
    return n;
}

/* "ddmm.mmmm" / "dddmm.mmmm" -> signed degrees (sign applied by caller). */
static int parse_angle(const char *v, double *out)
{
    if (!v || !*v)
        return 0;

    char *end = NULL;
    double raw = strtod(v, &end);
    if (end == v || raw < 0 || raw >= 36000.0)
        return 0;

    int deg = (int)(raw / 100.0);
    double min = raw - (double)deg * 100.0;
    if (min >= 60.0)
        return 0;

    *out = (double)deg + min / 60.0;
    return 1;
}

/* "hhmmss.ss" -> seconds since midnight UTC. */
static int parse_utc(const char *v, uint32_t *out)
{
    int hh = 0, mm = 0;
    double ss = 0.0;
    if (!v || strlen(v) < 6)
        return 0;
    if (sscanf(v, "%2d%2d%lf", &hh, &mm, &ss) != 3)
        return 0;
    if (hh > 23 || mm > 59 || ss >= 61.0)
        return 0;

    *out = (uint32_t)(hh * 3600 + mm * 60 + (int)ss);
    return 1;
}

/* Apply the lat/lon field pair (value + N/S or E/W) when both are present. */
static void apply_position(gps_data_t *d, const char *lat_s, const char *ns,
                           const char *lon_s, const char *ew)
{
    double lat, lon;
    if (!parse_angle(lat_s, &lat) || !parse_angle(lon_s, &lon))
        return;
    d->lat = (ns && ns[0] == 'S') ? -lat : lat;
    d->lon = (ew && ew[0] == 'W') ? -lon : lon;
}

/* $--GGA: time, lat, NS, lon, EW, quality, sats, hdop, alt, ... */
static int parse_gga(nmea_parser_t *p, char **f, int nf)
{
    if (nf < 10)
        return 0;

    gps_data_t *d = &p->data;
    int quality = atoi(f[6]);

    d->fix = quality > 0;
    if (quality > 0) {
        /* No-fix GGA sentences carry empty/placeholder values (99.99
         * HDOP, empty altitude) — keep the last valid solution instead. */
        d->sats  = (uint8_t)atoi(f[7]);
        d->hdop  = (float)atof(f[8]);
        d->alt_m = (float)atof(f[9]);
        apply_position(d, f[2], f[3], f[4], f[5]);
    }

    uint32_t utc;
    if (parse_utc(f[1], &utc))
        d->utc = utc;
    return 1;
}

/* $--RMC: time, status, lat, NS, lon, EW, speed(kn), course, date, ... */
static int parse_rmc(nmea_parser_t *p, char **f, int nf)
{
    if (nf < 9)
        return 0;

    gps_data_t *d = &p->data;
    int valid = (f[2][0] == 'A');

    d->fix = (uint8_t)valid;
    if (valid) {
        apply_position(d, f[3], f[4], f[5], f[6]);
        d->speed_kmh  = (float)(atof(f[7]) * 1.852);   /* knots -> km/h */
        d->course_deg = (float)atof(f[8]);
    } else {
        d->speed_kmh  = 0.0f;
    }

    uint32_t utc;
    if (parse_utc(f[1], &utc))
        d->utc = utc;
    return 1;
}

/* Decode one complete sentence body (without '$', without checksum). */
static int parse_sentence(nmea_parser_t *p, char *line)
{
    /* Drop the checksum tail before splitting; keep validating on the
     * original string first (checksum_ok needs the '*'). */
    char *star = strchr(line, '*');
    if (star)
        *star = 0;

    char *f[NMEA_FIELDS_MAX];
    int nf = split_fields(line, f, NMEA_FIELDS_MAX);
    if (nf < 2 || strlen(f[0]) < 5)
        return 0;

    /* f[0] = talker + sentence id, e.g. "GNGGA" */
    const char *id = f[0] + 2;
    int updated;
    if (strncmp(id, "GGA", 3) == 0)
        updated = parse_gga(p, f, nf);
    else if (strncmp(id, "RMC", 3) == 0)
        updated = parse_rmc(p, f, nf);
    else
        return 0;

    if (updated)
        p->data.stamp_ms = hal_tick_get();
    return updated;
}

/* Terminate and parse the pending line (if any), then reset the buffer.
 * Called on CR/LF and also when the next '$' shows up, so sentences still
 * parse when the terminator is missing (file replay without CRLF). */
static int finish_line(nmea_parser_t *p)
{
    int updated = 0;

    if (p->len > 0 && !p->overflow) {
        p->line[p->len] = 0;
        if (checksum_ok(p->line))
            updated = parse_sentence(p, p->line);
    }
    p->len = 0;
    p->overflow = 0;
    return updated;
}

/* Feed raw UART bytes; assembles '$'-delimited lines and parses them. */
int nmea_feed(nmea_parser_t *p, const char *buf, int len)
{
    if (!p || !buf)
        return 0;

    int updated = 0;

    for (int i = 0; i < len; i++) {
        char c = buf[i];

        if (c == '$' || c == '\r' || c == '\n') {
            updated |= finish_line(p);
            continue;
        }
        if (p->len >= NMEA_LINE_MAX - 1) {
            p->overflow = 1;
            continue;
        }
        p->line[p->len++] = c;
    }

    return updated;
}
