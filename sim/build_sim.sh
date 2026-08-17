#!/bin/bash
# PC 模拟器一键编译
# 产物: build_sim/bin/pico_nav_sim (host 可执行, 依赖 SDL2)
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

case "${1:-}" in
    clean) echo ">>> make clean"; make clean ;;
    -h|--help)
        echo "用法: ./build_sim.sh [clean]"
        echo "  编译: 需要 SDL2 (apt install libsdl2-dev)"
        echo "  运行: ./build_sim.sh run [sample.nmea]"
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
        [ -f "$BIN" ] && echo "编译完成: $BIN" || { echo "编译失败" >&2; exit 1; }
        ;;
esac