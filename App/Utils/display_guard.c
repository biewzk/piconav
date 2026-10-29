#include "display_guard.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Overridable so the guard logic can be unit-tested on a host (see
 * app/pico_nav/tools/test_display_guard.sh). */
#ifndef DISPLAY_GUARD_FB
#define DISPLAY_GUARD_FB "/dev/fb0"
#endif
#define FB_DEV DISPLAY_GUARD_FB
#define PROC_FD "/proc/%d/fd"

/* 1 when the process has FB_DEV open. */
static int pid_owns_fb(int pid)
{
    char dir[64];
    DIR *dp;

    snprintf(dir, sizeof dir, PROC_FD, pid);
    dp = opendir(dir);
    if (!dp)
        return 0;
    for (;;) {
        struct dirent *ent = readdir(dp);
        char path[384], link[512];
        ssize_t n;

        if (!ent) {
            closedir(dp);
            return 0;
        }
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        n = readlink(path, link, sizeof link - 1);
        if (n <= 0)
            continue;
        link[n] = 0;
        if (!strcmp(link, FB_DEV)) {
            closedir(dp);
            return 1;
        }
    }
}

/* The screenshot helper opens /dev/fb0 for a few milliseconds; yielding
 * to it every time would repaint the whole UI for nothing. */
static int is_screenshot_reader(int pid)
{
    char path[64], comm[32];
    FILE *f;

    snprintf(path, sizeof path, "/proc/%d/comm", pid);
    f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(comm, sizeof comm, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    comm[strcspn(comm, "\n")] = 0;
    return !strcmp(comm, "webctl");
}

int display_guard_owner_pid(void)
{
    DIR *dp = opendir("/proc");
    struct dirent *ent;

    if (!dp)
        return 0;
    for (;;) {
        int pid;

        ent = readdir(dp);
        if (!ent)
            break;
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9')
            continue;
        pid = atoi(ent->d_name);
        if (pid <= 0 || pid == (int)getpid())
            continue;
        if (is_screenshot_reader(pid))
            continue;
        if (pid_owns_fb(pid)) {
            closedir(dp);
            return pid;
        }
    }
    closedir(dp);
    return 0;
}
