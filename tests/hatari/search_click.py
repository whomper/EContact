#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The search field takes a click, in Hatari on an ST with ECONTACT.PRG:

    tests/hatari/search_click.py WORK [MACHINE]

- a click in the field gives it the focus: a black frame and the cursor;
- what is typed goes into it, and Delete there deletes no contact;
- a click on a contact in the list takes the focus away (the cursor goes,
  the query stays), and Delete then asks about the contact as before;
- a click on the clear button takes the text out and keeps the focus.
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
from esc_test import same  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
SEARCH = (250, 72)          # the field, in the window as it opens on an ST mono screen
CLEAR = (314, 72)           # its clear button, at the right
CONTACT = (200, 112)        # a contact in the list
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


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
        r.click(*SEARCH)
        time.sleep(2)
        r.shot("1-clicked")
        r.type("ma")
        time.sleep(8)                       # and the photo of the first found
        r.shot("2-typed")
        r.key(K["DELETE"])
        time.sleep(3)
        r.shot("3-delete-in-field")
        r.click(*CONTACT)
        time.sleep(3)
        r.shot("4-contact-clicked")
        r.key(K["DELETE"])
        time.sleep(3)
        r.shot("5-delete-asked")
        r.key(K["ESC"])
        time.sleep(3)
        r.shot("6-delete-esc")
        r.click(*CLEAR)
        time.sleep(3)
        r.shot("7-cleared")
        r.ctrl("q")
        time.sleep(8)
    finally:
        r.stop()
    shots = os.path.join(work, "shots")
    shot = lambda n: os.path.join(shots, n + ".png")  # noqa: E731

    def field(n):
        """the search field's box in a screenshot, as black pixels"""
        from PIL import Image
        im = Image.open(shot(n)).convert("L")
        x, y = SEARCH
        box = im.crop((x - 120, y - 10, x + 66, y + 10))
        return sum(1 for p in box.get_flattened_data() if p < 128)

    check("the click gave the field the focus (its frame and cursor)", field("1-clicked") > field("0-start") + 40)
    check("what was typed went into the field", same(shot("2-typed"), shot("1-clicked")) < 0.99)
    check("Delete in the field: no alert, nothing changed", same(shot("3-delete-in-field"), shot("2-typed")) > 0.999)
    check("a click on a contact took the focus away", field("4-contact-clicked") < field("2-typed") - 40)
    check("Delete on the contact then asked", same(shot("5-delete-asked"), shot("4-contact-clicked")) < 0.99)
    check("nothing deleted", count() == before)
    check("the clear button: the text gone, the focus kept",
          field("7-cleared") > field("0-start") + 40 and same(shot("7-cleared"), shot("6-delete-esc")) < 0.99)
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
