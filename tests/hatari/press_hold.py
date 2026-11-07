#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A press held still shows before it is let go (the guidelines 5.8), in
Hatari with ECONTACT.PRG and the sample contacts:

    tests/hatari/press_hold.py WORK [MACHINE]       (ST:mono by default)

A list row, a sidebar row and the card's Edit button are each pressed and
held still 1.2 s: the screenshot taken while held must show the press (the
row chosen, the button turned over), the card still the old one. TOS's AES
holds wind_update(BEG_UPDATE) back while the button is held still
(EHypView, 1 Nov 2026), so a press drawn under it showed only at the
release, and AESGAP never saw it: win.c draws presses between common's
press_update_begin and press_update_end. Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg, K  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0

# where the sample's rows and the Edit button are, in the screenshots'
# pixels (ST medium's 200 lines come doubled): Grace's row, the book's
# row in the sidebar, Edit
PLACES = {"ST:mono": (168, 142, (590, 369)), "ST:med": (189, 162, (585, 361)),
          "Falcon": (173, 147, (590, 467))}


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def main():
    from PIL import Image, ImageChops
    work, machine = os.path.abspath(sys.argv[1]), machine_arg(sys.argv, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    folder = os.path.join(work, "hd", "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "econtact.inf")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "econtact.prg"), folder)
    row_y, side_y, edit = PLACES.get(machine, PLACES["Falcon" if machine.startswith("Falcon") else "ST:mono"])
    r = Rig(work, machine, auto="C:\\ECONTACT\\econtact.prg")
    w, h = 640, 480 if machine.startswith("Falcon") else 400

    def held(name, x, y, box):
        """pressed and held still: the shot while held against the one before"""
        r.move(x, y)
        time.sleep(1.5)
        a = r.shot(name + "-0")
        r.xdo("mousedown", "1")
        time.sleep(1.2)
        b = r.shot(name + "-held")
        r.xdo("mouseup", "1")
        time.sleep(3)
        ia, ib = Image.open(a).convert("RGB").crop(box), Image.open(b).convert("RGB").crop(box)
        check("%s: drawn while held still, before the release" % name, ImageChops.difference(ia, ib).getbbox() is not None)
        return a, b
    try:
        time.sleep(30)
        a, b = held("a list row", 200, row_y, (136, 60, 330, h - 20))
        card = (340, 80, 600, 150)
        check("a list row: the card still the old one while held",
              ImageChops.difference(Image.open(a).convert("RGB").crop(card),
                                    Image.open(b).convert("RGB").crop(card)).getbbox() is None)
        held("a sidebar row", 60, side_y, (16, 80, 136, h - 20))
        held("the card's Edit button", edit[0], edit[1], (540, edit[1] - 12, 620, edit[1] + 8))
        r.key(K["UNDO"])
        time.sleep(3)
    finally:
        r.stop()
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
