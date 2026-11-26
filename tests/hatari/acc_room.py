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
gives a program, must be BUDGET bytes or more (1,024,000 on ST-2: 2.18 gives
1,030,540 with its heap held, some 6 KB of room for growth). Its first boot (no MEMKB= yet,
ECONTACT.INF with FOLDER= alone and the folder's cache empty: accmem's
first share of what is free, as the family check boots it), a boot with
no INF at all, and the same boot without the accessory, for the record;
--hd starts from another drive C: (quiet_start.py's), --memkb adds a
boot with that MEMKB=, and two with HEAPKB=64 and HEAPKB=300. On each boot
with an INF the accessory says where its heap is (NatFeats), and that must
lie outside the program's block: on TOS 1.04 and 4.04 a heap Malloc'd at
boot while a program started at boot was current belonged to that program,
and its end gave it to the next one (26 Nov 2026; the room didn't move with
the heap's size). Common's acc_core (9fd1335) takes it as the accessory's own. acc-opened: opened over a program (build/memopen.prg) that then
quits, the accessory opened again from the Desk menu draws as before.

    tests/hatari/acc_room.py WORK [MACHINE] [--bin DIR] [--memkb N]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machines  # noqa: E402
from esc_test import same  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
# ST-2's room beside the accessory, its first boot (2.18: 1,030,540, its heap its own
# since common's acc_core, 9fd1335); a release that eats the margin fails here first.
# (1 MB's 392,000 went with the accessories' 2 MB, the guidelines 2.1)
BUDGET = 1024000
WIN = (0, 40, 640, 380)     # the accessory's window as it opens on an ST mono screen


def probe(work, machine, acc, memkb, template=None, folder=False, heapkb=0, opened=False):
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
    if opened:
        # an address book (the sample contacts), so the first opening has no Welcome:
        # card-cli's INF moved to the root, its FOLDER= the Atari's
        folder = os.path.join(hd, "econtact")
        shutil.rmtree(folder, ignore_errors=True)
        os.remove(os.path.join(hd, "econtact.inf"))
        cli = os.path.join(ROOT, "build", "card-cli")
        subprocess.run([cli, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([cli, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                       check=True, stdout=subprocess.DEVNULL)
        t = open(os.path.join(folder, "econtact.inf"), "rb").read()
        os.remove(os.path.join(folder, "econtact.inf"))
        t = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", t).replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
        open(os.path.join(hd, "econtact.inf"), "wb").write(t)
    for f in ("memprobe.prg", "memlaunch.prg", "memopen.prg"):
        shutil.copy(os.path.join(ROOT, "build", f), os.path.join(hd, f))
    if acc:
        shutil.copy(acc, os.path.join(hd, "econtact.acc"))
    r = Rig(work, machine, auto="C:\\MEMOPEN.PRG" if opened else "C:\\MEMLAUNCH.PRG")
    for key, val in (("MEMKB", memkb), ("HEAPKB", heapkb)):
        if val:
            # (after Rig's natfeats(), which writes ECONTACT.INF: the key added to it)
            inf = machines.ci_path(hd, "econtact.inf")
            with open(inf, "a") as f:
                f.write("%s=%d\r\n\r\n" % (key, val))
    first = None
    try:
        for _ in range(120):
            time.sleep(1)
            log = open(os.path.join(work, "hatari.log"), errors="replace").read()
            if opened and first is None and "MEMLAUNCH AC_OPEN sent" in log:
                time.sleep(10)
                first = r.shot("1-opened-over-the-program")
            m = re.search(r"MEMPROBE tpa=(\d+) free=(\d+) total=\d+ low=([0-9a-f]+) high=([0-9a-f]+)", log)
            if m:
                h = re.search(r"ECONTACT heap at ([0-9a-f]+)", log)
                got = (int(m.group(1)), int(m.group(2)), int(m.group(3), 16), int(m.group(4), 16),
                       int(h.group(1), 16) if h else None)
                if opened:
                    # the program it was opened over has quit, and the probe after it:
                    # the accessory opened again from the Desk menu draws as before
                    time.sleep(4)
                    r.click(30, 8)
                    time.sleep(2)
                    r.click(60, 59)
                    time.sleep(12)
                    again = r.shot("2-opened-again")
                    got += (same(first, again, WIN) if first else 0.0,
                            any(f.lower() == "crash.log" for _, _, fs in os.walk(hd) for f in fs))
                return got
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
    cases += [("acc-opened", acc, 0, True, 0)]
    rooms = {}
    for name, f, memkb, folder, heapkb in cases:
        got = probe(os.path.join(a.work, name), a.machine, f, memkb, a.hd, folder, heapkb, name == "acc-opened")
        if not got:
            print("  FAIL %s: memprobe said nothing" % name)
            fails += 1
            continue
        tpa, free, low, high, heap = got[:5]
        if name == "acc-opened":
            ok = got[5] > 0.99 and not got[6]
            print("  %s acc-opened: opened over a program that then quit, opened again it draws as before "
                  "(%.3f alike), no CRASH.LOG" % ("ok  " if ok else "FAIL", got[5]))
            fails += not ok
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
