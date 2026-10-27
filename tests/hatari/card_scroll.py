#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The card in the window's own slider (the guidelines 5.1: the pane at
the window's right edge takes the AES's slider column), in Hatari with
ECONTACT.PRG and a card longer than the window:

    tests/hatari/card_scroll.py WORK [MACHINE]

- the window's down arrow scrolls the card, and the list beside it stays;
- the track under the slider scrolls it a page;
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
        f.write("NOTE:" + " ".join("This is line %d of a long note." % i for i in range(12)) + "\r\nEND:VCARD\r\n")
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
        for _ in range(3):
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
