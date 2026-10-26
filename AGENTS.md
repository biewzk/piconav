# AGENTS.md

C firmware project: LVGL v8.1.0 GPS odometer/nav GUI (240x320 RGB565) for an ARM board, with an SDL2 PC simulator built from shared sources. Detailed architecture docs live in `README.md` and `Doc/` (Chinese) — consult those for page/map-renderer internals.

## Builds (two independent Makefiles)

- **Device (ARM)**: `./build.sh` at repo root → `build/bin/demo`. Cross-compiles with buildroot toolchain `~/code/odometer/buildroot/buildroot-2023.02.6/output/host/bin/` (override with `make CROSS_COMPILE=...`). Binary runs on the board's `/dev/fb0`; cannot run on host.
- **Simulator (host)**: must run from `sim/`: `./build_sim.sh` or `./build_sim.sh run [file.nmea]` → `build_sim/bin/pico_nav_sim`. Requires SDL2 (`libsdl2-dev`).
- **Trap**: `README.md` says `./build.sh sim` builds the simulator — it doesn't. `build.sh` ignores the argument and silently rebuilds the ARM target.
- Both Makefiles wildcard-glob sources (`App/*.c`, `App/Pages/*.c`, `App/Utils/*.c`, plus per-target `HAL/` or `sim/`). New `.c` files need no Makefile edit **except** shared HAL sources: `sim/Makefile` lists `../HAL/nmea.c` explicitly (the device build gets it via the `HAL/*.c` glob). The Mapsforge engine and map UI are **not** part of this repo anymore — they moved to `../plugin_map` (and the GPS receiver page to `../plugin_gps`) as installable plugins.

## Verification (no test suite, no lint config)

Smoke-test via headless simulator from `sim/`:

```bash
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1500 \
  SIM_DUMP_PPM=/tmp/sim.ppm ./build_sim/bin/pico_nav_sim sample.nmea
```

- Check rendering by inspecting the dumped PPM. The dump forces a full-screen redraw first (`lv_obj_invalidate(lv_scr_act)` + `lv_refr_now`); without it the buffer only holds the last refreshed area at offset 0 and the image looks stale.
- Scripted key input: `SIM_KEY="right,enter,enter:800"` + `SIM_KEY_MS=400`. Token `"name:hold_ms"` holds the key (long-press gesture); plain `"name"` is a 60 ms click. Events are pushed from an lv_timer during `app_run` (the long-press detector needs `lv_timer_handler` running).
- Map/view selection: `MF_MAP=../resources/macau.map` (single file, wins over everything), `MF_MAP_DIR=../resources` (directory scanned by the map picker page), optional `SIM_LAT/SIM_LON/SIM_ZOOM`. These only matter with the map plugin loaded (see `../plugin_map`); without plugins the core has no map code at all.
- Debug flags: `MF_DEBUG=1` (frame stats), `MF_NOCOAST=1` (skip coastline flood fill), `LOG_CONSOLE=0`.
- Sim logs mirror to stderr and host syslog (`/var/log/syslog`).
- Device maps live in `/data/maps` (data partition, UBIFS via `S41data`). Map selection/keys/HUD are map-plugin territory (`../plugin_map/AGENTS.md`); the core only keeps the config key namespace (`map.selected`) and the shared services. In sim `/dev/config` is absent — resolution falls through to the scan/env. Drawer text/icons use `lv_font_app_16` (generated 16 px bitmap font: ASCII + the exact CJK glyphs the UI uses — montserrat renders Chinese as blanks and `lv_font_simsun_16_cjk` misses most of them, e.g. 行车→行).

## Architecture in one breath

Platform entry (`main.c` / `sim/main_sim.c`) → `App/app.c` poll loop (`app_run()`: read keys→publish `MSG_KEY`, GPS→`MSG_GPS`, `lv_timer_handler()`, 5ms delay). Framework in `App/Utils/`: `page_manager` (page registry `page_register()`/`page_find()` + page stack + lifecycle callbacks + the default long-ENTER back gesture, opt out with `PAGE_FLAG_NO_AUTO_BACK`) + `msg_center` (pub/sub with runtime topics via `msg_topic_register()`). **Page host = X-TRACK PageManager architecture**: one master LVGL screen hosts every page as a full-screen container; child order is the z-order. Transitions are 500 ms ease-out: push = the new page rises from the bottom edge over the old one (the old page is hidden after the anim); pop = the old page slides down off-screen and is torn down after the anim (`s_switch_busy` rejects overlapping switches). With one LVGL screen per page this would be impossible - the active screen always draws on top. Navigation is by name: `page_manager_open(name, param)`; built-ins register centrally in `App/Pages/pages_builtin.c`. Root page is `page_home` with a 2-panel carousel (`lv_anim_t` slide, watch-face entrance timeline): 0 watch face → 1 app drawer. **The core is a system UI**: only the trip dialplate (`page_dialplate`) and settings (`page_settings`) are built in; the GPS receiver and the live map (incl. the whole Mapsforge stack) live in the plugin repos `../plugin_gps` / `../plugin_map` and register their pages + drawer entries from `plugin_init()`. `app_registry` is a runtime list (`app_registry_add()`); the drawer (embedded in panel 1) is an **X-TRACK SystemInfos-style vertical rolling list**: flex column with `pad = (screen - item) / 2`, one item per app = a full-width black icon box (40x40 tile from `App/Utils/app_icons.c` / `tools/gen_app_icons.py` + CJK name) laid OVER grey info / white data columns; focusing animates the icon box width 220→70 (`lv_anim_path_overshoot`, 200 ms), revealing the columns plus an orange right-edge accent, and the root scrolls (`lv_obj_scroll_to_y(root, lv_obj_get_y(item))` — child y is content-relative, so the item lands centered). **Preview columns are registry-driven (ABI 3)**: each `app_entry_t` carries `info` (static grey lines) + `data` (static white column) or a `data_get(buf, len)` callback that the drawer polls once a second while visible (`app_drawer_set_visible()` gate) — the trip entry's counters, the GPS plugin's Lat/Lon/Sats and the map plugin's selected-map name all live with their entries; the drawer itself is semantics-free. GPS data production (`gps_service` + HAL + NMEA parser) **stays in the core** — the watch face and status bar consume the shared pool; the GPS page is only UI on top.

