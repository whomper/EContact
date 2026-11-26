#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The room a program has beside ECONTACT.ACC (the gate's row: a program
runs beside the accessory), on ST-2 since the accessories need 2 MB (the
guidelines 2.1, decided 26 Nov 2026; ST-1's case is acc_small.py). It boots with
econtact.acc in the root and starts build/memprobe.prg (tests/atari/
memprobe.c) as a user would, once the AES has run the accessories to their
first event call (build/memlaunch.prg, tests/atari/memlaunch.c, starts it
after 5 s of evnt_timer): its p_hitpa - p_lowtpa, the block TOS
gives a program, must be BUDGET bytes or more (392,000: EFTP started beside
2.12's 388,080 and not beside 2.13's 386,544; 4 KB more than 2.12). Its first boot (no MEMKB= yet,
ECONTACT.INF with FOLDER= alone and the folder's cache empty: accmem's
first share of what is free, as the family check boots it), a boot with
no INF at all, and the same boot without the accessory, for the record;
--hd starts from another drive C: (quiet_start.py's), --memkb adds a
boot with that MEMKB=, and two with HEAPKB=64 and HEAPKB=300. On each boot
with an INF the accessory says where its heap is (NatFeats), and that must
lie outside the program's block: on TOS 1.04 and 4.04 a heap Malloc'd at
boot was given to the program started next (26 Nov 2026; common's fix to
come), and the program's room didn't move with the heap's size.

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
# the room EFTP 2.16 needed (the family check, 22 Nov 2026): it started beside
# 2.12 (388,080 here) and not beside 2.13 (386,544); this budget is 2.12's
# and 4 KB more, so a release that eats the margin again fails here first
BUDGET = 392000


def probe(work, machine, acc, memkb, template=None, folder=False, heapkb=0):
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    if template:
        shutil.copytree(template, hd)
        for f in os.listdir(hd):
            if f.lower() == "econtact.acc":
                os.remove(os.path.join(hd, f))
    else:
        os.makedirs(hd)
    if folder:
        # the family check's first boot: ECONTACT.INF with FOLDER= alone, the
        # folder there with an empty cache, no MEMKB= (accmem's first share)
        os.makedirs(os.path.join(hd, "econtact", "cache"))
        with open(os.path.join(hd, "econtact.inf"), "wb") as f:
            f.write(b"[OPTIONS]\r\nFOLDER=C:\\ECONTACT\r\n\r\n")
    for f in ("memprobe.prg", "memlaunch.prg"):
        shutil.copy(os.path.join(ROOT, "build", f), os.path.join(hd, f))
    if acc:
        shutil.copy(acc, os.path.join(hd, "econtact.acc"))
    r = Rig(work, machine, auto="C:\\MEMLAUNCH.PRG")
    for key, val in (("MEMKB", memkb), ("HEAPKB", heapkb)):
        if val:
            # (after Rig's natfeats(), which writes ECONTACT.INF: the key added to it)
            inf = machines.ci_path(hd, "econtact.inf")
            with open(inf, "a") as f:
                f.write("%s=%d\r\n\r\n" % (key, val))
    try:
        for _ in range(90):
            time.sleep(1)
            log = open(os.path.join(work, "hatari.log"), errors="replace").read()
            m = re.search(r"MEMPROBE tpa=(\d+) free=(\d+) total=\d+ low=([0-9a-f]+) high=([0-9a-f]+)", log)
            if m:
                h = re.search(r"ECONTACT heap at ([0-9a-f]+)", log)
                return (int(m.group(1)), int(m.group(2)), int(m.group(3), 16), int(m.group(4), 16),
                        int(h.group(1), 16) if h else None)
    finally:
        r.stop()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("machine", nargs="?", default="ST-2:mono")
    ap.add_argument("--bin", default=os.path.join(ROOT, "binary"))
    ap.add_argument("--hd", help="a drive C: to start from (quiet_start.py's, its 350 contacts and ECONTACT.INF)")
    ap.add_argument("--memkb", type=int, default=0, help="MEMKB= for the second case (0: first boot only)")
    a = ap.parse_args()
    acc = machines.ci_path(a.bin, "econtact.acc")
    fails = 0
    cases = [("none", None, 0, False, 0), ("acc-no-inf", acc, 0, False, 0), ("acc-first-boot", acc, 0, True, 0)]
    if a.memkb:
        cases.append(("acc-memkb", acc, a.memkb, False, 0))
    cases += [("acc-heap64", acc, 0, True, 64), ("acc-heap300", acc, 0, True, 300)]
    rooms = {}
    for name, f, memkb, folder, heapkb in cases:
        got = probe(os.path.join(a.work, name), a.machine, f, memkb, a.hd, folder, heapkb)
        if not got:
            print("  FAIL %s: memprobe said nothing" % name)
            fails += 1
            continue
        tpa, free, low, high, heap = got
        rooms[name] = tpa
        ok = f is None or tpa >= BUDGET
        print("  %s %s: a program gets %d bytes (%d free after its Mshrink)%s" %
              ("ok  " if ok else "FAIL", name, tpa, free, "" if f is None else ", budget %d" % BUDGET))
        fails += not ok
        if f is not None and heap is None:
            print("  info %s: the accessory said nothing of its heap (no ECONTACT.INF, so no NATFEATS=1)" % name)
        elif f is not None:
            # the accessory's heap must stay its own: never inside the block TOS
            # gives the program started after it (on TOS 1.04 and 4.04 it was,
            # 26 Nov 2026: a non-fast-load program's start cleared it)
            ok = not (low <= heap < high)
            print("  %s %s: the accessory's heap (%08x) outside the program's block %08x-%08x" %
                  ("ok  " if ok else "FAIL", name, heap, low, high))
            fails += not ok
    if "acc-no-inf" in rooms:
        moved = [n for n in ("acc-heap64", "acc-heap300") if n in rooms and rooms[n] != rooms["acc-no-inf"]]
        print("  info the heap (HEAPKB=64, 300) %s" % ("moves the program's room: " + ", ".join(
            "%s %d" % (n, rooms[n]) for n in moved) if moved else "doesn't move the program's room"))
    print("acc_room: all passed" if not fails else "acc_room: %d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
