#!/bin/bash
# One-click PC simulator build.
# Output: build_sim/bin/pico_nav_sim (host executable, requires SDL2)
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

case "${1:-}" in
    clean) echo ">>> make clean"; make clean ;;
    -h|--help)
        echo "Usage: ./build_sim.sh [clean]"
        echo "  Build: requires SDL2 (apt install libsdl2-dev)"
        echo "  Run:   ./build_sim.sh run [sample.nmea]"
        exit 0
        ;;
    run)
        BIN=./build_sim/bin/pico_nav_sim
        [ -f "$BIN" ] || make
        ARGS="${2:-sample.nmea}"
        echo ">>> $BIN $ARGS"
        exec "$BIN" "$ARGS"
        ;;
    "")
        echo ">>> make"
        make
        BIN=./build_sim/bin/pico_nav_sim
        [ -f "$BIN" ] && echo "Build done: $BIN" || { echo "Build failed" >&2; exit 1; }
        ;;
esac