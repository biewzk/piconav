#!/bin/bash
# 一键编译 LVGL 里程表界面程序
# 产物: build/bin/demo (ARM 交叉编译, 运行于开发板)
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

CLEAN=0
case "${1:-}" in
    clean) CLEAN=1 ;;
    -h|--help)
        echo "用法: ./build.sh [clean]"
        echo "  clean: 先清理再编译"
        exit 0
        ;;
esac

if [ "$CLEAN" -eq 1 ]; then
    echo ">>> make clean"
    make clean
fi

echo ">>> make"
make

BIN="$DIR/build/bin/demo"
if [ -f "$BIN" ]; then
    echo ""
    echo "编译完成: $BIN"
    file "$BIN"
else
    echo "ERROR: 编译失败，未生成 $BIN" >&2
    exit 1
fi