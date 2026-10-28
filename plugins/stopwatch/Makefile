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
	@echo "built $@ (device, legacy bare .so)"

# App package: manifest + code + icon resource, installed by webctl
# (PUT /api/upload?mode=app) or the web UI into /data/apps/stopwatch/.
APP_VER := $(shell sed -n 's/^version=//p' manifest.txt)
APP_BIN := build/stopwatch-$(APP_VER).app

app: $(APP_BIN)

$(APP_BIN): stopwatch.c $(ROOT)/App/Utils/plugin_api.h manifest.txt \
            assets/icon.rgb565a8 $(ROOT)/tools/pack_app.py
	@mkdir -p build/app
	cp manifest.txt build/app/manifest.txt
	cp assets/icon.rgb565a8 build/app/icon.rgb565a8
	$(CROSS_COMPILE)gcc -O2 -g0 $(CFLAGS_COMMON) -o build/app/stopwatch.so stopwatch.c
	python3 $(ROOT)/tools/pack_app.py build/app -o $@
	@echo "built $@ (device package, install via webctl mode=app)"

build_sim/stopwatch.so: stopwatch.c $(ROOT)/App/Utils/plugin_api.h
	@mkdir -p $(dir $@)
	gcc -O1 -g $(CFLAGS_COMMON) -o $@ stopwatch.c
	@echo "built $@ (host sim, run with PICO_NAV_APPS=$(abspath build_sim))"

sim: build_sim/stopwatch.so

clean:
	rm -rf build build_sim

.PHONY: all app sim clean
