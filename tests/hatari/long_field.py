#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A text field holds more than it shows (the guidelines, 5.4), in Hatari
with ECONTACT.PRG:

    tests/hatari/long_field.py WORK [MACHINE]

A new contact (^N): 50 letters typed into the first name, a field 30
wide, then Shift+Left (the start) and a letter, Shift+Right (the end) and
another; Return saves. card-cli must read back all 52 letters, the two
at their ends. Screenshots in WORK/shots.
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
LONG = "abcdefghijklmnopqrstuvwxy" * 2      # 50 letters


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
    r = Rig(work, machine, auto="C:\\ECONTACT\\ECONTACT.PRG")
    try:
        time.sleep(25)
        r.ctrl("n")                     # a new contact: the first name has the focus
        time.sleep(3)
        r.type(LONG, pace=0.05)
        time.sleep(1)
        r.shot("1-typed")
        r.key(K["LEFT"], shift=True)    # the start
        r.type("q")
        r.key(K["RIGHT"], shift=True)   # the end
        r.type("z")
        time.sleep(1)
        r.shot("2-ends")
        r.key(K["RETURN"])              # Save
        time.sleep(4)
        r.shot("3-saved")
        r.ctrl("q")
        time.sleep(8)
    finally:
        r.stop()
    out = subprocess.run([CLI, folder, "list"], check=True, capture_output=True, text=True).stdout
    want = "q" + LONG + "z"
    check("the first name kept whole: %d letters in a field 30 wide" % len(want), want in out)
    if want not in out:
        print("  (card-cli list: %s)" % [l for l in out.splitlines() if "abc" in l])
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
