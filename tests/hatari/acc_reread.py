#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""The accessory while the program changes its files, on single TOS,
checked in Hatari on an ST (ECONTST.ACC):

    tests/hatari/acc_reread.py WORK etos512us.img

The accessory's memory outlives its window: it is opened (the contacts
read), closed, and while it waits in the Desk menu card-cli plays
ECONTACT.PRG and adds a contact (the cache written). Opened again, the
accessory must read the files again (a line in ECONTACT.LOG) and show
the new contact; opened once more with nothing changed, it must not.
Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def main():
    work, tos = sys.argv[1], sys.argv[2]
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(folder, "ECONTACT.INF"), "rb") as f:
        inf = f.read()
    # in the root, as ECONTACT.PRG leaves it; a log, and no refresh at opening
    inf = inf.replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    with open(os.path.join(hd, "ECONTACT.INF"), "wb") as f:
        f.write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTST.ACC"), os.path.join(hd, "ECONTACT.ACC"))
    log = os.path.join(folder, "ECONTACT.LOG")

    def reads():
        if not os.path.exists(log):
            return 0
        return open(log, encoding="latin-1").read().count("reading them again")

    r = Rig(work, tos, machine="megast", extra=["--memsize", "4", "--monitor", "mono"], auto="")
    try:
        def open_acc(wait):
            r.click(30, 8)		# Desk
            time.sleep(1.5)
            r.click(60, 59)		# Contacts
            time.sleep(wait)

        def close_acc(name):
            r.ctrl("w")
            time.sleep(3)
            r.shot(name)

        time.sleep(15)
        open_acc(8)
        r.shot("1-open")
        close_acc("1-closed")
        # ECONTACT.PRG, meanwhile: a contact added
        subprocess.run([CLI, folder, "add", "Contacts", "Zoe", "Zimmer", "zoe@example.com"], check=True,
                       stdout=subprocess.DEVNULL)
        open_acc(8)
        r.shot("2-again")
        close_acc("2-closed")
        check("read again after the program changed the files", reads() == 1)
        open_acc(5)
        r.shot("3-unchanged")
        close_acc("3-closed")
        check("not read again when nothing changed", reads() == 1)
    finally:
        r.stop()
    out = subprocess.run([CLI, folder, "list"], check=True, capture_output=True, text=True).stdout
    check("the new contact kept", "Zoe Zimmer | zoe@example.com" in out)
    check("the others with it", out.count("\n") == 22)
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
