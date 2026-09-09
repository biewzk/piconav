#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "App/app.h"
#include "App/Utils/log.h"
#include "HAL/hal.h"

#include <unistd.h>
#include <stdlib.h>

#include "App/Config/app_config.h"

int main(void)
{
    log_init();
    LOG_I("pico_nav starting (fbdev %dx%d)", APP_SCR_HOR, APP_SCR_VER);

    lv_init();

    fbdev_init();
    LOG_D("fbdev init done");

    static lv_color_t buf[APP_DISP_BUF];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, APP_DISP_BUF);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = fbdev_flush;
    disp_drv.hor_res    = APP_SCR_HOR;
    disp_drv.ver_res    = APP_SCR_VER;
    lv_disp_drv_register(&disp_drv);

    app_init();
    LOG_I("app_init done, entering main loop");
    app_run();

    LOG_I("pico_nav exit");
    return 0;
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    return hal_tick_get();
}