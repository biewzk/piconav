#!/usr/bin/env python3
"""
Generate the CORE built-in app icons (LVGL 8.1, RGB565 + per-pixel
alpha byte, LV_COLOR_16_SWAP 0).

Output: App/Utils/app_icons.c (const lv_img_dsc_t, 40x40 each)
  - app_icon_trip_img       speedometer gauge     (green tile)
  - app_icon_settings_img   gear                  (slate tile)

Only apps that live IN the firmware keep compiled tiles here. Plugins
own their icons: each plugin repo generates a 40x40 RGB565+A8 resource
(assets/icon.rgb565a8) declared in its manifest.txt, and the host
renders it (see plugin_manager.c / PLUGIN_SDK.md). The tile glyph
rasterizer below is the reference for those per-plugin tools.

    python3 tools/gen_app_icons.py
"""

import math

SIZE = 40
SS = 5              # supersampling grid per axis
R_TILE = 9          # tile corner radius


def circle(cx, cy, r, seg=24):
    return [(cx + r * math.cos(2 * math.pi * i / seg),
             cy + r * math.sin(2 * math.pi * i / seg)) for i in range(seg)]


def rect_rot(cx, cy, hw, hh, deg=0.0):
    """Axis-aligned or rotated rectangle from center + half sizes."""
    a = math.radians(deg)
    ca, sa = math.cos(a), math.sin(a)
    pts = [(-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)]
    return [(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in pts]


def rounded_tile(w=SIZE, h=SIZE, r=R_TILE):
    """Rounded square CENTERED on (0,0) (y-down coords), traced
    clockwise: top edge, then TR/BR/BL/TL corner arcs."""
    hw, hh = w / 2.0, h / 2.0
    pts = [(-hw + r, -hh)]
    n = 6

    def arc(cx, cy, a0, a1):
        return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
                 cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
                for i in range(n + 1)]
    pts += arc(hw - r, -hh + r, -90, 0)
    pts += arc(hw - r, hh - r, 0, 90)
    pts += arc(-hw + r, hh - r, 90, 180)
    pts += arc(-hw + r, -hh + r, 180, 270)
    return pts


def band(r_out, r_in, a0, a1, seg=28, cx=0.0, cy=0.0):
    """Arc band (donut segment) as one polygon, angles in degrees,
    y-down: -90 = up, 0 = right, 90 = down."""
    out = [(cx + r_out * math.cos(math.radians(a)),
            cy + r_out * math.sin(math.radians(a)))
           for a in [a0 + (a1 - a0) * i / seg for i in range(seg + 1)]]
    inn = [(cx + r_in * math.cos(math.radians(a)),
            cy + r_in * math.sin(math.radians(a)))
           for a in [a0 + (a1 - a0) * i / seg for i in range(seg + 1)]]
    return out + inn[::-1]


def parity(shape, px, py):
    """Even-odd coverage of a shape = list of polygons (holes allowed)."""
    hit = 0
    for poly in shape:
        n = len(poly)
        for i in range(n):
            x1, y1 = poly[i]
            x2, y2 = poly[(i + 1) % n]
            if (y1 > py) != (y2 > py):
                t = (py - y1) / (y2 - y1)
                if px < x1 + t * (x2 - x1):
                    hit ^= 1
    return hit


def shape_cov(shape, px, py):
    """Anti-aliased coverage of a shape = SUM of its polygons (each one
    simple), so overlapping polygons union instead of XOR-ing holes."""
    tot = 0.0
    for poly in shape:
        c = 0
        for sy in range(SS):
            for sx in range(SS):
                if parity([poly], px + (sx + 0.5) / SS,
                          py + (sy + 0.5) / SS):
                    c += 1
        tot += c / (SS * SS)
    return min(tot, 1.0)


def render(tile_rgb, add, cut=(), readd=()):
    """Tile in tile_rgb; glyph = clamp(add - cut + readd) in white."""
    img = []
    for j in range(SIZE):
        for i in range(SIZE):
            px, py = i + 0.5 - SIZE / 2.0, j + 0.5 - SIZE / 2.0
            c_tile = shape_cov([TILE], px, py)
            if c_tile <= 0.0:
                img.append((0, 0, 0, 0))
                continue
            g = shape_cov(add, px, py)
            if cut:
                g -= shape_cov(cut, px, py)
            if readd:
                g += shape_cov(readd, px, py)
            g = max(0.0, min(1.0, g))
            r = tile_rgb[0] * (1 - g) + 0xFF * g
            gg = tile_rgb[1] * (1 - g) + 0xFF * g
            b = tile_rgb[2] * (1 - g) + 0xFF * g
            img.append((round(r), round(gg), round(b), round(255 * c_tile)))
    return img


