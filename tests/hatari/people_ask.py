#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PEOPLE (the guidelines, 4.1) in Hatari on an ST: ASKER.PRG plays EMail
(tests/atari/asker.c, make build/ASKER.PRG) and asks the accessory
(ECONTST.ACC) at boot for the name behind an address:

    tests/hatari/people_ask.py WORK etos512us.img

The accessory hasn't read its books yet (START=5), so the first answer
is an empty name; when it has, it says changed:, the asker asks again
and gets the name. Then the asker picks "Grace": the accessory's window
opens to choose, searching for her, and Return chooses her.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def main():
    work, tos = os.path.abspath(sys.argv[1]), sys.argv[2]
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf = open(os.path.join(folder, "ECONTACT.INF"), "rb").read()
    inf = inf.replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nSTART=[^\r\n]*", rb"\nSTART=5", inf)
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTST.ACC"), os.path.join(hd, "ECONTACT.ACC"))
    shutil.copy(os.path.join(ROOT, "build", "ASKER.PRG"), os.path.join(hd, "ASKER.PRG"))
    said = os.path.join(hd, "ASKER.TXT")

    def text():
        return open(said, encoding="latin-1").read() if os.path.exists(said) else ""

    r = Rig(work, tos, machine="megast", extra=["--memsize", "4", "--monitor", "mono"], auto="C:\\ASKER.PRG")
    try:
        t0 = time.time()
        while time.time() - t0 < 300 and "pick sent" not in text():
            time.sleep(2)
        time.sleep(6)
        r.shot("choosing")
        r.key(K["RETURN"])
        t1 = time.time()
        while time.time() - t1 < 60 and "ASKER done" not in text() and "gave up" not in text():
            time.sleep(2)
        time.sleep(2)
        r.shot("after")
    finally:
        r.stop()
    t = text()
    print(t)
    lines = t.splitlines()
    names = [l for l in lines if l.startswith("ASKER name")]
    check("the first answer, before the books are read: no name", bool(names) and names[0].endswith('= ""'))
    check("changed: when they are read", "ASKER changed" in lines)
    check("then the name", any(l.endswith('= "Grace Baker"') for l in names))
    check("pick: Grace chosen in the window",
          "ASKER picked 1: grace.baker@example.net Grace Baker" in lines)
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
