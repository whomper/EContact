#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""EContact's icon, drawn as 32x32 1-bit pixel art: the same SM124-style
monochrome monitor as EMail's, ECal's and Claude ST's icons, whose dark
screen shows an address card: a brown spine like the Mac's Contacts book,
a person's head and shoulders, and the lines of a card. Adapted from
ECal's make_icon.py. Writes:
  ui/icon.h        the bitmap (16-bit words, MSB first)
  ui/icon16.h      the 16-colour version and its palette
  docs/icon16.png  an enlarged preview of that
  docs/icon.png    an enlarged preview
"""
import io
import os

W = H = 32
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
px = [[0] * W for _ in range(H)]


def keep(name, data):
    """write ROOT/name only when it changes: a header written again with
    the same text would make `make` build (and number) the program anew"""
    path = os.path.join(ROOT, name)
    if isinstance(data, str):
        data = data.encode()
    try:
        if open(path, "rb").read() == data:
            return
    except FileNotFoundError:
        pass
    with open(path, "wb") as f:
        f.write(data)


def put(x, y, v=1):
    if 0 <= x < W and 0 <= y < H:
        px[y][x] = v


def rect(x0, y0, x1, y1, v=1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(x, y, v)


def hline(x0, x1, y, v=1):
    rect(x0, y, x1, y, v)


def vline(x, y0, y1, v=1):
    rect(x, y0, x, y1, v)


# --- monitor case: a rounded box with a thicker bottom bezel ---
hline(3, 28, 0)
hline(2, 29, 1)
vline(1, 2, 21)
vline(30, 2, 21)
put(2, 1); put(29, 1)
hline(2, 29, 22)
hline(3, 28, 23)
put(2, 21); put(29, 21)
# bezel shading on the right/bottom for a little depth
vline(29, 3, 21)
hline(3, 28, 21)

# --- screen: dark CRT with rounded corners ---
rect(4, 3, 27, 18)
for x, y in [(4, 3), (27, 3), (4, 18), (27, 18)]:
    put(x, y, 0)

# --- the address card, in "white": a brown spine on the left (the Mac's
# Contacts book), a person's head and shoulders, the name and lines ---
EX0, EX1, EY0, EY1 = 7, 22, 5, 16
rect(EX0, EY0, EX1, EY1, 0)
rect(EX0, EY0, EX0 + 1, EY1, 1)          # the spine
for y in (EY0 + 2, EY0 + 5, EY0 + 8):    # its tabs
    put(EX0 + 1, y, 0)
HEAD = [(12, 7), (13, 7), (11, 8), (12, 8), (13, 8), (14, 8), (11, 9), (12, 9), (13, 9), (14, 9),
        (12, 10), (13, 10)]
BODY = [(11, 12), (12, 12), (13, 12), (14, 12), (10, 13), (11, 13), (12, 13), (13, 13), (14, 13),
        (15, 13), (10, 14), (11, 14), (12, 14), (13, 14), (14, 14), (15, 14)]
for (x, y) in HEAD + BODY:
    put(x, y, 1)
LINES = [(17, 21, 8), (17, 20, 10), (17, 21, 12), (17, 19, 14)]
for (x0, x1, y) in LINES:
    hline(x0, x1, y, 1)

# --- a block cursor at the right, like a terminal waiting for input ---
rect(24, 15, 25, 16, 0)

# --- power LED on the bezel, like the SM124's ---
rect(25, 21, 26, 21, 0)
put(25, 21, 1)

# --- neck and tilting foot ---
rect(13, 24, 18, 25)
hline(8, 23, 26)
hline(7, 24, 27)
vline(6, 28, 29); vline(25, 28, 29)
hline(7, 24, 28, 0)
hline(6, 25, 30)
hline(7, 24, 29)
put(7, 28); put(24, 28)
hline(8, 23, 31, 0)

# ---------------------------------------------------------------------------
# 16-colour version: the same shapes, coloured. Values are VDI pens: 0 is
# the background (not drawn), 1 black, 8..15 set from PALETTE while shown.
# ---------------------------------------------------------------------------
PALETTE = {                     # pen: (r, g, b) in 0..255
    8: (226, 216, 190),         # case: Atari putty beige
    9: (168, 156, 132),         # case shading
    10: (64, 60, 56),           # outline
    11: (16, 22, 42),           # CRT screen
    12: (246, 240, 222),        # the page's paper
    13: (150, 96, 56),          # the book's brown spine
    14: (96, 232, 120),         # green phosphor prompt and power LED
    15: (88, 104, 140),         # the person, and the screen's glare
}
col = [[0] * W for _ in range(H)]


def cput(x, y, v):
    if 0 <= x < W and 0 <= y < H:
        col[y][x] = v


def crect(x0, y0, x1, y1, v):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            cput(x, y, v)


# case: fill the outline's interior with beige, outline in dark grey
crect(2, 1, 29, 22, 8)
crect(3, 0, 28, 0, 8)
crect(3, 23, 28, 23, 8)
for y in range(H):
    for x in range(W):
        if px[y][x] and y <= 23:
            cput(x, y, 10)
# bevel shading: the double line on the right and the bottom bezel
for y in range(3, 22):
    cput(28, y, 9); cput(29, y, 9)
for x in range(3, 29):
    cput(x, 20, 9); cput(x, 21, 9)
for y in range(1, 23):
    cput(30, y, 10)
for x in range(3, 29):
    cput(x, 22, 10)
cput(29, 22, 10); cput(2, 22, 10)
# screen
for y in range(3, 19):
    for x in range(4, 28):
        if px[y][x]:
            cput(x, y, 11)
# glare in the top-left corner of the CRT
for x, y in [(6, 4), (7, 4), (8, 4), (5, 5), (6, 5), (5, 6)]:
    cput(x, y, 15)
# the page and the cursor: the "white" pixels on the screen
for y in range(3, 19):
    for x in range(4, 28):
        if not px[y][x] and not (x in (4, 27) and y in (3, 18)):
            cput(x, y, 12 if x <= EX1 else 14)
# the spine brown, the person blue-grey, the lines dark grey
for y in range(EY0, EY1 + 1):
    for x in (EX0, EX0 + 1):
        if px[y][x]:
            cput(x, y, 13)
for (x, y) in HEAD + BODY:
    cput(x, y, 15)
for (x0, x1, y) in LINES:
    for x in range(x0, x1 + 1):
        cput(x, y, 10)
# power LED
cput(26, 21, 14)
# neck and foot
for y in range(24, 31):
    for x in range(W):
        if px[y][x]:
            cput(x, y, 10)
crect(14, 24, 17, 25, 8)
crect(8, 28, 23, 28, 8)
crect(9, 27, 22, 27, 9)

f = io.StringIO()
f.write("/* SPDX-License-Identifier: GPL-2.0-or-later\n"
        " * Copyright (C) 2026 Erez Yaary */\n")
f.write("/* EContact icon, 32x32 in 16 colours (VDI pens, 2 per byte), from\n"
        " * tools/icon/make_icon.py. 0 = background, not drawn. */\n")
f.write("static const unsigned char icon16_px[32 * 16] = {\n")
for y in range(H):
    row = [(col[y][x] << 4) | col[y][x + 1] for x in range(0, W, 2)]
    f.write("\t" + ", ".join("0x%02x" % b for b in row) + ",\n")
f.write("};\n/* pens 8..15 as VDI RGB (0..1000) */\n")
f.write("static const short icon16_rgb[8][3] = {\n")
for pen in range(8, 16):
    r, g, b = PALETTE[pen]
    f.write("\t{ %d, %d, %d },\n" % (r * 1000 // 255, g * 1000 // 255, b * 1000 // 255))
f.write("};\n")
keep(os.path.join("ui", "icon16.h"), f.getvalue())

words = []
for y in range(H):
    for half in range(2):
        v = 0
        for b in range(16):
            v = (v << 1) | px[y][half * 16 + b]
        words.append(v)

f = io.StringIO()
f.write("/* EContact icon, 32x32 1-bit, generated by tools/icon/make_icon.py */\n")
f.write("#define ICON_W 32\n#define ICON_H 32\n")
f.write("static const unsigned short icon_bits[ICON_H * 2] = {\n")
for y in range(H):
    f.write("\t0x%04x, 0x%04x,\n" % (words[2 * y], words[2 * y + 1]))
f.write("};\n")
keep(os.path.join("ui", "icon.h"), f.getvalue())


def keep_png(name, img):
    b = io.BytesIO()
    img.resize((W * 8, H * 8), Image.NEAREST).save(b, "PNG")
    keep(os.path.join("docs", name), b.getvalue())


try:
    from PIL import Image
    img = Image.new("RGB", (W, H), "white")
    for y in range(H):
        for x in range(W):
            if px[y][x]:
                img.putpixel((x, y), (0, 0, 0))
    keep_png("icon.png", img)
    rgb = {0: (255, 255, 255), 1: (0, 0, 0), **PALETTE}
    img = Image.new("RGB", (W, H), "white")
    for y in range(H):
        for x in range(W):
            img.putpixel((x, y), rgb[col[y][x]])
    keep_png("icon16.png", img)
except ImportError:
    pass

if __name__ == "__main__":
    for y in range(H):
        print("".join("#" if v else "." for v in px[y]))
