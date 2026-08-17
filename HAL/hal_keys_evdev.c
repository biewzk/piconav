#include "hal.h"
#include "../App/Utils/log.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/time.h>
#include <linux/input.h>

#define EVDEV_PATH "/dev/input"

/* evdev 按键码 -> key_id_t (KEY_* 为 linux 宏, KEY_BTN_* 为应用枚举) */
static int map_code(unsigned code)
{
    switch (code) {
    case KEY_UP:    return KEY_BTN_UP;
    case KEY_DOWN:  return KEY_BTN_DOWN;
    case KEY_LEFT:  return KEY_BTN_LEFT;
    case KEY_RIGHT: return KEY_BTN_RIGHT;
    case KEY_ENTER: return KEY_BTN_ENTER;
    case KEY_BACK:  return KEY_BTN_BACK;
    default:        return -1;
    }
}

static int s_fd = -1;
static int s_opened;

static int open_key_device(void)
{
    DIR *dir = opendir(EVDEV_PATH);
    if (!dir)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;

        char path[64];
        snprintf(path, sizeof(path), "%s/%s", EVDEV_PATH, ent->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        /* 检查是否含 KEY_UP/KEY_DOWN 等导航键 */
        unsigned char bits[KEY_MAX / 8 + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0) {
            if ((bits[KEY_UP / 8] & (1 << (KEY_UP % 8))) &&
                (bits[KEY_DOWN / 8] & (1 << (KEY_DOWN % 8))) &&
                (bits[KEY_ENTER / 8] & (1 << (KEY_ENTER % 8)))) {
                s_fd = fd;
                s_opened = 1;
                LOG_I("key device opened: %s", path);
                closedir(dir);
                return 0;
            }
        }
        LOG_D("skip non-key device: %s", path);
        close(fd);
    }
    closedir(dir);
    return -1;
}

void hal_delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

uint32_t hal_tick_get(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

int hal_should_quit(void)
{
    return 0;
}

int hal_key_init(void)
{
    if (!s_opened && open_key_device() != 0) {
        LOG_E("no key device found (%s)", EVDEV_PATH);
        return -1;
    }
    return 0;
}

int hal_key_read(key_event_t *ev)
{
    if (s_fd < 0)
        return 0;

    struct input_event ie;
    ssize_t n = read(s_fd, &ie, sizeof(ie));
    if (n != (ssize_t)sizeof(ie))
        return 0;

    if (ie.type != EV_KEY)
        return 0;

    int id = map_code(ie.code);
    if (id < 0)
        return 0;

    ev->id       = (key_id_t)id;
    ev->is_press = (ie.value != 0) ? 1 : 0;   /* value: 0=抬起, 1=按下, 2=按住重复 */
    return 1;
}