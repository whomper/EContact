#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""How long reading a card takes on an Atari, in Hatari:

    make build/VCBENCH.PRG
    tests/hatari/vcbench.py WORK [MACHINE] [CARDS] [--prg PRG]

The cards tests/hatari/quiet_start.py makes (600 by default) in
C:\\VCBENCH.VCF, read by VCBENCH.PRG (tests/atari/vcbench.c) on the
machine (ST:mono by default): what it says, the time each card takes
looking for its photo, being parsed, and freed. --prg runs another build
of it (another version of vcard.c, say). 1.56 on an ST, TOS 1.04: the
photo 7.3 ms a card, the parse 72.9 ms; 1.57: 3.8 and 39.4.
"""
import glob
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402
import quiet_start  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def main():
    argv = sys.argv[:]
    prg = os.path.join(ROOT, "build", "VCBENCH.PRG")
    if "--prg" in argv:
        i = argv.index("--prg")
        prg = os.path.abspath(argv[i + 1])
        del argv[i:i + 2]
    work, machine = os.path.abspath(argv[1]), machine_arg(argv, 2, "ST:mono")
    n = int(argv[3]) if len(argv) > 3 else 600
    if not os.path.exists(prg):
        sys.exit("%s isn't there: make build/VCBENCH.PRG" % prg)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    quiet_start.cards(os.path.join(hd, "VCBENCH.VCF"), n)
    shutil.copy(prg, os.path.join(hd, "VCBENCH.PRG"))
    r = Rig(work, machine, auto="C:\\VCBENCH.PRG")
    txt = ""
    try:
        t0 = time.time()
        while time.time() - t0 < 900:
            txt = "".join(open(f, errors="replace").read() for f in glob.glob(os.path.join(work, "*.log")))
            if "VCB done" in txt or "VCB no" in txt:
                break
            time.sleep(3)
    finally:
        r.stop()
    out = [l for l in txt.splitlines() if l.startswith("VCB")]
    print("\n".join(out) if out else "no answer from VCBENCH.PRG")
    sys.exit(0 if "VCB done" in txt else 1)


if __name__ == "__main__":
    main()