TILE = None  # set in main() before render()


def rgb565(rgb):
    r, g, b = rgb
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return v & 0xFF, v >> 8


def emit(f, name, img):
    data_name = name + "_data"
    f.write("static const uint8_t %s[] = {\n" % data_name)
    for j in range(SIZE):
        row = []
        for i in range(SIZE):
            r, g, b, a = img[j * SIZE + i]
            lo, hi = rgb565((r, g, b))
            row += [lo, hi, a]
            assert max(row) <= 0xFF, "byte overflow at %d,%d" % (i, j)
        f.write("    " + " ".join("0x%02X," % v for v in row) + "\n")
    f.write("};\n\n")
    f.write("const lv_img_dsc_t %s = {\n" % name)
    f.write("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n")
    f.write("    .header.always_zero = 0,\n")
    f.write("    .header.reserved = 0,\n")
    f.write("    .header.w = %d,\n" % SIZE)
    f.write("    .header.h = %d,\n" % SIZE)
    f.write("    .data_size = %d,\n" % (SIZE * SIZE * 3))
    f.write("    .data = %s,\n" % data_name)
    f.write("};\n\n")


def preview(name, img):
    print("--- %s ---" % name)
    ramp = " .:-=+*#%@"
    for j in range(0, SIZE, 2):
        line = ""
        for i in range(SIZE):
            r, g, b, a = img[j * SIZE + i]
            # whiteness: glyph = white (255), colored tile has a low min
            w = min(r, g, b) if a else 0
            line += ramp[min(w * len(ramp) // 256, len(ramp) - 1)]
        print(line)


# ---------- glyph definitions (centered, y-down: -y is up) ----------

def glyph_trip():
    """Speedometer: open-bottom arc, needle to the upper right, hub."""
    ux, uy = math.cos(math.radians(-50)), math.sin(math.radians(-50))
    vx, vy = -uy, ux                      # perpendicular to the needle
    tip = (8 * ux, 1.5 + 8 * uy)
    add = [band(12.5, 8.5, 135, 405),
           [(0 - vx * 1.7, 1.5 - vy * 1.7), (0 + vx * 1.7, 1.5 + vy * 1.7),
            (tip[0] + vx * 0.9, tip[1] + vy * 0.9),
            (tip[0] - vx * 0.9, tip[1] - vy * 0.9)],
           circle(0, 1.5, 2.6)]
    return add, (), ()


def glyph_settings():
    """Gear: ring + 8 teeth, punched center hole."""
    add = [band(10.5, 6.6, 0, 360)]
    for k in range(8):
        a = math.radians(k * 45)
        add.append(rect_rot(11.2 * math.cos(a), 11.2 * math.sin(a),
                            2.1, 1.8, k * 45))
    cut = [circle(0, 0, 2.8, 20)]
    return add, cut, ()


ICONS = [
    ("app_icon_trip_img", (0x00, 0xA8, 0x60), glyph_trip, ()),
    ("app_icon_settings_img", (0x5E, 0x71, 0x88), glyph_settings, ()),
]


def main():
    global TILE
    TILE = rounded_tile()

    out = "App/Utils/app_icons.c"
    with open(out, "w") as f:
        f.write("/*\n")
        f.write(" * app_icons.c - generated by tools/gen_app_icons.py.\n")
        f.write(" * Do not edit by hand: tweak the script and re-run it.\n")
        f.write(" *\n")
        f.write(" * 40x40 RGB565 + alpha byte per pixel (LV_COLOR_16_SWAP 0),\n")
        f.write(" * rounded accent-color tile + white glyph for the built-in\n")
        f.write(" * apps. Plugins own their icon resources (manifest.txt).\n")
        f.write(" */\n\n")
        f.write('#include "app_icons.h"\n\n')
        for name, col, glyph, _ in ICONS:
            add, cut, readd = glyph()
            emit(f, name, render(col, add, cut, readd))
    print("wrote %s" % out)
    for name, col, glyph, _ in ICONS:
        add, cut, readd = glyph()
        preview(name, render(col, add, cut, readd))


if __name__ == "__main__":
    main()
