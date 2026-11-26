#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A click while a photo is made, in Hatari on an 8 MHz ST with ECONTACT.PRG
and the sample contacts (tests/hatari/sample.vcf, made-up people): a photo is
decoded the first time its card is shown, some seconds of an ST's. A click on
another contact meanwhile stops it and is taken at once (ui/card.c
photo_idle; owed since 22 Nov 2026: the click waited for the photo).

- Grace Baker chosen (her photo not made yet), then Luc Dubois clicked
  0.8 s later: 1.5 s after the click his card's name and values are
  shown, as once everything has settled (his own photo comes after);
- Grace Baker chosen again: her photo made after all (tried again).

    tests/hatari/photo_click.py WORK [MACHINE] [--bin DIR]
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402
from esc_test import same  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
BAKER = (200, 163)          # Grace Baker's row and Luc Dubois's, as the window opens on an ST mono screen
DUBOIS = (200, 235)         # (by last name, under the letters' headings)
CARD = (336, 56, 640, 380)  # the card's pane
TEXT = (416, 96, 640, 380)  # its name and values, right of the round picture (his own photo comes later)
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    fails += not ok


def dark(path, box=(340, 90, 414, 166)):
    """the share of dark pixels in the round picture: a photo dithered to black
    and white is some a fifth to a quarter, the initials' black disc about half"""
    from PIL import Image
    px = Image.open(path).convert("L").crop(box).load()
    w, h = box[2] - box[0], box[3] - box[1]
    return sum(1 for y in range(h) for x in range(w) if px[x, y] < 128) / float(w * h)


def main():
    args = sys.argv[:]
    bin_dir = os.path.join(ROOT, "binary")
    if "--bin" in args:
        i = args.index("--bin")
        bin_dir = args[i + 1]
        del args[i:i + 2]
    work, machine = os.path.abspath(args[1]), machine_arg(args, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "econtact.inf")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(bin_dir, "econtact.prg"), folder)
    r = Rig(work, machine, auto="C:\\ECONTACT\\econtact.prg")
    try:
        time.sleep(40)                      # the window, the books, the first contact's photo
        r.shot("0-start")
        r.click(*BAKER)
        time.sleep(0.8)                     # her photo being decoded
        r.click(*DUBOIS)
        time.sleep(1.5)
        r.shot("1-soon")
        time.sleep(15)
        r.shot("2-settled")
        r.click(*BAKER)
        time.sleep(20)                      # her photo made this time
        r.shot("3-baker")
        r.click(*DUBOIS)
        time.sleep(6)
        r.shot("4-dubois-again")
        r.ctrl("q")
        time.sleep(6)
    finally:
        r.stop()
    shot = lambda n: os.path.join(work, "shots", n + ".png")  # noqa: E731
    # his card as the rig leaves it (chosen again, everything settled) is the reference
    soon = same(shot("1-soon"), shot("4-dubois-again"), TEXT)
    check("1.5 s after the click Luc Dubois's name and values are shown (%.3f alike his card)" % soon, soon > 0.995)
    settled = same(shot("2-settled"), shot("4-dubois-again"), TEXT)
    check("the click wasn't lost: his card after it settled (%.3f alike)" % settled, settled > 0.995)
    baker = same(shot("3-baker"), shot("4-dubois-again"), TEXT)
    check("Grace Baker's card when chosen again (%.3f alike his)" % baker, baker < 0.98)
    pic = dark(shot("3-baker"))
    check("her photo made then, tried again after the stop (%.2f of the round picture dark: the initials' "
          "disc is about half)" % pic, pic < 0.35)
    print("photo_click: all passed" if not fails else "photo_click: %d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
