#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The Help key in Hatari on an ST, with a stand-in for ST-Guide
(build/fakeguide.acc, `make build/fakeguide.acc`) that writes what a
VA_START names into C:\\VASTART.TXT:

    tests/hatari/help_key.py WORK [MACHINE]

1. the accessory, ECONTACT.HYP in its folder: ST-Guide is sent its path
2. the same without ECONTACT.HYP: *:\\ECONTACT.HYP sent, for ST-Guide's
   own paths (common/ui/help.c, as ECal's help_test.py)
3. the program, started from its folder: the same path sent
Screenshots in WORK/*/shots.
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
BIN = os.path.join(ROOT, "binary")
CLI = os.path.join(ROOT, "build", "card-cli")
GUIDE = os.path.join(ROOT, "build", "fakeguide.acc")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def disk(w):
    """C:\\ with ST-GUIDE.ACC and EContact's folder, contacts and help in it"""
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(folder, "ECONTACT.INF"), "rb") as f:
        inf = f.read()
    inf = inf.replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    with open(os.path.join(hd, "ECONTACT.INF"), "wb") as f:
        f.write(inf)
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    shutil.copy(os.path.join(BIN, "ECONTACT.HYP"), folder)
    shutil.copy(GUIDE, os.path.join(hd, "ST-GUIDE.ACC"))
    return hd, folder


def sent(hd):
    p = os.path.join(hd, "VASTART.TXT")
    return open(p, encoding="latin-1").read() if os.path.exists(p) else None


def main():
    work, machine = sys.argv[1], machine_arg(sys.argv, 2, "ST:mono")

    print("1. the accessory, with ECONTACT.HYP")
    w = os.path.join(work, "acc")
    hd, folder = disk(w)
    shutil.copy(os.path.join(BIN, "ECONTST.ACC"), os.path.join(hd, "ECONTACT.ACC"))
    r = Rig(w, machine, auto="")
    try:
        time.sleep(15)
        r.click(30, 8)		# Desk
        time.sleep(1.5)
        r.shot("desk-menu")
        r.click(60, 59)		# Contacts (accessories are listed by name)
        time.sleep(8)
        r.key(K["HELP"])
        time.sleep(3)
        r.shot("1-help")
        check("ST-Guide sent the help file's path", sent(hd) == "C:\\ECONTACT\\ECONTACT.HYP")

        print("2. without it")
        os.remove(os.path.join(hd, "VASTART.TXT"))
        os.remove(os.path.join(folder, "ECONTACT.HYP"))
        r.key(K["HELP"])
        time.sleep(3)
        r.shot("2-search")
        check("ST-Guide asked to look in its own paths", sent(hd) == "*:\\ECONTACT.HYP")
    finally:
        r.stop()

    print("3. the program")
    w = os.path.join(work, "prg")
    hd, folder = disk(w)
    shutil.copy(os.path.join(BIN, "ECONTACT.PRG"), folder)
    r = Rig(w, machine, auto="C:\\ECONTACT\\ECONTACT.PRG")
    try:
        time.sleep(22)
        r.key(K["HELP"])
        time.sleep(3)
        r.shot("3-help")
        check("ST-Guide sent the help file's path", sent(hd) == "C:\\ECONTACT\\ECONTACT.HYP")
    finally:
        r.stop()
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
