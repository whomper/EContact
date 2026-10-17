#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc cancels (the guidelines, 3.5), in Hatari on an ST with ECONTACT.PRG:

    tests/hatari/esc_test.py WORK etos512us.img

- a new contact (^N), a name typed, then Esc: no contact added, and the
  name typed doesn't stay in the field (the dialog closes);
- the same with Undo;
- Settings (^,), then Esc: ECONTACT.INF as it was;
- Delete on a contact, then Esc at "Are you sure?", and again with Undo:
  the alert closes as Cancel (common/ui/alert.c), and nothing is deleted;
- a search typed, then Esc: the query and the list stay as they were (Esc
  never clears text).
Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def same(a, b):
    """the share of pixels two screenshots have alike"""
    from PIL import Image
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    pa, pb = ia.load(), ib.load()
    w, h = ia.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if pa[x, y] == pb[x, y])
    return n / ((w // 2) * (h // 2))


def main():
    work, tos = os.path.abspath(sys.argv[1]), sys.argv[2]
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
    r = Rig(work, tos, machine="megast", extra=["--memsize", "4", "--monitor", "mono"],
            auto="C:\\ECONTACT\\ECONTACT.PRG")
    try:
        time.sleep(25)
        r.shot("0-start")
        inf_start = open(inf_path, "rb").read()
        for key, name in (("ESC", "esc"), ("UNDO", "undo")):
            r.ctrl("n")                     # a new contact
            time.sleep(3)
            r.type("Zed")
            time.sleep(1)
            r.shot("1-new-%s" % name)
            r.key(K[key])
            time.sleep(3)
            r.shot("2-after-%s" % name)
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
        time.sleep(3)
        r.shot("8-search")
        r.key(K["ESC"])
        time.sleep(3)
        r.shot("9-search-esc")
        r.ctrl("q")
        time.sleep(8)
    finally:
        r.stop()
    shots = os.path.join(work, "shots")
    shot = lambda n: os.path.join(shots, n + ".png")  # noqa: E731
    check("Esc and Undo in the new contact: none added", count() == before)
    for k in ("esc", "undo"):
        check("%s closed the new contact's dialog: the window as before" % k.capitalize(),
              same(shot("2-after-" + k), shot("0-start")) > 0.995)
    check("Esc in Settings: ECONTACT.INF as it was", open(inf_path, "rb").read() == inf_start)
    check("the Delete alert came", same(shot("5-delete-asked"), shot("4-settings-esc")) < 0.99)
    check("Esc closed it (as Cancel): the window as before",
          same(shot("6-delete-esc"), shot("4-settings-esc")) > 0.995)
    check("Undo closed it too", same(shot("7-delete-undo"), shot("4-settings-esc")) > 0.995)
    check("Esc and Undo at \"Are you sure?\": nothing deleted", count() == before)
    check("the search typed", same(shot("8-search"), shot("7-delete-undo")) < 0.99)
    check("Esc in the search: the query and the list as they were",
          same(shot("9-search-esc"), shot("8-search")) > 0.995)
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
