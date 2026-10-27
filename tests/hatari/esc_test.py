#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc and Undo (the guidelines, 3.5, GEM's way since 27 Oct 2026), and
^U and ^W (3.2), in Hatari on an ST with ECONTACT.PRG:

    tests/hatari/esc_test.py WORK [MACHINE]

- a new contact (^N), a name typed, then Esc: the field empty again and
  the dialog still open; a name typed again, then Undo: the dialog closes
  as Cancel, and no contact is added;
- Settings (^,), whose first page has no text field, then Esc: it closes
  as Cancel, ECONTACT.INF as it was;
- Delete on a contact, then Esc at "Are you sure?", and again with Undo:
  the alert closes as Cancel (common/ui/alert.c), and nothing is deleted;
- a search typed, then Esc: the search field empty, the list as before;
- ^W, the next window: there is one, so nothing changes;
- ^U: the program's one window closes, and the program ends.
Screenshots in WORK/shots.
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


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def same(a, b, box=None):
    """the share of pixels two screenshots (or a box of them) have alike"""
    from PIL import Image
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if box:
        ia, ib = ia.crop(box), ib.crop(box)
    pa, pb = ia.load(), ib.load()
    w, h = ia.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if pa[x, y] == pb[x, y])
    return n / ((w // 2) * (h // 2))


def main():
    work, machine = os.path.abspath(sys.argv[1]), machine_arg(sys.argv, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "ECONTACT.INF")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), folder)

    def count():
        out = subprocess.run([CLI, folder, "list"], check=True, capture_output=True, text=True).stdout
        return len(out.splitlines())

    before = count()
    r = Rig(work, machine, auto="C:\\ECONTACT\\ECONTACT.PRG")
    try:
        time.sleep(25)
        r.shot("0-start")
        inf_start = open(inf_path, "rb").read()
        r.ctrl("n")                         # a new contact
        time.sleep(3)
        r.shot("1-new")
        r.type("Zed")
        time.sleep(1)
        r.shot("1-new-typed")
        r.key(K["ESC"])                     # the field emptied, the dialog open
        time.sleep(2)
        r.shot("2-new-esc")
        r.type("Zed")
        time.sleep(1)
        r.key(K["UNDO"])                    # Cancel
        time.sleep(3)
        r.shot("2-after-undo")
        r.ctrl(",")                         # Settings
        time.sleep(3)
        r.shot("3-settings")
        r.key(K["ESC"])
        time.sleep(3)
        r.shot("4-settings-esc")
        r.key(K["DELETE"])                  # the contact chosen, deleted?
        time.sleep(3)
        r.shot("5-delete-asked")
        r.key(K["ESC"])
        time.sleep(3)
        r.shot("6-delete-esc")
        r.key(K["DELETE"])
        time.sleep(3)
        r.key(K["UNDO"])
        time.sleep(3)
        r.shot("7-delete-undo")
        r.type("ma")                        # a search
        time.sleep(8)                       # Maya Carter's photo, decoded the first time:
        r.shot("8-search")                  # while the bee shows, Esc is a stop (busy.c)
        r.key(K["ESC"])
        time.sleep(3)
        r.shot("9-search-esc")
        r.ctrl("w")                         # the next window: there is none
        time.sleep(2)
        r.shot("10-ctrl-w")
        r.ctrl("u")                         # closes: the program ends
        time.sleep(8)
        r.shot("11-ctrl-u")
    finally:
        r.stop()
    shots = os.path.join(work, "shots")
    shot = lambda n: os.path.join(shots, n + ".png")  # noqa: E731
    check("Esc then Undo in the new contact: none added", count() == before)
    check("the name typed in the new contact", same(shot("1-new-typed"), shot("1-new")) < 0.9995)
    check("Esc emptied the field, the dialog still open",
          same(shot("2-new-esc"), shot("1-new")) > 0.999 and same(shot("2-new-esc"), shot("0-start")) < 0.99)
    check("Undo closed the new contact's dialog: the window as before",
          same(shot("2-after-undo"), shot("0-start")) > 0.995)
    check("Esc in Settings: ECONTACT.INF as it was", open(inf_path, "rb").read() == inf_start)
    check("the Delete alert came", same(shot("5-delete-asked"), shot("4-settings-esc")) < 0.99)
    check("Esc closed it (as Cancel): the window as before",
          same(shot("6-delete-esc"), shot("4-settings-esc")) > 0.995)
    check("Undo closed it too", same(shot("7-delete-undo"), shot("4-settings-esc")) > 0.995)
    check("Esc and Undo at \"Are you sure?\": nothing deleted", count() == before)
    check("the search typed", same(shot("8-search"), shot("7-delete-undo")) < 0.99)
    check("Esc in the search: every contact listed again (the info line; the one found stays chosen)",
          same(shot("9-search-esc"), shot("7-delete-undo"), (0, 38, 330, 58)) > 0.999)
    check("and the field empty (it keeps the cursor)",
          same(shot("9-search-esc"), shot("8-search"), (140, 58, 330, 80)) < 0.99)
    check("^W with one window: nothing", same(shot("10-ctrl-w"), shot("9-search-esc")) > 0.999)
    check("^U: the window closed, the program ended", same(shot("11-ctrl-u"), shot("10-ctrl-w")) < 0.9)
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
