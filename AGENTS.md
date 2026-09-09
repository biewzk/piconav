# AGENTS.md

C firmware project: LVGL v8.1.0 GPS odometer/nav GUI (240x320 RGB565) for an ARM board, with an SDL2 PC simulator built from shared sources. Detailed architecture docs live in `README.md` and `Doc/` (Chinese) — consult those for page/map-renderer internals.

## Builds (two independent Makefiles)

- **Device (ARM)**: `./build.sh` at repo root → `build/bin/demo`. Cross-compiles with buildroot toolchain `~/code/odometer/buildroot/buildroot-2023.02.6/output/host/bin/` (override with `make CROSS_COMPILE=...`). Binary runs on the board's `/dev/fb0`; cannot run on host.
- **Simulator (host)**: must run from `sim/`: `./build_sim.sh` or `./build_sim.sh run [file.nmea]` → `build_sim/bin/pico_nav_sim`. Requires SDL2 (`libsdl2-dev`).
- **Trap**: `README.md` says `./build.sh sim` builds the simulator — it doesn't. `build.sh` ignores the argument and silently rebuilds the ARM target.
- Both Makefiles wildcard-glob sources (`App/*.c`, `App/Pages/*.c`, `App/Utils/*.c`, `Mapsforge/*.c`, plus per-target `HAL/` or `sim/`). New `.c` files need no Makefile edit.

## Verification (no test suite, no lint config)

Smoke-test via headless simulator from `sim/`:

```bash
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1500 \
  SIM_DUMP_PPM=/tmp/sim.ppm ./build_sim/bin/pico_nav_sim sample.nmea
```

- Check rendering by inspecting the dumped PPM.
- Scripted key input: `SIM_KEY="right,right,enter,down,back"` + `SIM_KEY_MS=300`.
- Map/view selection: `MF_MAP=../resources/macau.map`, optional `SIM_LAT/SIM_LON/SIM_ZOOM`.
- Debug flags: `MF_DEBUG=1` (frame stats), `MF_NOCOAST=1` (skip coastline flood fill), `LOG_CONSOLE=0`.
- Sim logs mirror to stderr and host syslog (`/var/log/syslog`).

## Architecture in one breath

Platform entry (`main.c` / `sim/main_sim.c`) → `App/app.c` poll loop (`app_run()`: read keys→publish `MSG_KEY`, GPS→`MSG_GPS`, `lv_timer_handler()`, 5ms delay). Framework in `App/Utils/`: `page_manager` (page stack, lifecycle callbacks, 500ms OVER_LEFT/RIGHT transitions) + `msg_center` (pub/sub). Root page is `page_home` (Carousel panel switching with `lv_anim_t` slide animation + watch face with `lv_anim_timeline` entrance). `app_registry` maps app names/icons to `page_t*`; apps are opened via `page_manager_push()` from the app drawer (`page_app_drawer`, embedded in Carousel panel 1). `Mapsforge/` is a self-contained .map v5 renderer that depends only on LVGL canvas and `App/Utils/log` — no App framework dependency.

## Conventions & gotchas

- Button enums MUST be prefixed `KEY_BTN_*` (`HAL/hal.h`), never `KEY_*` — collides with `linux/input-event-codes.h` macros and silently breaks navigation on device.
- Pages subscribe messages in `on_show` and unsubscribe in `on_hide`; pass page state via `page_t.user`, not globals.
- HAL has two implementations behind `hal.h`: device (`HAL/hal_keys_evdev.c`, GPS stub) vs sim (`sim/hal_keys_sdl.c`, NMEA replay). Each Makefile picks its own set.
- Currently `app_init()` pushes `page_home` directly as root page (map dev mode); startup/dialplate/settings pages still exist but are not shown first.
- `resources/macau.map` is tracked in git; other `.map` files (e.g. `monaco.map`, `ningxia.map`) can be added by the user.
- Log strings are English (`LOG_E/W/I/D` via syslog); repo docs are Chinese.
