#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The card in the window's own slider (the guidelines 5.1: the pane at
the window's right edge takes the AES's slider column), in Hatari with
ECONTACT.PRG and a card longer than the window:

    tests/hatari/card_scroll.py WORK [MACHINE]

- the window's down arrow scrolls the card, and the list beside it stays;
  one click one line (the guidelines 5.1, "One click, one step": TOS 4's
  AES sent each click twice, common's evnt_multi_ drops the echo);
- the track under the slider scrolls it a page, one, not two;
- the track over the slider and the up arrow bring it back as it was;
- a short card, chosen next, starts from its top and fits.
Screenshots in WORK/shots.
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def region(png, box):
    from PIL import Image
    return Image.open(png).convert("L").crop(box).tobytes()


def shift_of(a, b, box, least=20):
    """how far up b's picture in box is from a's: the shift where most of
    b's rows with something drawn are a's rows that far down; 0 when none
    match (it went further than the box is high)"""
    from PIL import Image
    ra = Image.open(a).convert("L").crop(box)
    rb = Image.open(b).convert("L").crop(box)
    w, h = ra.size
    rows_a = [ra.crop((0, y, w, y + 1)).tobytes() for y in range(h)]
    rows_b = [rb.crop((0, y, w, y + 1)).tobytes() for y in range(h)]
    best, best_n = 0, 0
    for d in range(1, h - 6):
        ys = [y for y in range(h - d) if len(set(rows_b[y])) > 1]
        if len(ys) < least:
            continue
        n = sum(rows_b[y] == rows_a[y + d] for y in ys)
        if n > best_n and n >= 0.95 * len(ys):
            best, best_n = d, n
    return best


def view_foot(png, x0, x1, h):
    """where the card's view ends: over the line above its + and Edit,
    the first row down from the middle all of one colour but the card's"""
    from PIL import Image
    im = Image.open(png).convert("L")
    ground = max(set(im.crop((x0, 100, x1, 101)).tobytes()), key=im.crop((x0, 100, x1, 101)).tobytes().count)
    for y in range(h // 2, h):
        row = im.crop((x0, y, x1, y + 1)).tobytes()
        if len(set(row)) == 1 and row[0] != ground:
            return y - 3	# (the view ends 2 pixels over it: card.c)
    return h - 35


def main():
    args = [a for a in sys.argv if not a.startswith("--")]
    work, machine = os.path.abspath(args[1]), machine_arg(args, 2, "Falcon:16")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    vcf = os.path.join(work, "long.vcf")
    with open(vcf, "w", newline="") as f:
        f.write("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Longcard;Lena;;;\r\nFN:Lena Longcard\r\nUID:LONG-1\r\n")
        for i in range(6):
            f.write("TEL;type=CELL:+1 555 01%02d\r\n" % i)
        for i in range(6):
            f.write("EMAIL;type=INTERNET:lena%d@example.com\r\n" % i)
        f.write("ADR;type=HOME:;;1 Long Street;Springfield;IL;62701;USA\r\n"
                "ADR;type=WORK:;;2 Work Road;Shelbyville;IL;62565;USA\r\n")
        # long enough that two pages don't stop at its end
        f.write("NOTE:" + " ".join("This is line %d of a long note." % i for i in range(40)) + "\r\nEND:VCARD\r\n")
        f.write("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Short;Sam;;;\r\nFN:Sam Short\r\nTEL:+1 555 0200\r\n"
                "UID:SHORT-1\r\nEND:VCARD\r\n")
    subprocess.run([CLI, hd, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, hd, "import", "Contacts", vcf], check=True, stdout=subprocess.DEVNULL)
    inf = os.path.join(hd, "ECONTACT.INF")
    t = open(inf, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    open(inf, "wb").write(t)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), hd)
    r = Rig(work, machine)
    try:
        time.sleep(20)
        from PIL import Image
        away = lambda: (r.move(w - 60, h // 2), time.sleep(1))     # noqa: E731
        s0 = r.shot("0-start")
        w, h = Image.open(s0).size
        away()
        s0 = r.shot("0-start")
        # the window's own column at the screen's right: its down arrow
        # over the sizer, its up arrow under the info line (y 66 on TOS
        # 1.04 in ST high and TOS 4.04 in VGA alike)
        down, up = (w - 8, h - 30), (w - 8, 66)
        card = (w // 2 + 20, 70, w - 30, h - 40)    # the card's values
        lst = (w // 4, 70, w // 2 - 40, h - 40)     # the list
        cardm = (w // 2 + 20, 95, w - 70, h - 40)   # for the shift: under the toolbar,
                                                    # left of the pointer (away)
        r.click(*down)                  # one click: one line (ch + 4)
        time.sleep(1.5)
        away()
        s1a = r.shot("1-down-once")
        d = shift_of(s0, s1a, cardm)
        print("   one click on the down arrow: %d pixels" % d)
        check("one click on the arrow, one line (not two)", d == 20)
        for _ in range(2):
            r.click(*down)
            time.sleep(1.5)
        away()
        s1 = r.shot("1-down")
        check("the window's down arrow scrolled the card", region(s0, card) != region(s1, card))
        check("and the list stayed", region(s0, lst) == region(s1, lst))
        r.click(w - 8, h - 60)          # the track under the slider
        time.sleep(2)
        away()
        s2 = r.shot("2-page")
        check("the track a page further", region(s1, card) != region(s2, card))
        # a page is the view less a line: the line at its foot comes to
        # its top, the only rows the two have alike (two pages: none)
        view = (w // 2 + 20, 86, w - 70, view_foot(s1, w // 2 + 20, w - 70, h))
        d = shift_of(s1, s2, view, 6)
        print("   one click on the track: %d pixels (the card shows %d)" % (d, view[3] - view[1]))
        check("one click on the track, one page (not two)", 20 < d < view[3] - view[1])
        for _ in range(3):              # the track over the slider: pages up
            r.click(up[0], up[1] + 30)
            time.sleep(1.5)
        for _ in range(4):
            r.click(*up)
            time.sleep(1)
        away()
        s3 = r.shot("3-back")
        check("the up arrow back to its top: as it began", region(s0, card) == region(s3, card))
        r.key(0x50)                     # down: Sam Short
        time.sleep(3)
        away()
        s4 = r.shot("4-short")
        check("another card shown", region(s3, card) != region(s4, card))
    finally:
        r.stop()
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
