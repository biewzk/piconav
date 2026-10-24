# AGENTS.md

C firmware project: LVGL v8.1.0 GPS odometer/nav GUI (240x320 RGB565) for an ARM board, with an SDL2 PC simulator built from shared sources. Detailed architecture docs live in `README.md` and `Doc/` (Chinese) — consult those for page/map-renderer internals.

## Builds (two independent Makefiles)

- **Device (ARM)**: `./build.sh` at repo root → `build/bin/demo`. Cross-compiles with buildroot toolchain `~/code/odometer/buildroot/buildroot-2023.02.6/output/host/bin/` (override with `make CROSS_COMPILE=...`). Binary runs on the board's `/dev/fb0`; cannot run on host.
- **Simulator (host)**: must run from `sim/`: `./build_sim.sh` or `./build_sim.sh run [file.nmea]` → `build_sim/bin/pico_nav_sim`. Requires SDL2 (`libsdl2-dev`).
- **Trap**: `README.md` says `./build.sh sim` builds the simulator — it doesn't. `build.sh` ignores the argument and silently rebuilds the ARM target.
- Both Makefiles wildcard-glob sources (`App/*.c`, `App/Pages/*.c`, `App/Utils/*.c`, `Mapsforge/*.c`, plus per-target `HAL/` or `sim/`). New `.c` files need no Makefile edit **except** shared HAL sources: `sim/Makefile` lists `../HAL/nmea.c` explicitly (the device build gets it via the `HAL/*.c` glob).

## Verification (no test suite, no lint config)

Smoke-test via headless simulator from `sim/`:

```bash
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1500 \
  SIM_DUMP_PPM=/tmp/sim.ppm ./build_sim/bin/pico_nav_sim sample.nmea
```

- Check rendering by inspecting the dumped PPM. The dump forces a full-screen redraw first (`lv_obj_invalidate(lv_scr_act)` + `lv_refr_now`); without it the buffer only holds the last refreshed area at offset 0 and the image looks stale.
- Scripted key input: `SIM_KEY="right,enter,enter:800"` + `SIM_KEY_MS=400`. Token `"name:hold_ms"` holds the key (long-press gesture); plain `"name"` is a 60 ms click. Events are pushed from an lv_timer during `app_run` (the long-press detector needs `lv_timer_handler` running).
- Map/view selection: `MF_MAP=../resources/macau.map` (single file, wins over everything), `MF_MAP_DIR=../resources` (directory scanned by the map picker page), optional `SIM_LAT/SIM_LON/SIM_ZOOM`.
- Debug flags: `MF_DEBUG=1` (frame stats), `MF_NOCOAST=1` (skip coastline flood fill), `LOG_CONSOLE=0`.
- Sim logs mirror to stderr and host syslog (`/var/log/syslog`).
- Device maps live in `/data/maps` (data partition, UBIFS via `S41data`); the drawer's map entry opens `page_mapselect` which lists that directory, persists the choice to config key `map.selected` and opens it in `page_livemap` (`page_livemap_set_map()`). In sim `/dev/config` is absent — the picker still works but persistence is skipped (logged as a warning).

## Architecture in one breath

Platform entry (`main.c` / `sim/main_sim.c`) → `App/app.c` poll loop (`app_run()`: read keys→publish `MSG_KEY`, GPS→`MSG_GPS`, `lv_timer_handler()`, 5ms delay). Framework in `App/Utils/`: `page_manager` (page stack, lifecycle callbacks, 500ms OVER_LEFT/RIGHT transitions) + `msg_center` (pub/sub). Root page is `page_home` (Carousel panel switching with `lv_anim_t` slide animation + watch face with `lv_anim_timeline` entrance). `app_registry` maps app names/icons to `page_t*`; apps are opened via `page_manager_push()` from the app drawer (`page_app_drawer`, embedded in Carousel panel 1). `Mapsforge/` is a self-contained .map v5 renderer that depends only on LVGL canvas and `App/Utils/log` — no App framework dependency.

## Conventions & gotchas

- Button enums MUST be prefixed `KEY_BTN_*` (`HAL/hal.h`), never `KEY_*` — collides with `linux/input-event-codes.h` macros and silently breaks navigation on device.
- Pages subscribe messages in `on_show` and unsubscribe in `on_hide`; pass page state via `page_t.user`, not globals.
- HAL has two implementations behind `hal.h`: device (`HAL/hal_keys_evdev.c` + `HAL/hal_gps_uart.c`) vs sim (`sim/hal_keys_sdl.c` + `sim/hal_gps_nmea.c`). The NMEA-0183 parser (`HAL/nmea.c`, GGA/RMC) is shared by both.
- GPS: device opens `/dev/ttyS3` (UART3_M1, header pins 19/20, 9600 8N1, `GPS_DEV`/`GPS_BAUD` overrides) and retries every 5 s if the node is missing; sim replays `SIM_NMEA` (default `sample.nmea`) with `SIM_GPS_MS` per sentence (default 100). `app_run()` publishes snapshots on `MSG_GPS`; `app_gps()` returns the last one. Consumers: `page_dialplate` (speed / trip distance / moving time, statics survive push-pop) and `page_livemap` (GPS follow, disabled by manual pan).
- `LV_SPRINTF_USE_FLOAT` is 0: `lv_label_set_text_fmt()` does **not** support `%f` (prints junk). Format floats with libc `snprintf()` first, then `lv_label_set_text()`.
- Currently `app_init()` pushes `page_home` directly as root page (map dev mode); startup/dialplate/settings pages still exist but are not shown first.
- `resources/macau.map` is tracked in git; other `.map` files (e.g. `monaco.map`, `ningxia.map`) can be added by the user.
- Log strings are English (`LOG_E/W/I/D` via syslog); repo docs are Chinese.
