#!/usr/bin/env python3
"""Writes tests/photo_vec.h: JPEGs for the photo tests in unit.c, made
with Pillow. Four coloured quarters (red, green / blue, yellow) so where
each part of the picture lands can be checked."""
import io
import os
from PIL import Image

def quarters(w, h, **kw):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            top, left = y < h // 2, x < w // 2
            px[x, y] = (230, 30, 30) if top and left else (30, 200, 40) if top else \
                       (30, 40, 220) if left else (240, 220, 20)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=85, **kw)
    return b.getvalue()

vec = {
    "jpg_wide": quarters(400, 300),                    # decoded at 1/4, the middle square
    "jpg_square": quarters(512, 512),                  # 1/8, no IDCT
    "jpg_small": quarters(40, 40),                     # smaller than the picture: 1/1, enlarged
    "jpg_prog": quarters(200, 200, progressive=True),  # TJpgDec can't: refused
    "jpg_gray": None,
}
g = Image.new("L", (128, 128), 128)
b = io.BytesIO()
g.save(b, "JPEG", quality=90)
vec["jpg_gray"] = b.getvalue()
out = os.path.join(os.path.dirname(__file__), "photo_vec.h")
with open(out, "w") as f:
    f.write("/* made by tests/make_photo_vec.py: do not edit */\n")
    for k, v in vec.items():
        f.write("static const unsigned char %s[%d] = {\n" % (k, len(v)))
        for i in range(0, len(v), 16):
            f.write("\t" + ", ".join("0x%02x" % c for c in v[i:i + 16]) + ",\n")
        f.write("};\n")
print(out, {k: len(v) for k, v in vec.items()})
