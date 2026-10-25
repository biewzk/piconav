#!/usr/bin/env python3
"""
Regenerate App/Utils/lv_font_app_16.c - the UI bitmap font (16 px, bpp 4):
ASCII 0x20-0x7F + degree sign + bullet, plus exactly the CJK glyphs the
UI strings use (see SYMBOLS below; add a char here when the UI grows).

lv_font_conv echoes the symbol set into generated comments; those are
sanitized to U+XXXX so the C source stays ASCII (repo rule).

    python3 tools/gen_app_font.py        (needs node/npx on PATH)
"""

import subprocess

# UI charset: ying yong / xing che / di tu / she zhi / miao biao /
# xuan ze / da kai  (drawer title+apps, picker hint)
SYMBOLS = "\u5e94\u7528\u884c\u8f66\u5730\u56fe\u8bbe\u7f6e" \
          "\u79d2\u8868\u9009\u62e9\u6253\u5f00"

OUT = "App/Utils/lv_font_app_16.c"


def main():
    cmd = [
        "npx", "--yes", "lv_font_conv",
        "--font", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "--range", "0x20-0x7F,0xB0,0x2022",
        "--font",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "--symbols", SYMBOLS,
        "--size", "16", "--bpp", "4",
        "--format", "lvgl", "--lv-font-name", "lv_font_app_16",
        "--no-compress", "-o", OUT,
    ]
    subprocess.run(cmd, check=True)

    src = open(OUT, encoding="utf-8").read()
    src = "".join(ch if not ("\u4e00" <= ch <= "\u9fff")
                  else "U+%04X" % ord(ch) for ch in src)
    open(OUT, "w", encoding="utf-8").write(src)
    print("wrote %s" % OUT)


if __name__ == "__main__":
    main()
