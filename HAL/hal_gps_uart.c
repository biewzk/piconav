#define _POSIX_C_SOURCE 200809L

#include "hal.h"
#include "nmea.h"
#include "../App/Utils/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* BN-180 on UART3_M1 (header pins 19/20): 9600 8N1 by default.
 * Override for bring-up: GPS_DEV=/dev/ttyS3 GPS_BAUD=115200 ... */
#define GPS_DEFAULT_DEV    "/dev/ttyS3"
#define GPS_DEFAULT_BAUD   9600
#define GPS_REOPEN_MS      5000   /* retry interval while the node is missing */

static int          s_fd = -1;
static nmea_parser_t s_parser;
static char         s_dev[64];
static int          s_baud;
static uint32_t     s_next_open_ms;
static int          s_warned;          /* one W-level open failure, then D */
static int          s_last_fix = -1;   /* -1 = unknown, for transition logs */

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

/* Configure the tty as a raw 8N1 receiver: no echo, no CR/LF
 * translation, no flow control, no canonical line buffering. */
static int configure_tty(int fd, int baud)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0)
        return -1;

    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                     ICRNL | IXON | IXOFF | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    speed_t sp = baud_to_speed(baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);

    if (tcsetattr(fd, TCSANOW, &tio) != 0)
        return -1;

    tcflush(fd, TCIFLUSH);
    return 0;
}

/* Open + configure the UART; returns 0 on success. */
static int gps_open(void)
{
    int fd = open(s_dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        /* /dev/ttyS3 only appears once the DTS enables uart3: warn once,
         * then stay quiet while retrying. */
        if (s_warned)
            LOG_D("gps: open %s failed: %s", s_dev, strerror(errno));
        else
            LOG_W("gps: open %s failed: %s (retrying every %d ms)",
                  s_dev, strerror(errno), GPS_REOPEN_MS);
        s_warned = 1;
        return -1;
    }
    if (configure_tty(fd, s_baud) != 0) {
        LOG_W("gps: %s is not a usable tty (%s)", s_dev, strerror(errno));
        close(fd);
        return -1;
    }

    s_fd = fd;
    s_warned = 0;
    nmea_init(&s_parser);
    s_last_fix = -1;
    LOG_I("gps: listening on %s @ %d 8N1", s_dev, s_baud);
    return 0;
}

static void gps_close(void)
{
    if (s_fd >= 0)
        close(s_fd);
    s_fd = -1;
}

/*
 * Initialize the GPS HAL: parse the env overrides, reset the parser and
 * open the UART. A missing /dev node is not fatal (the port only exists
 * once the DTS enables it) — hal_gps_read() keeps retrying.
 */
int hal_gps_init(void)
{
    const char *dev = getenv("GPS_DEV");
    const char *baud = getenv("GPS_BAUD");

    snprintf(s_dev, sizeof(s_dev), "%s", (dev && dev[0]) ? dev : GPS_DEFAULT_DEV);
    s_baud = (baud && atoi(baud) > 0) ? atoi(baud) : GPS_DEFAULT_BAUD;

    nmea_init(&s_parser);
    s_next_open_ms = 0;

    if (gps_open() != 0) {
        s_next_open_ms = hal_tick_get() + GPS_REOPEN_MS;
        return -1;
    }
    return 0;
}

/*
 * Poll the UART. Returns 1 when a fresh snapshot was parsed (out is then
 * filled), 0 when nothing arrived. Handles the node appearing/disappearing
 * at runtime by reopening every GPS_REOPEN_MS.
 */
int hal_gps_read(gps_data_t *out)
{
    if (!out)
        return 0;

    uint32_t now = hal_tick_get();

    if (s_fd < 0) {
        if (now < s_next_open_ms)
            return 0;
        s_next_open_ms = now + GPS_REOPEN_MS;
        if (gps_open() != 0)
            return 0;
    }

    char buf[128];
    ssize_t n = read(s_fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        LOG_W("gps: read %s failed: %s, reopening", s_dev, strerror(errno));
        gps_close();
        s_next_open_ms = now + GPS_REOPEN_MS;
        return 0;
    }
    if (n == 0)
        return 0;

    if (!nmea_feed(&s_parser, buf, (int)n))
        return 0;

    if (s_parser.data.fix != s_last_fix) {
        s_last_fix = s_parser.data.fix;
        if (s_last_fix)
            LOG_I("gps: fix acquired (%u sats)", (unsigned)s_parser.data.sats);
        else
            LOG_W("gps: fix lost");
    }

    *out = s_parser.data;
    return 1;
}

/* Release the UART (hardware hand-off before launching a child app that
 * wants the receiver for itself). */
void hal_gps_suspend(void)
{
    gps_close();
}

/* Re-acquire: the next hal_gps_read() reopens the port immediately. */
void hal_gps_resume(void)
{
    s_next_open_ms = 0;
}
