#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's quiet start (the guidelines, 2.2), in Hatari on an ST
and a Falcon (16 MHz; falcon32: at 32, as common/tests/bgwork), with an edition built to time the stretches between its
event calls:

    make OBJDIR=WORK/build BIN=WORK/bin EXTRA_CFLAGS=-DAES_PROFILE \\
        WORK/bin/ECONTACT.ACC WORK/bin/ECONTST.ACC
    tests/hatari/quiet_start.py WORK etos512us.img [st|falcon|falcon32] [contacts]

600 contacts (by default) and START=5: the accessory reads them in the
background a few seconds after boot, its window closed, then is opened.
Twice: the first time every card is parsed (card-cli's index is the
host's), the second they come from the index. Each stretch of 50 ms or
more goes to Hatari's console as "AESGAP <ms> ms <tag>" (common/atari/
gem.c); at boot, in the background and when idle none may be longer
than 50 ms on the Falcon or 100 ms on the ST. The first opening and its
drawing, which the user asked for, are reported (tags open-* and redraw).
"""
import os
import random
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


def cards(path, n):
    random.seed(7)
    first = "Avi Dana Erez Mike Shira Noa Yael Omer Tal Gil Ron Maya Lior Eitan Dalit Orit".split()
    last = "Cohen Levi Mizrahi Peretz Biton Friedman Katz Azulay Gold Hart Lavi Deck Galil".split()
    with open(path, "w", newline="") as f:
        for i in range(n):
            g, fam = random.choice(first), random.choice(last) + str(i)
            f.write("BEGIN:VCARD\r\nVERSION:3.0\r\nPRODID:-//Apple Inc.//iPhone OS 18.0//EN\r\n"
                    "N:%s;%s;;;\r\nFN:%s %s\r\nORG:Company %d;\r\nTITLE:Manager\r\n" % (fam, g, g, fam, i % 50)
                    + "EMAIL;type=INTERNET;type=HOME;type=pref:%s.%s@example.com\r\n" % (g.lower(), fam.lower())
                    + "EMAIL;type=INTERNET;type=WORK:%s@company%d.example\r\n" % (g.lower(), i % 50)
                    + "TEL;type=CELL;type=VOICE;type=pref:+972 54-555-%04d\r\n" % i
                    + "item1.ADR;type=HOME;type=pref:;;%d Herzl St;Tel Aviv;;6100000;Israel\r\n" % (i + 1)
                    + "BDAY:1970-%02d-%02d\r\nNOTE:Met at the meeting %d\\, call back later.\r\n"
                    % (i % 12 + 1, i % 28 + 1, i)
                    + "UID:QUIET-%05d\r\nEND:VCARD\r\n" % i)


def main():
    work, tos = os.path.abspath(sys.argv[1]), sys.argv[2]
    machines = [sys.argv[3]] if len(sys.argv) > 3 else ["st", "falcon", "falcon32"]
    n = int(sys.argv[4]) if len(sys.argv) > 4 else 600
    for machine in machines:
        limit = 100 if machine == "st" else 50
        d = os.path.join(work, machine)
        shutil.rmtree(d, ignore_errors=True)
        hd = os.path.join(d, "hd")
        folder = os.path.join(hd, "ECONTACT")
        os.makedirs(folder)
        cards(os.path.join(d, "p.vcf"), n)
        subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([CLI, folder, "import", "Contacts", os.path.join(d, "p.vcf")], check=True,
                       stdout=subprocess.DEVNULL)
        inf = open(os.path.join(folder, "ECONTACT.INF"), "rb").read()
        inf = inf.replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
        inf = re.sub(rb"\nSTART=[^\r\n]*", rb"\nSTART=5", inf)
        inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
        os.remove(os.path.join(folder, "ECONTACT.INF"))
        open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
        acc = "ECONTST.ACC" if machine == "st" else "ECONTACT.ACC"
        shutil.copy(os.path.join(work, "bin", acc), os.path.join(hd, "ECONTACT.ACC"))
        log = os.path.join(folder, "ECONTACT.LOG")
        extra = ["--memsize", "4", "--monitor", "mono"] if machine == "st" else ["--memsize", "14"]
        if machine == "falcon32":
            extra += ["--cpuclock", "32"]
        for run in (1, 2):
            r = Rig(d, tos, machine="megast" if machine == "st" else "falcon", extra=extra, auto="")
            try:
                t0 = time.time()
                while time.time() - t0 < 900:
                    time.sleep(2)
                    t = open(log, encoding="latin-1").read() if os.path.exists(log) else ""
                    if t.count("the list sorted") >= run:
                        break
                loaded = time.time() - t0
                time.sleep(3)
                r.click(30, 8)		# Desk
                time.sleep(1.5)
                r.click(60, 59)		# EContact
                time.sleep(10)
                r.shot("%s-%d-open" % (machine, run))
            finally:
                r.stop()
            con = open(os.path.join(d, "hatari.log"), encoding="latin-1").read()
            os.rename(os.path.join(d, "hatari.log"), os.path.join(d, "hatari-%d.log" % run))
            gaps = {}
            for m in re.finditer(r"AESGAP (\d+) ms (\S+)", con):
                gaps.setdefault(m.group(2), []).append(int(m.group(1)))
            lines = [l for l in open(log, encoding="latin-1").read().splitlines() if "Contacts --" in l]
            print("%s, run %d: the books read %.0f s after boot (the emulator's time)" % (machine, run, loaded))
            for l in lines[-3:]:
                print("   " + l)
            for tag, g in sorted(gaps.items()):
                print("   %-12s %3d over 50 ms, %3d over %d, the longest %d ms"
                      % (tag, len(g), len([x for x in g if x > limit]), limit, max(g)))
            # the opening and its drawing the user asked for: reported, not limited
            quiet = [g for t, v in gaps.items() if not t.startswith("open") and t != "redraw" for g in v]
            check("%s, run %d: no stretch over %d ms at boot, in the background or idle (%s)"
                  % (machine, run, limit, "the longest %d ms" % max(quiet) if quiet else "none reached 50 ms"),
                  max(quiet or [0]) <= limit)
            check("%s, run %d: read before the opening" % (machine, run), loaded < 890)
        # opened while it reads (the index gone: every card parsed): what is
        # left is read at once, and all of them are there
        for f in os.listdir(os.path.join(folder, "CACHE")):
            if f.endswith(".IDX"):
                os.remove(os.path.join(folder, "CACHE", f))
        r = Rig(d, tos, machine="megast" if machine == "st" else "falcon", extra=extra, auto="")
        try:
            time.sleep(14)
            r.click(30, 8)
            time.sleep(1.5)
            r.click(60, 59)
            t0 = time.time()
            while time.time() - t0 < 600:
                time.sleep(2)
                if open(log, encoding="latin-1").read().count("the list sorted") >= 3:
                    break
            time.sleep(4)
            r.shot("%s-3-opened-while-reading" % machine)
        finally:
            r.stop()
        t = open(log, encoding="latin-1").read()
        last = [l for l in t.splitlines() if "cards in" in l][-1:]
        print("%s, run 3 (opened while reading): %s" % (machine, last[0] if last else "?"))
        check("%s, run 3: all %d read when opened while reading" % (machine, n),
              bool(last) and (" %d cards in" % n) in last[0])
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
