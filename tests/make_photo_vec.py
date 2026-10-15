#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes tests/photo_vec.h: JPEGs for the photo tests in unit.c, made
with Pillow. Four coloured quarters (red, green / blue, yellow) so where
each part of the picture lands can be checked."""
import io
import os
import subprocess
import tempfile
from PIL import Image

def quarters_im(w, h):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            top, left = y < h // 2, x < w // 2
            px[x, y] = (230, 30, 30) if top and left else (30, 200, 40) if top else \
                       (30, 40, 220) if left else (240, 220, 20)
    return im


def quarters(w, h, **kw):
    im = quarters_im(w, h)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=85, **kw)
    return b.getvalue()

def magick(w, h, *opts):
    """ImageMagick's own libjpeg settings: what Pillow can't make"""
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "q.png"), os.path.join(d, "q.jpg")
        quarters_im(w, h).save(src)
        subprocess.run(["convert", src, *opts, "-quality", "85", dst], check=True)
        return open(dst, "rb").read()


vec = {
    "jpg_wide": quarters(400, 300),                    # decoded at 1/4, the middle square
    "jpg_square": quarters(512, 512),                  # 1/8, no IDCT
    "jpg_small": quarters(40, 40),                     # smaller than the picture: 1/1, enlarged
    # the kinds TJpgDec won't take, read from their blocks' averages (jpegdc.c)
    "jpg_prog": quarters(512, 512, progressive=True),
    "jpg_prog_small": quarters(200, 200, progressive=True),
    "jpg_prog_wide": quarters(640, 480, progressive=True, subsampling=0),
    "jpg_440": magick(512, 512, "-sampling-factor", "1x2"),
    "jpg_cmyk": magick(512, 512, "-colorspace", "CMYK"),
    "jpg_prog_gray": magick(256, 256, "-colorspace", "Gray", "-interlace", "JPEG"),
    "jpg_prog_restart": quarters(512, 512, progressive=True, restart_marker_blocks=3),
    "png_quarters": None,
    "jpg_gray": None,
}
b = io.BytesIO()
quarters_im(64, 64).save(b, "PNG")
vec["png_quarters"] = b.getvalue()
g = Image.new("L", (128, 128), 128)
b = io.BytesIO()
g.save(b, "JPEG", quality=90)
vec["jpg_gray"] = b.getvalue()
out = os.path.join(os.path.dirname(__file__), "photo_vec.h")
with open(out, "w") as f:
    f.write("/* SPDX-License-Identifier: GPL-2.0-or-later\n * Copyright (C) 2026 AERA | Erez Yaary */\n")
    f.write("/* made by tests/make_photo_vec.py: do not edit */\n")
    for k, v in vec.items():
        f.write("static const unsigned char %s[%d] = {\n" % (k, len(v)))
        for i in range(0, len(v), 16):
            f.write("\t" + ", ".join("0x%02x" % c for c in v[i:i + 16]) + ",\n")
        f.write("};\n")
print(out, {k: len(v) for k, v in vec.items()})
