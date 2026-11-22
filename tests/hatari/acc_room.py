#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The room a program has beside ECONTACT.ACC on a 1 MB ST (the gate's row:
a 1 MB ST runs another program beside the accessory). ST-1 boots with
econtact.acc in the root and starts build/memprobe.prg (tests/atari/
memprobe.c) once the desktop is up: its p_hitpa - p_lowtpa, the block TOS
gives a program, must be BUDGET bytes or more (365,000: EFTP's 359,200
and room; 2.13 left too little for it). Its first boot (no MEMKB= yet,
accmem's share of what is free) and a boot with the MEMKB= a book of 350
contacts asks for (tests/hatari/quiet_start.py's) are both measured, and
the same boot without the accessory, for the record.

    tests/hatari/acc_room.py WORK [MACHINE] [--bin DIR] [--memkb N]
"""
import argparse
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machines  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
BUDGET = 365000


def probe(work, machine, acc, memkb, template=None):
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    if template:
        shutil.copytree(template, hd)
        for f in os.listdir(hd):
            if f.lower() == "econtact.acc":
                os.remove(os.path.join(hd, f))
    else:
        os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "build", "memprobe.prg"), os.path.join(hd, "memprobe.prg"))
    if acc:
        shutil.copy(acc, os.path.join(hd, "econtact.acc"))
    r = Rig(work, machine, auto="C:\\MEMPROBE.PRG")
    if memkb:
        # (after Rig's natfeats(), which writes ECONTACT.INF: MEMKB= added to it)
        inf = machines.ci_path(hd, "econtact.inf")
        with open(inf, "a") as f:
            f.write("MEMKB=%d\r\n\r\n" % memkb)
    try:
        for _ in range(90):
            time.sleep(1)
            m = re.search(r"MEMPROBE tpa=(\d+) free=(\d+)", open(os.path.join(work, "hatari.log"), errors="replace").read())
            if m:
                return int(m.group(1)), int(m.group(2))
    finally:
        r.stop()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("machine", nargs="?", default="ST-1:mono")
    ap.add_argument("--bin", default=os.path.join(ROOT, "binary"))
    ap.add_argument("--hd", help="a drive C: to start from (quiet_start.py's, its 350 contacts and ECONTACT.INF)")
    ap.add_argument("--memkb", type=int, default=0, help="MEMKB= for the second case (0: first boot only)")
    a = ap.parse_args()
    acc = machines.ci_path(a.bin, "econtact.acc")
    fails = 0
    cases = [("none", None, 0), ("acc-first-boot", acc, 0)]
    if a.memkb:
        cases.append(("acc-memkb", acc, a.memkb))
    for name, f, memkb in cases:
        got = probe(os.path.join(a.work, name), a.machine, f, memkb, a.hd)
        if not got:
            print("  FAIL %s: memprobe said nothing" % name)
            fails += 1
            continue
        tpa, free = got
        ok = f is None or tpa >= BUDGET
        print("  %s %s: a program gets %d bytes (%d free after its Mshrink)%s" %
              ("ok  " if ok else "FAIL", name, tpa, free, "" if f is None else ", budget %d" % BUDGET))
        fails += not ok
    print("acc_room: all passed" if not fails else "acc_room: %d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
