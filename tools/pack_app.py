#!/usr/bin/env python3
"""
Pack a GUI app directory into a .app package (format v1).

Layout (all little-endian):

  off  size  field
  0    8     magic "PICOAPP1"
  8    4     u32 manifest_len
  12   4     u32 file_count
  16   4     u32 index_off            (absolute)
  20   ...   manifest text            (key=value lines, see below)
       ...   index @index_off: file_count x { u16 name_len, name, u32 off, u32 size }
       ...   file payloads (absolute offsets)

Manifest keys (fixed format, parsed by the host and by webctl):
  name      unique app id, [a-z0-9_-]      (required)
  label     drawer label
  version   free-form
  abi       plugin ABI, must match the firmware
  page      page name the .so registers
  exec      .so file name (default app.so)
  icon      40x40 RGB565+A8 raw file (4800 bytes)
  info      grey drawer column, "\\n" for line breaks
  data      white drawer column, "\\n" for line breaks

Usage:
  python3 tools/pack_app.py <src_dir> -o <out.app>

The source dir contains manifest.txt plus the .so, icon and resource
files. Every regular file in the dir is packed, so the installer can
recreate the directory 1:1.
"""
import argparse
import os
import re
import struct
import sys

MAGIC = b"PICOAPP1"
ICON_BYTES = 40 * 40 * 3
NAME_RE = re.compile(r"^[a-z0-9_-]{1,39}$")


def parse_manifest(path):
    man = {"exec": "app.so"}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            man[k.strip()] = v.strip()
    return man


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("srcdir")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    src = os.path.abspath(args.srcdir)
    mpath = os.path.join(src, "manifest.txt")
    if not os.path.isfile(mpath):
        sys.exit("missing %s" % mpath)
    man = parse_manifest(mpath)

    name = man.get("name", "")
    if not NAME_RE.match(name):
        sys.exit("manifest name must match %s" % NAME_RE.pattern)
    if "abi" not in man:
        sys.exit("manifest abi is required")

    exec_name = man.get("exec", "app.so")
    exec_path = os.path.join(src, exec_name)
    if not os.path.isfile(exec_path):
        sys.exit("missing exec %s" % exec_path)
    with open(exec_path, "rb") as f:
        eh = f.read(20)
    if eh[:4] != b"\x7fELF" or eh[4] != 1 or eh[5] != 1:
        sys.exit("%s is not a 32-bit little-endian ELF" % exec_name)
    if struct.unpack_from("<H", eh, 18)[0] != 40:   # EM_ARM
        sys.exit("%s is not an ARM ELF" % exec_name)

    icon = man.get("icon", "")
    if icon:
        ipath = os.path.join(src, icon)
        if not os.path.isfile(ipath) or os.path.getsize(ipath) != ICON_BYTES:
            sys.exit("icon %s must be %d bytes (40x40 RGB565+A8)"
                     % (icon, ICON_BYTES))

    files = []
    for fn in sorted(os.listdir(src)):
        p = os.path.join(src, fn)
        if os.path.isfile(p):
            files.append(fn)

    with open(mpath, "rb") as f:
        manifest = f.read()

    index_len = sum(2 + len(fn.encode()) + 8 for fn in files)
    payload_off = 20 + len(manifest) + index_len

    out = bytearray()
    out += MAGIC
    out += struct.pack("<III", len(manifest), len(files), 20 + len(manifest))
    out += manifest

    off = payload_off
    blobs = []
    for fn in files:
        data = open(os.path.join(src, fn), "rb").read()
        nb = fn.encode()
        out += struct.pack("<H", len(nb)) + nb
        out += struct.pack("<II", off, len(data))
        blobs.append(data)
        off += len(data)
    for data in blobs:
        out += data

    with open(args.out, "wb") as f:
        f.write(out)
    print("OK %s: %s %s, %d file(s), %d bytes"
          % (args.out, name, man.get("version", "?"), len(files), len(out)))


if __name__ == "__main__":
    main()
