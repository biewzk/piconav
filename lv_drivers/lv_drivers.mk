LV_DRIVERS_DIR_NAME ?= lv_drivers

# 只保留本项目用到的驱动: display/fbdev (应用通过 HAL/hal_keys_evdev.c 直接读 /dev/input, 不依赖 indev)
CSRCS += $(wildcard $(LVGL_DIR)/$(LV_DRIVERS_DIR_NAME)/display/fbdev.c)
