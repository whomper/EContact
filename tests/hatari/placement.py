#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Where EContact finds its settings and its folder, as ECal does, checked
in Hatari on an ST (ECONTST.ACC, ECONTACT.PRG):

    tests/hatari/placement.py WORK etos512us.img

1. the accessory, nothing there yet: it asks, and C:\\ECONTACT is made,
   with FOLDER=C:\\ECONTACT in C:\\ECONTACT.INF
2. the program, started from C:\\APPS\\CONTACTS: its folder is that one,
   written into FOLDER=; then the accessory, booted on the same disk,
   uses it without asking
3. the accessory with settings only in C:\\ECONTACT\\ECONTACT.INF (copied
   there, or made by card-cli): copied to the root, contacts shown
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "binary")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def folder_of(hd):
    p = os.path.join(hd, "ECONTACT.INF")
    if not os.path.exists(p):
        return None
    for ln in open(p, encoding="latin-1"):
        if ln.startswith("FOLDER="):
            return ln[7:].strip()
    return ""


def boot(work, tos, auto, steps):
    r = Rig(work, tos, machine="megast", extra=["--memsize", "4", "--monitor", "mono"], auto=auto)
    try:
        time.sleep(15)
        for s in steps:
            if isinstance(s, tuple):
                r.click(*s)
            else:
                time.sleep(s)
    finally:
        r.stop()


def desk_contacts():
    return [(30, 8), 1.5, (60, 59), 5]	# Desk > Contacts


def main():
    work, tos = sys.argv[1], sys.argv[2]
    acc = os.path.join(BIN, "ECONTST.ACC")

    print("1. the accessory, from nothing")
    w = os.path.join(work, "a")
    shutil.rmtree(w, ignore_errors=True)
    os.makedirs(os.path.join(w, "hd"))
    shutil.copy(acc, os.path.join(w, "hd", "ECONTACT.ACC"))
    boot(w, tos, "", desk_contacts() + [(375, 248), 4])	# "C:\ECONTACT"
    check("FOLDER=C:\\ECONTACT", folder_of(os.path.join(w, "hd")) == "C:\\ECONTACT")
    check("the folder made, the key in it", os.path.exists(os.path.join(w, "hd", "ECONTACT", "ECONTACT.KEY")))

    print("2. the program in its own folder, then the accessory")
    w = os.path.join(work, "b")
    shutil.rmtree(w, ignore_errors=True)
    app = os.path.join(w, "hd", "APPS", "CONTACTS")
    os.makedirs(app)
    shutil.copy(os.path.join(BIN, "ECONTACT.PRG"), app)
    boot(w, tos, "C:\\APPS\\CONTACTS\\ECONTACT.PRG", [6])
    check("FOLDER= the program's", folder_of(os.path.join(w, "hd")) == "C:\\APPS\\CONTACTS")
    check("its files next to it", os.path.exists(os.path.join(app, "ECONTACT.KEY")))
    shutil.copy(acc, os.path.join(w, "hd", "ECONTACT.ACC"))
    boot(w, tos, "", desk_contacts())
    check("the accessory kept it", folder_of(os.path.join(w, "hd")) == "C:\\APPS\\CONTACTS")
    check("and made no folder of its own", not os.path.exists(os.path.join(w, "hd", "ECONTACT")))

    print("3. the accessory, settings in the folder only")
    w = os.path.join(work, "c")
    shutil.rmtree(w, ignore_errors=True)
    folder = os.path.join(w, "hd", "ECONTACT")
    os.makedirs(folder)
    cli = os.path.join(ROOT, "build", "card-cli")
    subprocess.run([cli, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([cli, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    shutil.copy(acc, os.path.join(w, "hd", "ECONTACT.ACC"))
    boot(w, tos, "", desk_contacts() + [6])
    check("copied to the root, FOLDER=C:\\ECONTACT", folder_of(os.path.join(w, "hd")) == "C:\\ECONTACT")
    check("the book with it", "[BOOK]" in open(os.path.join(w, "hd", "ECONTACT.INF"), encoding="latin-1").read())

    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
