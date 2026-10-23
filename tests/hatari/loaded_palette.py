#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Colours chosen as the screen shows them, in Hatari on a Falcon in 256
colours with a palette loaded behind the VDI's back:

    tests/hatari/loaded_palette.py WORK [MACHINE] [--nvdi]

On Erez's Falcon COLORVDI.CPX loads a palette into the video hardware;
NVDI's table, which vq_color reads, keeps its own, and a grey by it
showed bright green (1.61). PALSET.PRG (tests/atari/palset.c, make
build/PALSET.PRG) plays it: it sets the registers 16-254 all bright
green and runs ECONTACT.PRG with 80 contacts. EContact reads the pens
from the hardware (vq_color_seen, draw.c pens_init), so:
- the greys in the log are greys;
- the chosen row's blue on the screen is blue, not green (pens_init
  looks for it in all 256 pens).
MACHINE is Falcon:256 unless given. Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402
from list_bar import cards  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def chosen_row(png):
    """the colour of the widest run of one colour, not white, black or a
    grey, in the window's middle third: the chosen row"""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    px = im.load()
    w, h = im.size
    best = (0, None)
    for y in range(h // 6, h // 2):
        x = w // 5
        while x < w * 3 // 5:
            c = px[x, y]
            n = 1
            while x + n < w * 3 // 5 and px[x + n, y] == c:
                n += 1
            if max(c) - min(c) > 60 and n > best[0]:
                best = (n, c)
            x += n
    return best


def main():
    if "--nvdi" in sys.argv:
        os.environ["AERA_WITH_NVDI"] = "1"
    args = [a for a in sys.argv if not a.startswith("--")]
    work, machine = os.path.abspath(args[1]), machine_arg(args, 2, "Falcon:256")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    vcf = os.path.join(work, "cards.vcf")
    cards(vcf, 80)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", vcf], check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "ECONTACT.INF")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = inf.replace(b"\nLOG=0", b"\nLOG=1")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), folder)
    shutil.copy(os.path.join(ROOT, "build", "PALSET.PRG"), os.path.join(hd, "PALSET.PRG"))
    r = Rig(work, machine, auto="C:\\PALSET.PRG")
    try:
        time.sleep(30)
        r.move(600, 250)
        time.sleep(1)
        shot = r.shot("0-start")
        log = os.path.join(folder, "ECONTACT.LOG")
        pens = [l.strip() for l in open(log, encoding="latin-1") if "pens:" in l] \
            if os.path.exists(log) else []
        print("  (%s)" % (pens[0] if pens else "no pens line in the log"))
        greys = re.findall(r"\((\d+) (\d+) (\d+)\)", pens[0]) if pens else []
        check("the log's two greys are greys (%s)" % (greys,),
              len(greys) == 2 and all(max(map(int, g)) - min(map(int, g)) <= 120 for g in greys))
        n, c = chosen_row(shot)
        check("the chosen row is blue on the screen, not green (%s, %d pixels wide)" % (c, n),
              c is not None and n > 100 and c[2] > c[1] and c[2] > c[0])
    finally:
        r.stop()
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
