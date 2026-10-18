#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ECONTACT.PRG with ECONTACT.ACC installed, in Hatari under EmuTOS
(plain TOS; the AERA guidelines, 2.3 and 3.1):
  - the accessory opened and closed, then ECONTACT.PRG started from the
    desktop: it runs, with its menu bar;
  - EContact chosen in the Desk menu meanwhile: the accessory stands aside
    and the program's window stays the only one;
  - the Groups sidebar turned off in the program's View menu, then Quit;
  - the accessory opened again: it reads the files again (the program
    changed ECONTACT.INF), and shows no sidebar either.
Screenshots in WORK/shots; the checks read C:\\ECONTACT\\ECONTACT.LOG.

    tests/hatari/twin_test.py WORK [MACHINE]
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failed += 1


def same(a, b):
    """the share of pixels two screenshots have alike"""
    from PIL import Image
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    pa, pb = ia.load(), ib.load()
    w, h = ia.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if pa[x, y] == pb[x, y])
    return n / ((w // 2) * (h // 2))


def main():
    work, machine = sys.argv[1], machine_arg(sys.argv, 2, "Falcon:16")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(folder, "ECONTACT.INF"), "rb") as f:
        inf = f.read()
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    # in the root, FOLDER= naming the program's folder; a log, the sidebar
    # shown, and no refresh at opening
    inf = inf.replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = inf.replace(b"\nSIDEBAR=0", b"\nSIDEBAR=1")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    with open(os.path.join(hd, "ECONTACT.INF"), "wb") as f:
        f.write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.ACC"), os.path.join(hd, "ECONTACT.ACC"))
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), os.path.join(folder, "ECONTACT.PRG"))
    log = os.path.join(folder, "ECONTACT.LOG")
    shots = os.path.join(work, "shots")
    r = Rig(work, machine, auto="")
    try:
        time.sleep(16)
        r.click(18, 8)                  # the desktop's Desk menu: EContact
        time.sleep(1)
        r.click(60, 8 + 3 * 16 + 8)
        time.sleep(8)
        r.shot("1-accessory")
        r.ctrl("w")                     # closed, as one does
        time.sleep(3)
        r.click(195, 40, double=True)   # DISK C, the ECONTACT folder, ECONTACT.PRG
        time.sleep(3)
        r.click(60, 158, double=True)
        time.sleep(3)
        r.shot("2-folder")
        # CACHE, ECONTACT.KEY, ECONTACT.LOG / ECONTACT.PRG
        r.click(60, 200, double=True)
        time.sleep(15)
        r.shot("3-program")
        r.click(216, 8)                 # View
        time.sleep(1)
        r.shot("4-view-menu")
        r.key(0x01)                     # Esc: the menu closes
        time.sleep(1)
        r.click(56, 8)                  # the Desk menu: EContact's own
        time.sleep(1)
        r.shot("5-desk-menu")
        r.click(60, 8 + 3 * 16 + 8)     # its first accessory: ECONTACT.ACC
        time.sleep(5)
        r.shot("6-acc-chosen")
        r.click(216, 8)                 # View > Groups (off)
        time.sleep(1)
        r.click(240, 19 + 2 * 16 + 8)
        time.sleep(2)
        r.shot("7-no-sidebar")
        r.ctrl("q")                     # Quit
        time.sleep(8)
        r.shot("8-desktop")
        r.click(18, 8)                  # the desktop's Desk menu: EContact
        time.sleep(1)
        r.click(60, 8 + 3 * 16 + 8)
        time.sleep(8)
        r.shot("9-accessory-again")
    finally:
        r.stop()
    print("screenshots in", shots)
    s = lambda n: os.path.join(shots, n + ".png")
    if all(os.path.exists(s(n)) for n in ("3-program", "6-acc-chosen")):
        check(same(s("3-program"), s("6-acc-chosen")) > 0.97,
              "EContact chosen in the Desk menu while ECONTACT.PRG runs: no second window")
    text = open(log, encoding="latin-1").read() if os.path.exists(log) else ""
    check(text.count("reading them again") == 1,
          "the accessory read the files again after the program changed ECONTACT.INF")
    with open(os.path.join(hd, "ECONTACT.INF"), newline="") as f:
        got = f.read()
    check("SIDEBAR=0" in got, "the program wrote SIDEBAR=0")
    check(not os.path.exists(os.path.join(hd, "ECONTACT.IN$")), "no ECONTACT.IN$ left behind")
    print("%d failed" % failed)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
