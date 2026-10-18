#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A shortcut typed while EContact is busy (the guidelines, 3.2), in
Hatari with ECONTACT.PRG:

    tests/hatari/busy_keys.py WORK [MACHINE]       (ST:mono by default)

600 contacts and no index: the program reads them all as it starts. ^N
is typed while it does; the AES hands the key over afterwards, with the
Control key long let go (kstate 0), so only its code says it was ^N
(key_ctrl, common/atari/gem.h). The New Contact dialog must open once
the contacts are read, and Esc closes it.
Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg, K, SCAN  # noqa: E402
from quiet_start import cards  # noqa: E402
from esc_test import same  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def main():
    work, machine = os.path.abspath(sys.argv[1]), machine_arg(sys.argv, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    folder = os.path.join(work, "hd", "ECONTACT")
    os.makedirs(folder)
    cards(os.path.join(work, "p.vcf"), 600)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(work, "p.vcf")], check=True,
                   stdout=subprocess.DEVNULL)
    for f in os.listdir(os.path.join(folder, "CACHE")):
        if f.endswith(".IDX"):              # every card parsed: a long start
            os.remove(os.path.join(folder, "CACHE", f))
    inf_path = os.path.join(folder, "ECONTACT.INF")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0").replace(b"\nLOG=0", b"\nLOG=1")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), folder)
    log = os.path.join(folder, "ECONTACT.LOG")
    r = Rig(work, machine, auto="C:\\ECONTACT\\ECONTACT.PRG")
    try:
        t0 = time.time()
        while time.time() - t0 < 60 and not os.path.exists(log):
            time.sleep(0.5)
        time.sleep(2)                       # reading the cards
        r.shot("1-busy")
        r.key(SCAN["n"], ctrl=True)         # ^N, the Control key let go at once
        typed = time.time()
        while time.time() - t0 < 600:
            time.sleep(2)
            if "the list sorted" in open(log, encoding="latin-1").read():
                break
        busy = time.time() - typed
        time.sleep(5)
        r.shot("2-read")
        r.key(K["ESC"])
        time.sleep(3)
        r.shot("3-esc")
        r.ctrl("q")
        time.sleep(5)
    finally:
        r.stop()
    shots = os.path.join(work, "shots")
    shot = lambda n: os.path.join(shots, n + ".png")  # noqa: E731
    check("^N typed while busy (it went on for %.0f s after it)" % busy, busy > 5)
    check("the New Contact dialog came once the contacts were read",
          same(shot("2-read"), shot("3-esc")) < 0.97)
    print("screenshots in", shots)
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
