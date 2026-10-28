#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Several contacts chosen at once (Erez, 28 Oct 2026), in Hatari with
ECONTACT.PRG and the sample contacts:

    tests/hatari/multi_select.py WORK [MACHINE]

As EFTP's and EMail's lists: a click chooses one, Shift+click one more
(or one fewer), Control+click a range from the one chosen, ^A everyone
shown, and a right-click on one of them their menu. What is chosen is
read back through ^C (C:\\CLIPBRD\\SCRAP.TXT,
"Name <email>, ..."), and the card pane says "N Cards Selected". Then
two chosen are deleted with Delete and the alert's Delete button: both
go, the rest stay (card-cli list). Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg, K  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0

# the rows of the sample list (as in docs/window.png), x in the names
X = 200
ROW = {"Johnny": 114, "Grace": 168, "Maya": 204, "Luc": 240, "Olivia": 276, "Sofia": 348}
# the alert's Delete, its first button (centred: 39 lines lower on a
# Falcon's 480 than an ST's 400)
DELETE_ST, DELETE_FALCON = (283, 233), (283, 272)


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def same(a, b, box=None):
    """the share of pixels two screenshots (or a box of them) have alike"""
    from PIL import Image
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if box:
        ia, ib = ia.crop(box), ib.crop(box)
    pa, pb = ia.load(), ib.load()
    w, h = ia.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if pa[x, y] == pb[x, y])
    return n / ((w // 2) * (h // 2))


def main():
    work, machine = os.path.abspath(sys.argv[1]), machine_arg(sys.argv, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "ECONTACT.INF")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    open(inf_path, "wb").write(inf)
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), folder)
    scrap = os.path.join(hd, "CLIPBRD", "SCRAP.TXT")
    delete_at = DELETE_ST if machine.startswith("ST") else DELETE_FALCON

    def names():
        out = subprocess.run([CLI, folder, "list"], check=True, capture_output=True, text=True).stdout
        return [ln.split(" | ")[0] for ln in out.splitlines()]

    copied = {}
    before = names()
    r = Rig(work, machine, auto="C:\\ECONTACT\\ECONTACT.PRG")

    def held(key, x, y):
        r.move(x, y)
        r.cmd("hatari-event", "keydown", str(K[key]))
        time.sleep(0.2)
        r.click()
        r.cmd("hatari-event", "keyup", str(K[key]))
        time.sleep(2)

    def copy(name):
        if os.path.exists(scrap):
            os.remove(scrap)
        r.ctrl("c")
        time.sleep(2)
        copied[name] = open(scrap, encoding="latin-1").read() if os.path.exists(scrap) else ""

    try:
        time.sleep(25)
        r.click(X, ROW["Grace"])
        time.sleep(3)
        r.shot("1-one")
        copy("one")
        held("CTRL", X, ROW["Olivia"])         # Grace to Olivia
        time.sleep(3)
        r.shot("2-range")
        copy("range")
        r.rclick(X, ROW["Maya"])               # the menu for the four
        time.sleep(2)
        r.shot("2-menu")
        r.key(K["ESC"])
        time.sleep(2)
        copy("after-menu")
        held("SHIFT", X, ROW["Maya"])          # one fewer
        time.sleep(2)
        copy("fewer")
        held("SHIFT", X, ROW["Johnny"])        # one more
        time.sleep(3)
        r.shot("3-more")
        copy("more")
        r.click(X, ROW["Sofia"])               # a click: one again
        time.sleep(3)
        r.shot("4-click")
        copy("click")
        r.ctrl("a")                            # everyone shown
        time.sleep(4)
        r.shot("5-all")
        copy("all")
        r.click(X, ROW["Grace"])
        time.sleep(2)
        held("CTRL", X, ROW["Maya"])           # Grace and Maya
        time.sleep(2)
        r.key(K["DELETE"])
        time.sleep(3)
        r.shot("6-delete-asked")
        r.click(*delete_at)
        time.sleep(6)
        r.shot("7-deleted")
    finally:
        r.stop()
    shots = os.path.join(work, "shots")
    shot = lambda n: os.path.join(shots, n + ".png")  # noqa: E731
    who = lambda k: [p.split(" <")[0] for p in copied.get(k, "").split(", ") if p]  # noqa: E731
    print("   copied:", {k: who(k) for k in copied})
    check("a click: Grace alone", who("one") == ["Grace Baker"])
    check("Control+click: Grace to Olivia", who("range") == ["Grace Baker", "Maya Carter", "Luc Dubois", "Olivia Evans"])
    check("Shift+click on one of them: one fewer", who("fewer") == ["Grace Baker", "Luc Dubois", "Olivia Evans"])
    check("Shift+click on another: one more",
          who("more") == ["Johnny Appleseed", "Grace Baker", "Luc Dubois", "Olivia Evans"])
    check("as EMail's To: line takes them", "Grace Baker <" in copied.get("range", ""))
    check("a right-click on one of them: their menu", same(shot("2-menu"), shot("2-range")) < 0.99)
    check("and the four still chosen after it", who("after-menu") == who("range"))
    check("a click: one again", who("click") == ["Sofia Garcia"])
    check("^A: all 21", len(who("all")) == 21)
    check("the card pane says how many (unlike one card's)",
          same(shot("2-range"), shot("1-one"), (330, 80, 620, 380)) < 0.97)
    check("the Delete alert came", same(shot("6-delete-asked"), shot("5-all")) < 0.99)
    after = names()
    check("Grace and Maya deleted", "Grace Baker" not in after and "Maya Carter" not in after)
    check("the others kept", len(after) == len(before) - 2)
    print("screenshots in", shots)
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
