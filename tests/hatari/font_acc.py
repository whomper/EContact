#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A GDOS font in the accessory, in Hatari (under NVDI on a Falcon:
AERA_WITH_NVDI=1, or --nvdi, common/tools/machines.py):

    tests/hatari/font_acc.py WORK [MACHINE] [--boot] [--nvdi]

Choose (the default): the accessory opened, its Font... (the toolbar's
menu), the first GDOS font in the list chosen at the size it offers (Down,
Return); the window must redraw in it, and again after it is closed and
opened. With --boot, the 1.51 way: ECONTACT.PRG chooses the font first
(Options > Font..., then Quit), so ECONTACT.INF names it at the next
boot; then the accessory must draw in it, and again after it is closed
and opened.

Which GDOS fonts there are comes from ECONTACT.FNT, which EContact
writes when it has looked at them: none (no GDOS, or an accessory that
loaded none) skips the rest and passes. Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, machine_arg  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0

# where things are on a Falcon's 640 x 480 (acc_files.py, twin_test.py):
# the Desk menu's EContact, the accessory's toolbar menu and its Font...
# line, the program's Options title and its Font... line
DESK, ACC_ITEM = (30, 8), (60, 61)
MENU, ACC_FONT = (598, 73), (520, 201 + 11 * 16)
ACC_FONT_ST = (520, 171 + 11 * 16)  # the menu 30 lines higher on an ST's 400
OPTIONS, PRG_FONT = (276, 8), (300, 19 + 16 + 8)


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def same(a, b):
    """the share of pixels two screenshots have alike"""
    from PIL import Image
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    pa, pb = ia.load(), ib.load()
    w, h = ia.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if pa[x, y] == pb[x, y])
    return n / ((w // 2) * (h // 2))


def gdos_fonts(folder):
    """the fixed-width GDOS fonts ECONTACT.FNT names: [(name, id)]"""
    path = os.path.join(folder, "ECONTACT.FNT")
    if not os.path.exists(path):
        return []
    out = []
    for line in open(path, encoding="latin-1").read().splitlines():
        m = re.match(r"1 ([^\t]+)\t(\d+)", line)  # FNT2 adds family, style, format
        if m and int(m.group(2)) > 1:
            out.append((m.group(1), int(m.group(2))))
    return out


def inf_font(hd):
    """FONT_ID= in ECONTACT.INF (the root's)"""
    t = open(os.path.join(hd, "ECONTACT.INF"), encoding="latin-1").read()
    m = re.search(r"^FONT_ID=(\d+)", t, re.M)
    return int(m.group(1)) if m else 1


def wait_file(path, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        if os.path.exists(path):
            time.sleep(2)		# (written whole)
            return True
        time.sleep(2)
    return False


def main():
    boot = "--boot" in sys.argv
    if "--nvdi" in sys.argv:
        os.environ["AERA_WITH_NVDI"] = "1"
    args = [a for a in sys.argv if not a.startswith("--")]
    work, machine = os.path.abspath(args[1]), machine_arg(args, 2, "Falcon:16")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(folder, "ECONTACT.INF"), "rb") as f:
        inf = f.read()
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    # in the root, FOLDER= naming the folder; a log, no refresh at
    # opening, the system font
    inf = inf.replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    inf = re.sub(rb"\nFONT_ID=[^\r\n]*", rb"\nFONT_ID=1", inf)
    with open(os.path.join(hd, "ECONTACT.INF"), "wb") as f:
        f.write(inf)
    acc = "ECONTACT.ACC"
    fnt = os.path.join(folder, "ECONTACT.FNT")

    def start(auto):
        r = Rig(work, machine, auto=auto)
        print("  under NVDI" if r.nvdi else "  without NVDI")
        time.sleep(18)		# (rig.py waits for NVDI's fonts first)
        return r

    def open_acc(r, name):
        r.click(*DESK)
        time.sleep(1.5)
        r.click(*ACC_ITEM)
        time.sleep(8)
        return r.shot(name)

    def close_acc(r, name):
        r.ctrl("w")
        time.sleep(3)
        return r.shot(name)

    def skip(why):
        print("  skipped: %s" % why)
        print("all passed")
        sys.exit(0)

    if boot:
        # the program chooses the font, as Erez did, and quits
        shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), os.path.join(folder, "ECONTACT.PRG"))
        r = start("C:\\ECONTACT\\ECONTACT.PRG")
        try:
            r.shot("1-program")
            r.click(*OPTIONS)
            time.sleep(1)
            r.click(*PRG_FONT)
            wait_file(fnt, 120)		# it looks at every font first
            time.sleep(3)
            r.shot("2-program-font")
            fonts = gdos_fonts(folder)
            if fonts:
                r.key(K["DOWN"])	# the first GDOS font, at the size it offers
                time.sleep(3)
                r.shot("3-program-chosen")
                r.key(K["RETURN"])
                time.sleep(4)
            else:
                r.key(K["ESC"])
                time.sleep(2)
            r.ctrl("q")
            time.sleep(8)
        finally:
            r.stop()
        if not fonts:
            skip("no fixed-width GDOS font (ECONTACT.FNT names none)")
        chosen = inf_font(hd)
        names = dict((i, n) for n, i in fonts)
        check("the program wrote FONT_ID=%d (%s)" % (chosen, names.get(chosen, "not listed")), chosen in names)
        os.remove(os.path.join(folder, "ECONTACT.PRG"))
        shutil.copy(os.path.join(ROOT, "binary", acc), os.path.join(hd, "ECONTACT.ACC"))
        r = start("")
        try:
            desk = r.shot("4-desktop")
            first = open_acc(r, "5-accessory")
            close_acc(r, "6-closed")
            again = open_acc(r, "7-again")
            close_acc(r, "8-closed")
        finally:
            r.stop()
        check("the accessory drew its window", same(desk, first) < 0.9)
        check("drawn again the same after closing and opening (%.3f alike)" % same(first, again),
              same(first, again) > 0.97)
        check("FONT_ID kept", inf_font(hd) == chosen)
    else:
        shutil.copy(os.path.join(ROOT, "binary", acc), os.path.join(hd, "ECONTACT.ACC"))
        r = start("")
        try:
            desk = r.shot("1-desktop")
            first = open_acc(r, "2-system-font")
            r.click(*MENU)
            time.sleep(2)
            r.shot("3-menu")
            r.click(*(ACC_FONT_ST if machine.startswith("ST") else ACC_FONT))
            wait_file(fnt, 120)		# with the fonts loaded it looks at each first
            time.sleep(3)
            r.shot("4-font-dialog")
            fonts = gdos_fonts(folder)
            if fonts:
                r.key(K["DOWN"])	# the first GDOS font, at the size it offers
                time.sleep(3)
                r.shot("5-chosen")
                r.key(K["RETURN"])
                time.sleep(5)
                chosen = r.shot("6-in-the-font")
                close_acc(r, "7-closed")
                again = open_acc(r, "8-again")
                close_acc(r, "9-closed")
            else:
                r.key(K["ESC"])
                time.sleep(2)
                close_acc(r, "5-closed")
        finally:
            r.stop()
        if not fonts:
            skip("no fixed-width GDOS font in the accessory's Font... (ECONTACT.FNT names none)")
        got = inf_font(hd)
        names = dict((i, n) for n, i in fonts)
        check("the window redrawn in %s (%.3f alike the system font's)" % (names.get(got, got), same(first, chosen)),
              same(first, chosen) < 0.97 and same(desk, chosen) < 0.9)
        check("drawn again the same after closing and opening (%.3f alike)" % same(chosen, again),
              same(chosen, again) > 0.97)
        check("FONT_ID=%d written, a font the dialog listed" % got, got in names)
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
