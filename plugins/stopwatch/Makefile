#
# Stopwatch plugin makefile
#
# Builds the same source twice:
#   make        -> build/stopwatch.so       (ARM, device: upload to /data/apps)
#   make sim    -> build_sim/stopwatch.so   (x86-64 host, for the SDL sim
#                                             via PICO_NAV_APPS=...)
#
# The plugin links against NOTHING: every symbol (LVGL, page registry,
# app registry) resolves from the host binary at dlopen() time - the
# host exports them through app.exports.
#
ROOT               ?= ../..
BUILDROOT_TOOLCHAIN ?= $(HOME)/code/odometer/buildroot/buildroot-2023.02.6/output/host/bin
CROSS_COMPILE      ?= $(BUILDROOT_TOOLCHAIN)/arm-buildroot-linux-gnueabihf-

WARNINGS := -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I$(ROOT) -I$(ROOT)/lvgl -I$(ROOT)/App/Utils -I$(ROOT)/App/Pages
CFLAGS_COMMON = -std=c99 -DLV_CONF_INCLUDE_SIMPLE $(INCLUDES) $(WARNINGS) \
                -fPIC -shared -fno-strict-aliasing

all: build/stopwatch.so

build/stopwatch.so: stopwatch.c $(ROOT)/App/Utils/plugin_api.h
	@mkdir -p $(dir $@)
	$(CROSS_COMPILE)gcc -O2 -g0 $(CFLAGS_COMMON) -o $@ stopwatch.c
	@echo "built $@ (device, upload to /data/apps/)"

build_sim/stopwatch.so: stopwatch.c $(ROOT)/App/Utils/plugin_api.h
	@mkdir -p $(dir $@)
	gcc -O1 -g $(CFLAGS_COMMON) -o $@ stopwatch.c
	@echo "built $@ (host sim, run with PICO_NAV_APPS=$(abspath build_sim))"

sim: build_sim/stopwatch.so

clean:
	rm -rf build build_sim

.PHONY: all sim clean
