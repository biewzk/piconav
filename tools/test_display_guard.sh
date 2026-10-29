#!/bin/sh
# Host unit test for App/Utils/display_guard.c (no /dev/fb0 needed).
# Builds the guard against a temp file and checks that it reports the
# process holding it, and 0 once that process exits.
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/disp_guard_test.$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/t.c" <<'SRC'
#include <stdio.h>
#include "display_guard.h"
int main(void) { printf("%d\n", display_guard_owner_pid()); return 0; }
SRC

gcc -O1 -Wall -I"$DIR/App/Utils" \
    -DDISPLAY_GUARD_FB="\"$TMP/fakefb\"" \
    -o "$TMP/t" "$TMP/t.c" "$DIR/App/Utils/display_guard.c"

touch "$TMP/fakefb"
[ "$("$TMP/t")" = "0" ] || { echo "FAIL: owner reported with nobody holding it"; exit 1; }

( exec 9<"$TMP/fakefb"; sleep 2 ) &
sleep 0.4
PID=$("$TMP/t")
[ "$PID" != "0" ] || { echo "FAIL: owner not detected"; exit 1; }
echo "detected owner pid $PID"
wait
[ "$("$TMP/t")" = "0" ] || { echo "FAIL: owner still reported after exit"; exit 1; }
echo "PASS"
