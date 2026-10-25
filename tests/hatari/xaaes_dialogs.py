#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EContact's dialogs under FreeMiNT and XaAES (the guidelines 3.5: where
the AES runs several programs, the alerts are the AES's and a dialog is a
window of the AES's):

    tests/hatari/xaaes_dialogs.py WORK

ECONTACT.PRG (binary/) with the sample book, in common/tests/xaaes (make
base there first), ECal's latest release's accessory beside it:

- Settings opens in a window: the screen changes where it is;
- moved by its title bar, the window behind is drawn whole where the
  dialog was (no ghost), and its pop-ups and sections answer where it is
  now;
- its closer and Esc close it, and the window is as it was;
- Delete's alert is the AES's, and Return (Cancel) leaves all as it was;
- New Group's box and Add to Group's list open in a window, take keys and
  close with Esc, the window as it was;
- ECal's first alert put over Settings and answered: the dialog is whole
  (last: XaAES splits the window's redraw at the dialog's edge, and a
  bold letter cut there loses its widened column, which no later check
  should trip on).
Screenshots in WORK/run/shots.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
XA = os.path.join(ROOT, "common", "tests", "xaaes")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import docs_shots  # noqa: E402
import machines  # noqa: E402

fails = 0
PARK = "move 4 470;wait 1"	# the pointer on the desktop, left of the window


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok
    return ok


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    work = os.path.abspath(sys.argv[1])
    if not os.path.isdir(os.path.join(XA, "build", "base")):
        sys.exit("no %s: make -C common/tests/xaaes base" % os.path.join(XA, "build", "base"))
    shutil.rmtree(work, ignore_errors=True)
    files = os.path.join(work, "files")
    os.makedirs(files)
    docs_shots.seed(files)
    inf = os.path.join(files, "ECONTACT.INF")
    with open(inf, newline="") as f:
        t = f.read()
    with open(inf, "w", newline="") as f:
        f.write("\r\n".join("FOLDER=C:\\" if ln.startswith("FOLDER=") else ln for ln in t.split("\r\n")))
    shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), os.path.join(files, "ECONTACT.PRG"))
    subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "getrelease.py"), "ECal", files,
                    "ECAL.ACC"], check=True)
    steps = [
        "click 200 200", "wait 2", PARK, "shot",			# 1 the window
        "ctrl 0x33", "wait 5", PARK, "shot",				# 2 Settings
        "drag 300 86 300 150", "wait 4", PARK, "shot",			# 3 moved
        "click 384 224", "wait 3", PARK, "shot", "key 0x01", "wait 2",	# 4 Sort by's pop-up
        "click 110 224", "wait 3", PARK, "shot",			# 5 Refresh's section
        "click 82 150", "wait 4", PARK, "shot",				# 6 the closer
        "ctrl 0x33", "wait 5", "key 0x01", "wait 4", PARK, "shot",	# 7 Esc
        "key 0x53", "wait 4", PARK, "shot",				# 8 Delete's alert
        "key 0x1c", "wait 3", PARK, "shot",				# 9 Return: Cancel
        "ctrl g", "wait 4", "key a", "key b", "wait 1", PARK, "shot",	# 10 New Group
        "key 0x01", "wait 3", PARK, "shot",				# 11 Esc
        "rclick 200 200", "wait 2", "click 290 264", "wait 4", PARK, "shot",	# 12 Add to Group
        "key 0x50", "wait 1", PARK, "shot",				# 13 the next one
        "key 0x01", "wait 3", PARK, "shot",				# 14 Esc
        "ctrl 0x33", "wait 5", "drag 300 86 300 150", "wait 4", PARK, "shot",	# 15 Settings, moved
        "move 50 8", "wait 3", "move 60 59", "wait 3", "click 50 91",	# the desk menu's Clients: ECal
        "wait 8", PARK, "shot",						# 16 ECal's alert over it
        "click 431 281", "wait 6", PARK, "shot",			# 17 its Cancel: ECal gone
        "click 300 150", "wait 2", "key 0x01", "wait 3",		# Settings to the front, closed
    ]
    run = os.path.join(work, "run")
    cmd = [sys.executable, os.path.join(XA, "run.py"), run, machines.emutos(), "ECONTACT.PRG",
           "--copy"] + [os.path.join(files, n) for n in ("ECONTACT.INF", "ECONTACT.KEY", "CACHE",
                                                          "CONTACTS.TXT", "ECAL.ACC")] + \
          ["--wait", "70", "--steps", ";".join(steps)]
    old = os.getcwd()
    os.chdir(files)
    r = subprocess.run(cmd)
    os.chdir(old)
    check("the run's steps", r.returncode == 0)
    shots = os.path.join(run, "shots")
    names = sorted(n for n in os.listdir(shots) if n.endswith(".png")) if os.path.isdir(shots) else []
    if not check("17 screenshots", len(names) == 17):
        return
    from PIL import Image, ImageChops
    im = [None] + [Image.open(os.path.join(shots, n)).convert("RGB") for n in names]

    def same(a, b, box=None):
        """the same, but for a column here and there: a bold letter cut by
        the edge of a piece the AES asked for loses its widened column (at
        a pop-up's or a dialog's old edge); a ghost of a dialog is an area"""
        x, y = im[a], im[b]
        if box:
            x, y = x.crop(box), y.crop(box)
        d = ImageChops.difference(x, y).convert("L").point(lambda v: 255 if v else 0)
        if d.getbbox() is None:
            return True
        cols = [c for c in range(d.width) if d.crop((c, 0, c + 1, d.height)).getbbox()]
        return len(cols) <= 4 and all(c + 1 not in cols for c in cols)

    dlg = (90, 160, 560, 440)		# Settings, moved down 64
    check("Settings opens over the window", not same(1, 2))
    check("Settings moved: the window drawn again where it was", same(1, 3, (100, 76, 560, 134)))
    check("Settings moved: Sort by's pop-up at its place", not same(3, 4, (316, 230, 456, 280)))
    check("Settings moved: the Refresh section chosen", not same(3, 5, dlg))
    check("its closer closes it, the window as it was", same(1, 6))
    check("Esc closes it", same(1, 7))
    check("Delete's alert (the AES's)", not same(1, 8))
    check("Return: Cancel, all as it was", same(1, 9))
    check("New Group in a window, typed into", not same(1, 10))
    check("Esc closes New Group", same(1, 11))
    check("Add to Group's list", not same(1, 12))
    check("Down moves in it", not same(12, 13))
    check("Esc closes it", same(1, 14))
    check("ECal over Settings (its first alert: where its folder is)", not same(15, 16))
    check("ECal gone: Settings whole again", same(15, 17, (72, 162, 566, 466)))

main()
print("%s: %s" % (os.path.basename(__file__), "all passed" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
