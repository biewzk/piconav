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
- Device maps live in `/data/maps` (data partition, UBIFS via `S41data`); the map is a **home carousel panel** (`page_livemap.c`, panel 4 — like the GPS panel, deliberately not an app-drawer entry). The panel opens one map: config key `map.selected` (set by `page_mapselect`, still openable by name for plugins) if the file exists, else the first name-sorted `.map` in `/data/maps`, else the `MF_MAP`/dev fallback. A picker selection publishes the runtime `map.selected` topic and the panel swaps the file live (`map_view_open`). In sim `/dev/config` is absent — resolution falls through to the scan/env.

## Architecture in one breath

Platform entry (`main.c` / `sim/main_sim.c`) → `App/app.c` poll loop (`app_run()`: read keys→publish `MSG_KEY`, GPS→`MSG_GPS`, `lv_timer_handler()`, 5ms delay). Framework in `App/Utils/`: `page_manager` (page registry `page_register()`/`page_find()` + page stack + lifecycle callbacks + 500ms OVER_LEFT/RIGHT transitions + the default long-ENTER back gesture, opt out with `PAGE_FLAG_NO_AUTO_BACK`) + `msg_center` (pub/sub with runtime topics via `msg_topic_register()`). Navigation is by name: `page_manager_open(name, param)`; built-ins register centrally in `App/Pages/pages_builtin.c`. Root page is `page_home` with a 4-panel carousel (`lv_anim_t` slide, watch-face entrance timeline): 0 watch face → 1 app drawer → 2 GPS receiver → 3 map. `app_registry` is a runtime list (`app_registry_add()`); the drawer (embedded in panel 1) lists it — the GPS and map panels are deliberately NOT drawer entries. Map rendering is the reusable `App/Utils/map_view.c` widget with the plan-A GPS HUD baked in (course-rotated arrow marker / fix badge / speed chip / scale bar / N chip / 2xOK recenter pill; `page_livemap.c` is the thin carousel-panel shell wiring status bar + 1 s GPS refresh + panel switching). `Mapsforge/` is a self-contained .map v5 renderer that depends only on LVGL canvas and `App/Utils/log` — no App framework dependency.

Map panel keys (all other keys go to the map): **double-tap LEFT/RIGHT = prev/next carousel panel** (the stray single-tap pan self-heals — panel re-entry forces follow + recenter), hold OK at min zoom = leave to the watch face. Map keys: arrows pan (tap step, hold-to-repeat), ENTER click zoom in, **double-click ENTER recenter + resume follow**, held ENTER zoom out (at min zoom = panel close). Dev/sim knob: `MAPVIEW_DOUBLE_TAP_MS` widens both double-tap windows (default 450 ms). The map HUD staleness policy lives in the panel: `map_view_set_gps(mv, fresh ? g : NULL)` with `gps_service_stale_ms()`.

## GUI plugins (installable apps)

`App/Utils/plugin_manager.c` dlopens `/data/apps/*.so` (override `PICO_NAV_APPS`) after the built-ins register and loads plugins that export the contract in `App/Utils/plugin_api.h` (`plugin_abi_version`/`plugin_name`/`plugin_version`/`plugin_init()`; ABI mismatch is refused, load failures only log). Plugins use the same APIs as built-in pages; the host binary exports a curated symbol set via `app.exports` (version script + `--export-dynamic`; **an executable needs BOTH** — version script alone exports nothing). Plugins stay resident for the app lifetime. Full guide: `PLUGIN_SDK.md`; example: `plugins/stopwatch/` (same source builds ARM `make` and host `make sim`).

## Conventions & gotchas

- Button enums MUST be prefixed `KEY_BTN_*` (`HAL/hal.h`), never `KEY_*` — collides with `linux/input-event-codes.h` macros and silently breaks navigation on device.
- Pages subscribe messages in `on_show` and unsubscribe in `on_hide`; pass page state via `page_t.user`, not globals.
- HAL has two implementations behind `hal.h`: device (`HAL/hal_keys_evdev.c` + `HAL/hal_gps_uart.c`) vs sim (`sim/hal_keys_sdl.c` + `sim/hal_gps_nmea.c`). The NMEA-0183 parser (`HAL/nmea.c`, GGA/RMC) is shared by both.
- GPS: device opens `/dev/ttyS3` (UART3_M1, header pins 19/20, 9600 8N1, `GPS_DEV`/`GPS_BAUD` overrides) and retries every 5 s if the node is missing; sim replays `SIM_NMEA` (default `sample.nmea`) with `SIM_GPS_MS` per sentence (default 100). `app_run()` publishes snapshots on `MSG_GPS`; `app_gps()` returns the last one. Consumers: `page_dialplate` (speed; trip distance/moving time come from the `trip_store` service, which integrates MSG_GPS app-wide), the map panel (follow + HUD via its 1 s panel timer) and the GPS receiver panel. Sample rate is demand-driven: pages/panels call `gps_service_demand_add()/demand_release()` (refcounted) from show/hide; home owns the demand for both GPS-consuming carousel panels.
- `LV_SPRINTF_USE_FLOAT` is 0: `lv_label_set_text_fmt()` does **not** support `%f` (prints junk). Format floats with libc `snprintf()` first, then `lv_label_set_text()`.
- `app_init()` order: msg_center → page_manager → HAL → gps_service/trip_store/config_watcher → `pages_builtin_register()` → `plugin_manager_init()` → `page_manager_open("home", NULL)`. There is no startup splash page (`page_startup` was dead code, removed).
- `resources/macau.map` is tracked in git; other `.map` files (e.g. `monaco.map`, `ningxia.map`) can be added by the user.
- Log strings are English (`LOG_E/W/I/D` via syslog); repo docs are Chinese.
