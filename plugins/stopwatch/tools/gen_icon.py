#!/usr/bin/env python3
"""
Generate the drawer icon resource for stopwatch (LVGL 8.1, 40x40 RGB565
+ per-pixel alpha byte, LV_COLOR_16_SWAP 0).

The icon is an app resource in the fixed format the system UI parses:
`assets/icon.rgb565a8` (SIZE*SIZE*3 bytes, declared as `icon=` in
manifest.txt). The tile rasterizer is the shared app-icon tool from
pico_nav (pico_nav/tools/gen_app_icons.py, trimmed to this glyph).

    python3 tools/gen_icon.py
"""

import math
import os

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


def glyph_stopwatch():
    """Stopwatch: ring, top button, stem, 12-o'clock hand, hub."""
    add = [band(11, 8.1, 0, 360),
           rect_rot(0, -12.6, 2.6, 1.6),
           rect_rot(0, -9.9, 1.2, 1.2),
           rect_rot(0, -4.4, 0.9, 3.7),
           circle(0, 0, 1.8, 16)]
    return add, (), ()


def main():
    global TILE
    TILE = rounded_tile()

    add, cut, readd = glyph_stopwatch()
    img = render((0xC0, 0xA0, 0x00), add, cut, readd)

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "assets", "icon.rgb565a8")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        for (rr, gg, bb, aa) in img:
            lo, hi = rgb565((rr, gg, bb))
            f.write(bytes((lo, hi, aa)))
    print("wrote %s (%dx%d RGB565+A8, %d bytes)"
          % (out, SIZE, SIZE, SIZE * SIZE * 3))


if __name__ == "__main__":
    main()
