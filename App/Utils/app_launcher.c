#include "app_launcher.h"
#include "log.h"
#include "gps_service.h"
#include "../../HAL/hal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lvgl.h"

#define LAUNCH_PATH_MAX 256

static char s_pending[LAUNCH_PATH_MAX];

void app_launcher_request(const char *path)
{
    if (!path || !path[0])
        return;
    snprintf(s_pending, sizeof s_pending, "%s", path);
}

int app_launcher_pending(void)
{
    return s_pending[0] != 0;
}

/*
 * Service one launch request. Runs in the main loop, never inside an
 * LVGL callback, so suspending the GUI here is safe: the LVGL timer
 * handler is simply not called again until the child exits.
 */
void app_launcher_run(void)
{
    char path[LAUNCH_PATH_MAX];
    struct stat st;
    pid_t pid;
    int status = 0;

    snprintf(path, sizeof path, "%s", s_pending);
    s_pending[0] = 0;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        LOG_W("launcher: %s is missing, skipped", path);
        return;
    }
    if (access(path, X_OK) != 0) {
        LOG_W("launcher: %s is not executable, skipped", path);
        return;
    }

    LOG_I("launcher: starting %s (GUI suspended)", path);

    /* Hand the hardware over: the child opens /dev/fb0, the key device
     * and (if it needs it) the GPS UART itself. */
    gps_service_suspend();
    hal_key_shutdown();

    pid = fork();
    if (pid == 0) {
        execl(path, path, (char *)NULL);
        LOG_E("launcher: exec %s failed: %s", path, strerror(errno));
        _exit(127);
    }
    if (pid < 0) {
        LOG_E("launcher: fork failed: %s", strerror(errno));
    } else {
        if (waitpid(pid, &status, 0) < 0)
            LOG_W("launcher: waitpid failed: %s", strerror(errno));
        else if (WIFEXITED(status))
            LOG_I("launcher: %s exited (code %d)", path, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            LOG_W("launcher: %s killed by signal %d", path, WTERMSIG(status));
    }

    /* Re-acquire the hardware and repaint: the child scribbled the
     * framebuffer directly, our LVGL tree still holds the old frame. */
    hal_key_init();
    hal_key_flush();
    gps_service_resume();

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    LOG_I("launcher: GUI resumed");
}
