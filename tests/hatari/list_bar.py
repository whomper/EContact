#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The list's own scroll bar, in Hatari with ECONTACT.PRG and 80 contacts:

    tests/hatari/list_bar.py WORK [MACHINE] [--acc] [--nvdi]

--acc runs the accessory (ECONTST.ACC on an ST) from the Desk menu,
--nvdi under NVDI (AERA_WITH_NVDI, a Falcon: the window drawn in an EdDI
bitmap). ECONTACT.LOG says how the window is drawn (win.c).

The window has no slider of its own; the bar is at the right of the
middle list (list.c). Its slider is found on the screen, then:
- a click on the down arrow scrolls the list a row, and the slider moves;
- a click in the track under the slider scrolls a page;
- the up arrow then the down arrow leave the list as it was: the rows
  moved on the screen (win_scroll) and those that came in drawn match;
- the track held under the slider goes on a page at a time until the
  slider reaches the pointer (it did one page only: Erez, 22 Oct 2026);
- the down arrow held scrolls on, a row at a time;
- the slider dragged to the bottom shows the last contact, and back to
  the top the first;
- the card on the right stays as it was throughout.
Screenshots in WORK/shots.
"""
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def cards(path, n):
    with open(path, "w", newline="") as f:
        for i in range(n):
            f.write("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Person%02d;Ada;;;\r\nFN:Ada Person%02d\r\n"
                    "EMAIL;type=INTERNET:ada%d@example.com\r\nUID:BAR-%03d\r\nEND:VCARD\r\n" % (i, i, i, i))


def thumb(png):
    """the slider: (x, top, bottom) of the tallest solid black rectangle
    10 to 24 pixels wide, or None"""
    from PIL import Image
    im = Image.open(png).convert("L")
    px = im.load()
    w, h = im.size

    def black(x, y):
        return px[x, y] < 128

    def width(x, y):
        n = 0
        while x + n < w and black(x + n, y):
            n += 1
        return n

    best = None
    for x in range(1, w - 24):
        y = 0
        while y < h:
            wd = width(x, y) if black(x, y) and not black(x - 1, y) else 0
            if 10 <= wd <= 24:
                y0 = y
                while y < h and not black(x - 1, y) and width(x, y) == wd:
                    y += 1
                if y - y0 >= 14 and (best is None or y - y0 > best[2] - best[1]):
                    best = (x, y0, y)
            else:
                y += 1
    return best


def region(png, box):
    from PIL import Image
    return Image.open(png).convert("L").crop(box).tobytes()


def main():
    acc = "--acc" in sys.argv
    if "--nvdi" in sys.argv:
        os.environ["AERA_WITH_NVDI"] = "1"
    args = [a for a in sys.argv if not a.startswith("--")]
    work, machine = os.path.abspath(args[1]), machine_arg(args, 2, "Falcon:16")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    vcf = os.path.join(work, "bar.vcf")
    cards(vcf, 80)
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", vcf], check=True, stdout=subprocess.DEVNULL)
    inf_path = os.path.join(folder, "ECONTACT.INF")
    inf = open(inf_path, "rb").read().replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = inf.replace(b"\nLOG=0", b"\nLOG=1")
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    if acc:     # in the root, FOLDER= naming the folder
        os.remove(inf_path)
        inf_path = os.path.join(hd, "ECONTACT.INF")
        shutil.copy(os.path.join(ROOT, "binary", "ECONTST.ACC" if machine.startswith("ST") else "ECONTACT.ACC"),
                    os.path.join(hd, "ECONTACT.ACC"))
    else:
        shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), folder)
    open(inf_path, "wb").write(inf)
    r = Rig(work, machine, auto="" if acc else "C:\\ECONTACT\\ECONTACT.PRG")
    try:
        if acc:
            time.sleep(15)
            r.click(30, 8)              # Desk
            time.sleep(1.5)
            r.click(60, 59)             # EContact
        time.sleep(25)
        log = os.path.join(folder, "ECONTACT.LOG")
        drawn = [l.strip() for l in open(log, encoding="latin-1") if "the window is drawn" in l] \
            if os.path.exists(log) else []
        print("  (%s)" % (drawn[0] if drawn else "the log doesn't say how the window is drawn"))
        r.move(600, 250)                # an ST's pointer is in its screenshots: off the bar
        time.sleep(2)
        s0 = r.shot("0-start")
        t0 = thumb(s0)
        check("a slider beside the list (%s)" % (t0,), t0 is not None)
        if not t0:
            return
        x, top, bottom = t0
        bx = x + 6                      # inside the bar
        # the card: everything right of the bar
        from PIL import Image
        w, h = Image.open(s0).size
        card = (x + 30, top, w - 30, bottom)
        # the down arrow: under the track's end, found going down from the slider
        im = Image.open(s0).convert("L").load()
        y = bottom
        while y < h - 1 and not (im[bx, y] < 128 and im[bx + 1, y] < 128 and im[bx + 2, y] < 128):
            y += 1                      # the track to the arrow box's top line
        arrow = y + 6
        print("  (the slider at x %d, the down arrow at y %d)" % (x, arrow))
        def away():                     # the pointer off the slider for the shot
            r.move(600, 250)
            time.sleep(1)

        def press(x, y):
            """a click where the pointer has got to: an ST's comes slowly"""
            r.move(x, y)
            time.sleep(1.5)
            r.click()

        def drag(x, y, y2):
            r.move(x, y)
            time.sleep(1.5)
            r.drag(x, y, x, y2, secs=1.5)

        press(bx, arrow)
        away()
        time.sleep(2)
        s1 = r.shot("1-down-arrow")
        t1 = thumb(s1)
        check("the down arrow moved the slider (%s -> %s)" % (top, t1 and t1[1]), t1 and t1[1] > top)
        check("and scrolled the list", region(s0, (x - 200, top, x - 2, bottom)) != region(s1, (x - 200, top, x - 2, bottom)))
        press(bx, t1[2] + 10 if t1 else arrow - 20)     # the track under the slider: a page
        away()
        time.sleep(2)
        s2 = r.shot("2-page")
        t2 = thumb(s2)
        check("the track under it a page (%s -> %s)" % (t1 and t1[1], t2 and t2[1]),
              t1 and t2 and t2[1] - t1[1] > 4 * (t1[1] - top))
        lst = (x - 200, top, x - 2, bottom)
        press(bx, top - 8)              # the up arrow, over the track's top
        press(bx, arrow)
        away()
        time.sleep(2)
        s2b = r.shot("2b-up-down")
        bar = (x - 2, top - 20, x + 26, arrow + 10)    # the bar, its arrows and frame
        check("up then down: the rows as they were", region(s2, lst) == region(s2b, lst))
        check("and the bar", region(s2, bar) == region(s2b, bar))
        # held under the slider, near the track's end: page after page
        hold_y = arrow - 30
        r.move(bx, hold_y)
        time.sleep(1.5)
        r.xdo("mousedown", "1")
        time.sleep(5)
        r.xdo("mouseup", "1")
        away()
        time.sleep(2)
        s2c = r.shot("2c-track-held")
        t2c = thumb(s2c)
        check("the track held: on to the pointer (%s -> %s, the pointer at %d)" % (t2 and t2[1], t2c, hold_y),
              t2 and t2c and t2c[1] <= hold_y < t2c[2] and 2 * (t2c[1] - t2[1]) > 3 * (t2[1] - t1[1]))
        # and back up by the up arrow held
        r.move(bx, top - 8)
        time.sleep(1.5)
        r.xdo("mousedown", "1")
        time.sleep(3)
        r.xdo("mouseup", "1")
        away()
        time.sleep(2)
        s2d = r.shot("2d-arrow-held")
        t2d = thumb(s2d)
        check("the up arrow held: rows on (%s -> %s)" % (t2c and t2c[1], t2d and t2d[1]),
              t2c and t2d and t2c[1] - t2d[1] > 3 * (t1[1] - top))
        t2 = t2d
        drag(bx, (t2[1] + t2[2]) // 2, h - 10)
        away()
        time.sleep(2)
        s3 = r.shot("3-dragged-down")
        t3 = thumb(s3)
        check("dragged to the bottom: the slider at the track's end (%s)" % (t3,), t3 and t3[2] >= arrow - 12)
        drag(bx, (t3[1] + t3[2]) // 2, 5)
        away()
        time.sleep(2)
        s4 = r.shot("4-dragged-up")
        t4 = thumb(s4)
        check("dragged back up: at the top again (%s)" % (t4,), t4 and t4[1] == top)
        check("and the list as it began", region(s0, (x - 200, top, x - 2, bottom)) == region(s4, (x - 200, top, x - 2, bottom)))
        check("the bar too (only the slider's old and new place drawn)", region(s0, bar) == region(s4, bar))
        check("the card on the right as it was", region(s0, card) == region(s4, card) == region(s3, card))
    finally:
        r.stop()
    print("screenshots in", os.path.join(work, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
