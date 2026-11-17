#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Settings in the family's layout and the keyboard's reach (the guidelines 6.1, 5.10; D24, D25, D30):

    tests/hatari/settings_keys.py WORK [MACHINE]      (ST:mono by default)

ECONTACT.PRG with the sample book; ^, opens Settings on General, its page list in the focus (no text field there):
- Down, Down, Down: Refresh, Secure, Advanced, each shown (a screenshot of every page, for comparing with common's
  settings.c);
- Up, Up: Secure; Up: Refresh; Tab: Refresh all's radio group, its ring; Right: 15 min; Tab: the tick; Space: it
  turns; Esc: nothing kept, ECONTACT.INF as it was;
- again: Down to Refresh, Tab, Right, Return (OK): REFRESH=15 in ECONTACT.INF;
- Secure's port 0 and OK: refused with an alert, the dialog open (D30); Return, Esc: nothing kept.
Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time
sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg, K, ci_path  # noqa: E402
from PIL import Image, ImageChops  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def differ(a, b):
    """pixels that differ between two screenshots"""
    d = ImageChops.difference(Image.open(a).convert("L"), Image.open(b).convert("L"))
    return sum(1 for v in d.getdata() if v)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    work, machine = os.path.abspath(sys.argv[1]), machine_arg(sys.argv, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "econtact.inf")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    inf = re.sub(rb"\nREFRESH=[^\r\n]*", rb"\nREFRESH=0", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "econtact.prg"), folder)

    def inf_now():
        """the INF the program uses: the root's, where it moves the folder's at its start"""
        root = ci_path(hd, "econtact.inf")
        return open(root if os.path.exists(root) else inf_path, "rb").read()

    r = Rig(work, machine, auto="C:\\ECONTACT\\econtact.prg")
    try:
        time.sleep(25)
        r.shot("0-start")
        start = inf_now()

        print("1. the pages by the list's keys")
        r.ctrl(",")
        time.sleep(3)
        pages = [r.shot("1-general")]
        for name in ("refresh", "secure", "advanced"):
            r.key(K["DOWN"])
            time.sleep(2)
            pages.append(r.shot("1-" + name))
        check("Down shows each page in turn (each screen another)",
              all(differ(pages[i], pages[i + 1]) > 200 for i in range(3)))

        print("2. a radio group and a tick by the keys, then Esc")
        for _ in range(3):
            r.key(K["UP"])
            time.sleep(1.5)
        refresh = r.shot("2-refresh-again")
        check("Up, Up, Up: back on General", differ(refresh, pages[0]) < 50)
        r.key(K["DOWN"])
        time.sleep(1.5)
        r.key(K["TAB"])
        time.sleep(1.5)
        ring = r.shot("2-tab-ring")
        check("Tab: the radio group's ring (the screen changed a little)", 0 < differ(ring, pages[1]) < 4000)
        r.key(K["RIGHT"])
        time.sleep(1.5)
        chosen = r.shot("2-right-15min")
        check("Right: another choice drawn", differ(chosen, ring) > 100)
        r.key(K["TAB"])
        time.sleep(1)
        r.key(0x39)		# Space
        time.sleep(1.5)
        ticked = r.shot("2-space-tick")
        check("Tab, Space: the tick turned", differ(ticked, chosen) > 20)
        r.key(K["ESC"])
        time.sleep(3)
        check("Esc: closed, ECONTACT.INF as it was", inf_now() == start)

        print("3. OK by Return")
        r.ctrl(",")
        time.sleep(3)
        r.key(K["DOWN"])
        time.sleep(1.5)
        r.key(K["TAB"])
        time.sleep(1)
        r.key(K["RIGHT"])
        time.sleep(1)
        r.key(K["RETURN"])
        time.sleep(4)
        check("Return: REFRESH=15 in ECONTACT.INF", b"\nREFRESH=15" in inf_now())

        print("4. a port out of range refused (D30)")
        r.ctrl(",")
        time.sleep(3)
        r.key(K["DOWN"])
        time.sleep(1)
        r.key(K["DOWN"])
        time.sleep(1.5)
        r.shot("4-secure")
        for _ in range(4):	# the port: the two ticks, the address, the port
            r.key(K["TAB"])
            time.sleep(0.6)
        r.shot("4-port-focus")
        r.key(K["ESC"])	# Esc empties the field the cursor is in
        time.sleep(1)
        r.type("0")
        time.sleep(1)
        before = inf_now()
        r.key(K["RETURN"])
        time.sleep(3)
        alert = r.shot("4-port-alert")
        r.key(K["RETURN"])	# the alert's OK
        time.sleep(2)
        still = r.shot("4-dialog-still-open")
        check("the alert came (the screen changed)", differ(alert, still) > 200)
        r.key(K["UNDO"])
        time.sleep(3)
        check("Undo: nothing kept, the port as it was", inf_now() == before and b"\nGATEPORT=0" not in inf_now())
    finally:
        r.stop()
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


main()