Map page keys / HUD internals moved with the map plugin (see `../plugin_map/AGENTS.md`); the freshness policy consumers use is the shared `gps_service_is_fresh()` helper (core service, NULL-safe, exported to plugins).

## GUI plugins (installable apps)

`App/Utils/plugin_manager.c` dlopens `/data/apps/*.so` (override `PICO_NAV_APPS`) after the built-ins register and loads plugins that export the contract in `App/Utils/plugin_api.h` (`plugin_abi_version`/`plugin_name`/`plugin_version`/`plugin_init()`; ABI mismatch is refused, load failures only log — **ABI 3**: `app_entry_t` gained the `info`/`data`/`data_get` drawer preview columns; ABI 2 plugins are skipped until rebuilt against the new headers). Plugins use the same APIs as built-in pages and can set `app_entry_t.img` to a host icon (`app_icon_trip/gps/map/settings/stopwatch_img`, see `App/Utils/app_icons.h`); without one the drawer falls back to the letter glyph. The host binary exports a curated symbol set via `app.exports` (version script + `--export-dynamic`; **an executable needs BOTH** — version script alone exports nothing; the map stack exports `mf_*`/`map_view_*` were removed when the map moved into its plugin). Plugins stay resident for the app lifetime. Full guide: `PLUGIN_SDK.md`; reference plugins live in their own repos: `../plugin_gps` (GPS receiver page), `../plugin_map` (Mapsforge live map + picker) and `plugins/stopwatch/` in-tree (same source builds ARM `make` and host `make sim`).

## Conventions & gotchas

- Button enums MUST be prefixed `KEY_BTN_*` (`HAL/hal.h`), never `KEY_*` — collides with `linux/input-event-codes.h` macros and silently breaks navigation on device.
- Pages subscribe messages in `on_show` and unsubscribe in `on_hide`; pass page state via `page_t.user`, not globals.
- HAL has two implementations behind `hal.h`: device (`HAL/hal_keys_evdev.c` + `HAL/hal_gps_uart.c`) vs sim (`sim/hal_keys_sdl.c` + `sim/hal_gps_nmea.c`). The NMEA-0183 parser (`HAL/nmea.c`, GGA/RMC) is shared by both.
- GPS: device opens `/dev/ttyS3` (UART3_M1, header pins 19/20, 9600 8N1, `GPS_DEV`/`GPS_BAUD` overrides) and retries every 5 s if the node is missing; sim replays `SIM_NMEA` (default `sample.nmea`) with `SIM_GPS_MS` per sentence (default 100). `app_run()` publishes snapshots on `MSG_GPS`; `app_gps()` returns the last one. Consumers: `page_dialplate` (speed; trip distance/moving time come from the `trip_store` service, which integrates MSG_GPS app-wide), the status bar and — via plugins — the GPS receiver page and the map HUD. Sample rate is demand-driven: pages call `gps_service_demand_add()/demand_release()` (refcounted) from their on_show/on_hide — there is no panel-level demand shim anymore.
- `LV_SPRINTF_USE_FLOAT` is 0: `lv_label_set_text_fmt()` does **not** support `%f` (prints junk). Format floats with libc `snprintf()` first, then `lv_label_set_text()`.
- Never cast 3-arg style setters (`lv_obj_set_style_text_opa`, ...) to `lv_anim_exec_xcb_t`: `lv_anim` invokes exec callbacks with 2 args, so the setter's `selector` reads a garbage register — it silently worked for months, then crashed the moment the code layout shifted. Wrap them in a real 2-arg callback (see `text_opa_anim_cb` in `page_home.c`).
- `app_init()` order: msg_center → page_manager → HAL → gps_service/trip_store/config_watcher → `pages_builtin_register()` → `plugin_manager_init()` → `page_manager_open("home", NULL)`. There is no startup splash page (`page_startup` was dead code, removed).
- `resources/macau.map` is tracked in git; other `.map` files (e.g. `monaco.map`, `ningxia.map`) can be added by the user.
- Log strings are English (`LOG_E/W/I/D` via syslog); repo docs are Chinese.
