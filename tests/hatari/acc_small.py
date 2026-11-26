#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""No accessory on a 1 MB machine (the guidelines 2.1, decided 26 Nov 2026):
ECONTACT.ACC on ST-1 (1 MB, TOS 1.04) takes no heap and reads nothing; its
Desk entry gives common's acc_small_alert, and nothing else.

1. alert: nothing at boot, the desktop up; "  EContact" in the Desk menu
   gives the alert; OK gives the desktop back, its Desk menu opening; no
   CRASH.LOG, no ECONTACT.INF or folder written.
2. memory: build/memprobe.prg's free memory (every free block, "total")
   started straight at boot and started by build/memlaunch.prg once the
   AES has run the accessory to its first event call: the same, so the
   accessory took nothing when it started; and started by build/memopen.prg,
   which first opens the accessory (AC_OPEN, its alert answered): the same
   again, so the alert took nothing either. A boot without it, for the
   record: what it costs is its resident image, which only taking
   ECONTACT.ACC off the boot drive gives back.

    tests/hatari/acc_small.py WORK [--bin DIR]
"""
import argparse
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, machines  # noqa: E402
from cpu_alert_test import gray, menu_bar, alert_box, ACC_Y  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
MACHINE = "ST-1:mono"
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)


def drive(d, acc):
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    os.makedirs(hd)
    for f in ("memprobe.prg", "memlaunch.prg", "memopen.prg"):
        shutil.copy(os.path.join(ROOT, "build", f), os.path.join(hd, f))
    if acc:
        shutil.copy(acc, os.path.join(hd, "econtact.acc"))
    return hd


def alert_case(work, acc):
    d = os.path.join(work, "alert")
    hd = drive(d, acc)
    r = Rig(d, MACHINE, auto=None)
    try:
        time.sleep(30)
        boot = gray(r.shot("0-boot"))
        check("alert: nothing at boot, the desktop up", menu_bar(boot) and not alert_box(boot))
        r.click(30, 8)                   # Desk
        time.sleep(2)
        r.shot("1-desk-menu")
        r.click(60, ACC_Y)               # "  EContact"
        time.sleep(4)
        check("alert: the Desk entry gives the alert", alert_box(gray(r.shot("2-alert"))))
        r.key(K["RETURN"])               # OK
        time.sleep(6)
        after = gray(r.shot("3-after"))
        check("alert: the desktop's menu bar after OK", menu_bar(after) and not alert_box(after))
        r.click(30, 8)
        time.sleep(2)
        menu = gray(r.shot("4-desk-menu"))
        drop = (40, 20, 200, 120)
        check("alert: the Desk menu opens (the machine answers)",
              menu.crop(drop).tobytes() != after.crop(drop).tobytes())
        r.click(320, 300)
        time.sleep(1)
    finally:
        r.stop()
    names = [f.lower() for _, ds, fs in os.walk(hd) for f in fs + ds]
    check("alert: no CRASH.LOG", "crash.log" not in names)
    check("alert: no ECONTACT.INF nor folder written (natfeats' INF aside)",
          "econtact" not in names and "econtact.log" not in names)


def probe(work, name, acc, auto):
    d = os.path.join(work, name)
    drive(d, acc)
    r = Rig(d, MACHINE, auto=auto)
    answered = False
    try:
        for _ in range(150):
            time.sleep(1)
            log = open(os.path.join(d, "hatari.log"), errors="replace").read()
            if not answered and "MEMLAUNCH AC_OPEN sent" in log:
                time.sleep(4)
                check("memory: AC_OPEN gives the alert", alert_box(gray(r.shot("alert"))))
                r.key(K["RETURN"])       # OK
                answered = True
            m = re.search(r"MEMPROBE tpa=(\d+) free=(\d+) total=(\d+)", log)
            if m:
                return [int(x) for x in m.groups()]
    finally:
        r.stop()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--bin", default=os.path.join(ROOT, "binary"))
    a = ap.parse_args()
    acc = machines.ci_path(a.bin, "econtact.acc")
    alert_case(a.work, acc)
    none = probe(a.work, "mem-none", None, "C:\\MEMLAUNCH.PRG")
    early = probe(a.work, "mem-early", acc, "C:\\MEMPROBE.PRG")
    late = probe(a.work, "mem-late", acc, "C:\\MEMLAUNCH.PRG")
    opened = probe(a.work, "mem-opened", acc, "C:\\MEMOPEN.PRG")
    check("memory: the probe said what it had (%s, %s, %s, %s)" % (none, early, late, opened),
          none and early and late and opened)
    if none and early and late and opened:
        check("memory: free the same before the accessory's start and after it (%d, %d bytes)"
              % (early[2], late[2]), early[2] == late[2])
        check("memory: free the same after its Desk entry and alert (%d bytes)" % opened[2],
              opened[2] == late[2])
        print("  info without ECONTACT.ACC %d bytes free in all, %d for a program; with it %d and %d"
              % (none[2], none[0], late[2], late[0]), flush=True)
    print("acc_small: all passed" if not fails else "acc_small: %d failed" % len(fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
