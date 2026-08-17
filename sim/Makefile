#
# 模拟器 Makefile (PC 主机编译 + SDL2)
# 用法: make           # 编译
#       make clean
# 产物: build_sim/bin/pico_nav_sim
# 运行: ./build_sim/bin/pico_nav_sim [sample.nmea]
#
CC           = gcc
LVGL_DIR     = ..
LVGL_DIR_NAME = lvgl

WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
            -Wno-missing-field-initializers -Wno-sign-compare -Wno-cast-qual

CFLAGS  := -O2 -g -I$(LVGL_DIR)/ -I$(LVGL_DIR)/$(LVGL_DIR_NAME)/ -I. $(shell sdl2-config --cflags) \
           -DLV_CONF_INCLUDE_SIMPLE $(WARNINGS) -std=c99
LDFLAGS := -lm $(shell sdl2-config --libs)

BIN        = pico_nav_sim
BUILD_DIR  = ./build_sim
OBJ_DIR    = $(BUILD_DIR)/obj
BIN_DIR    = $(BUILD_DIR)/bin

# 模拟器源码: 应用框架 + 模拟 HAL + SDL 入口
MAINSRC = main_sim.c \
          hal_keys_sdl.c \
          hal_gps_nmea.c \
          $(wildcard ../App/*.c) \
          $(wildcard ../App/Utils/*.c) \
          $(wildcard ../App/Pages/*.c)

include $(LVGL_DIR)/lvgl/lvgl.mk

OBJEXT = .o
COBJS = $(CSRCS:.c=$(OBJEXT))
MAINOBJ = $(MAINSRC:.c=$(OBJEXT))
SRCS = $(CSRCS) $(MAINSRC)
OBJS = $(COBJS) $(MAINOBJ)
TARGET = $(addprefix $(OBJ_DIR)/, $(patsubst ../%, sim/%, $(patsubst ./%, %, $(OBJS))))

all: default

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

$(OBJ_DIR)/sim/%.o: ../%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(TARGET)
	@mkdir -p $(BIN_DIR)
	$(CC) -o $(BIN_DIR)/$(BIN) $(TARGET) $(LDFLAGS)
	@echo ""
	@echo "编译完成: $(BIN_DIR)/$(BIN)"
	@echo "运行: cd sim && ./build_sim/bin/pico_nav_sim [sample.nmea]"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean