#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A photo's own colours in 256 colours (the guidelines 5.2, decided 22 Nov
2026): ECONTACT.PRG with the sample contacts (tests/hatari/sample.vcf,
made-up people) on the reference configuration (Falcon-4:256 with NVDI,
erz_env(); EmuTOS without it where this computer has no NVDI), and
build/winacc.acc, a stand-in for another program's accessory with a window.

1. a contact with a photo chosen, the window on top: the photo in its own
   colours (pens 192-254, PIC.OVL 1.4's quant);
2. WINACC.GO in C:, the stand-in's window on top: the pens back as they
   were within two seconds (TOS 4's AES sends no WM_UNTOPPED: a look a
   second), the photo drawn again in the screen's pens, nothing else on
   the screen changed;
3. WINACC.GO taken away, EContact's window on top again: the photo in its
   own colours once more, the screen as in 1.

    tests/hatari/photo_pens.py WORK [MACHINE]
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machines  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "binary")
CLI = os.path.join(ROOT, "build", "card-cli")
# left out of the comparison: the stand-in's window with its frame and shadow (x, y, w, h), and the
# title bars (y under 40: EContact's is drawn as not on top meanwhile)
SKIP = ((0, 0, 260, 160), (0, 0, 640, 40))


def seed(hd):
    def cli(*a):
        subprocess.run([CLI, hd] + list(a), check=True, stdout=subprocess.DEVNULL)
    cli("local", "Contacts")
    cli("import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf"))


def pixels(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    return im.size, im.load()


def compare(a, b, skip=()):
    """the box around the pixels that differ (outside the boxes skip), and how many"""
    (w, h), pa = pixels(a)
    _, pb = pixels(b)
    box, n = None, 0
    for y in range(h):
        for x in range(w):
            if any(k[0] <= x < k[0] + k[2] and k[1] <= y < k[1] + k[3] for k in skip):
                continue
            if pa[x, y] != pb[x, y]:
                n += 1
                box = (x, y, x, y) if not box else (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    return box, n


def colours(path, box):
    _, p = pixels(path)
    return len({p[x, y] for y in range(box[1], box[3] + 1) for x in range(box[0], box[2] + 1)})


def main():
    work = sys.argv[1]
    machine = sys.argv[2] if len(sys.argv) > 2 else machines.ERZ_MACHINE
    os.environ.update(machines.erz_env())
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    seed(hd)
    for f in ("econtact.prg", "tls.ovl", "pic.ovl", "cacert.pem", "roots.dat"):
        shutil.copy(machines.ci_path(BIN, f), os.path.join(hd, f))
    shutil.copy(os.path.join(ROOT, "build", "winacc.acc"), os.path.join(hd, "winacc.acc"))
    go = os.path.join(hd, "WINACC.GO")
    fails = 0
    r = Rig(work, machine)
    try:
        time.sleep(16)
        r.click(200, 204)                   # Maya Carter, her photo
        r.wait(6)
        r.move(630, 470)                    # the pointer out of the way
        a = r.shot("1-own")
        open(go, "w").close()
        time.sleep(3.5)
        b = r.shot("2-given")
        os.remove(go)
        time.sleep(3.5)
        c = r.shot("3-again")
    finally:
        r.stop()
    box, n = compare(a, b, SKIP)
    print("  1 -> 2: %d pixels differ outside the stand-in's window and the title bars, in %s" % (n, box))
    if not box:
        print("  FAIL the photo wasn't drawn again with the pens given back (or never had its own)")
        fails += 1
    else:
        w, h = box[2] - box[0] + 1, box[3] - box[1] + 1
        ok = w <= 160 and h <= 128 and box[0] > 300
        print("  %s only the photo changed: %d x %d at %d,%d (the card's photo is at most 160 x 128)" %
              ("ok  " if ok else "FAIL", w, h, box[0], box[1]))
        fails += not ok
        ka, kb = colours(a, box), colours(b, box)
        print("  info the photo's colours: %d with its own pens, %d in the screen's" % (ka, kb))
    box3, n3 = compare(a, c)
    ok = n3 == 0
    print("  %s 3: the screen as in 1 again (%d pixels differ%s)" % ("ok  " if ok else "FAIL", n3,
                                                                    ", in %s" % (box3,) if box3 else ""))
    fails += not ok
    print("photo_pens: all passed" if not fails else "photo_pens: %d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
