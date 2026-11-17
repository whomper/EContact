#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""One boot with no NATFEATS= in ECONTACT.INF, as on a real machine (the
regression gate's row 12, TEST-MATRIX.md; the guidelines 6.5): ECONTACT.PRG
starts, opens its window with the sample book, quits with ^Q, and leaves
no CRASH.LOG; the INF it writes has no NATFEATS= either. With LOG=1: the
log, kept open a second after a line (the guidelines 6.5, 17 Nov 2026),
is on the disk whole after the quit, to its last step.

    tests/hatari/natfeats_off.py WORK [MACHINE]    (Falcon-4:256 by default;
                                                  AERA_WITH_NVDI=1 for Erez's)
"""
import os
import re
import shutil
import subprocess
import sys
import time
sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg, ci_path  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.environ.get("ECONTACT_BIN", os.path.join(ROOT, "binary"))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def differ(a, b):
    """how many bytes two screenshots' files differ in (0: the same picture)"""
    x, y = open(a, "rb").read(), open(b, "rb").read()
    return abs(len(x) - len(y)) + sum(1 for i, j in zip(x, y) if i != j)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    work, machine = os.path.abspath(sys.argv[1]), machine_arg(sys.argv, 2, "Falcon-4:256")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf = open(os.path.join(folder, "econtact.inf"), "rb").read()
    inf = inf.replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    inf = re.sub(rb"\nNATFEATS=[^\r\n]*\r?\n", b"\n", inf)
    inf = re.sub(rb"\nLOG=0", b"\nLOG=1", inf)
    open(os.path.join(hd, "econtact.inf"), "wb").write(inf)
    os.remove(os.path.join(folder, "econtact.inf"))
    shutil.copy(os.path.join(BIN, "econtact.prg"), folder)
    r = Rig(work, machine, auto="C:\\ECONTACT\\econtact.prg", nf=False)
    try:
        time.sleep(40)
        win = r.shot("1-window")
        r.ctrl("q")
        time.sleep(10)
        desk = r.shot("2-quit")
        check("the window drawn, then gone at ^Q (the screen changed)", differ(win, desk) > 1000)
        r.ctrl("q")	# nothing should be there to take it
        time.sleep(3)
        check("Hatari still running", r.proc.poll() is None)
    finally:
        r.stop()
    crash = [n for n in os.listdir(folder) + os.listdir(hd) if n.lower() == "crash.log"]
    check("no CRASH.LOG", not crash)
    t = open(ci_path(hd, "econtact.inf"), "rb").read()
    check("the INF it wrote: its window (WINDOW=), no NATFEATS=", b"\nWINDOW=" in t and b"NATFEATS" not in t.upper())
    lp = ci_path(folder, "econtact.log")
    lt = open(lp, encoding="latin-1").read() if os.path.exists(lp) else ""
    check("ECONTACT.LOG whole after the quit: its start to 'quit: out of the AES' (%d lines)" % lt.count("\n"),
          "EContact 2" in lt and lt.rstrip().endswith("quit: out of the AES"))
    log = open(os.path.join(work, "hatari.log"), errors="replace").read()
    check("nothing through NatFeats (no AESGAP or EContact line in Hatari's log)",
          "AESGAP" not in log and "EContact" not in log)
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


main()
