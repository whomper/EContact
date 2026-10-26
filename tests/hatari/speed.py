#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""How long the user waits (the guidelines, "Speed: the stock machines set
it"), in Hatari with an edition that times the stretches between its
event calls:

    make OBJDIR=WORK/build BIN=WORK/bin EXTRA_CFLAGS=-DAES_PROFILE=20 \\
        WORK/bin/ECONTACT.PRG WORK/bin/ECONTACT.ACC
    tests/hatari/speed.py WORK [MACHINE...] [--nvdi] [--bin DIR] [--only prg|acc]

On each machine (ST:mono, ST:med and Falcon-4:16 by default; --nvdi runs
the Falcons under NVDI) with the sample people and their photos and 600
more: the program started twice (the first time makes the index; the
second is timed), then in its window
- Clr/Home: the first contact, with a photo, its card drawn;
- Down: the next row;
- the list's down arrow: a line; the track under the slider: a page;
- a letter typed in the search field, a second, and both taken out;
- ^G: New Group's dialog opened, a letter typed in its field, Esc;
- ^,: Settings opened, Esc: the window uncovered;
- Delete: its alert, Return (Cancel): uncovered;
and the accessory opened from the Desk menu, its books read at boot.
Each action's longest stretch ("AESGAP" lines of 20 ms and more, the
"AESDRAWN" times of the start and the opening: ui/main.c) is set against
the guidelines' time for it. The table goes to WORK/speed.txt and
WORK/speed.json, the screenshots to WORK/<machine>/shots.
--profile ELF (the build's contact.elf) has Hatari's profiler say where
the program's time went in each of its actions: the functions that took
the most cycles, in WORK/<machine>/profile.txt.
"""
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig import Rig, K  # noqa: E402
import list_bar  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
SAMPLE = os.path.join(ROOT, "tests", "hatari", "sample.vcf")

# the guidelines' times (ms)
T_KEY, T_LINE, T_PAGE, T_DIALOG, T_WINDOW, T_ACC, T_START = 50, 50, 150, 150, 300, 500, 2000


def cards(path, n):
    """n made-up people, as an iPhone writes them (quiet_start.py's)"""
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
                    + ("CATEGORIES:%s\r\n" % ",".join(c for c, k in (("Friends", 4), ("Work", 7)) if not i % k)
                       if not i % 4 or not i % 7 else "")
                    + "UID:SPEED-%05d\r\nEND:VCARD\r\n" % i)


def seed(folder, work):
    os.makedirs(folder, exist_ok=True)
    vcf = os.path.join(work, "people.vcf")
    cards(vcf, 600)
    for a in (("local", "Contacts"), ("import", "Contacts", SAMPLE), ("import", "Contacts", vcf)):
        subprocess.run([CLI, folder] + list(a), check=True, stdout=subprocess.DEVNULL)
    inf = os.path.join(folder, "ECONTACT.INF")
    t = open(inf, "rb").read().replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    t = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", t)
    t = re.sub(rb"\nSTART=[^\r\n]*", rb"\nSTART=5", t)
    os.remove(inf)
    return t


class Console:
    """Hatari's console (hatari.log): the AESGAP and AESDRAWN lines since a mark"""
    def __init__(self, path):
        self.path, self.at = path, 0

    def text(self):
        try:
            return open(self.path, encoding="latin-1").read()
        except OSError:
            return ""

    def mark(self):
        self.at = len(self.text())

    def since(self):
        t = self.text()[self.at:]
        gaps = [(int(m.group(1)), m.group(2)) for m in re.finditer(r"AESGAP (\d+) ms (\S+)", t)]
        drawn = [(int(m.group(1)), m.group(2)) for m in re.finditer(r"AESDRAWN (\d+) ms (\S+)", t)]
        return gaps, drawn


class Run:
    def __init__(self, machine, label):
        self.machine, self.label, self.rows = machine, label, []

    def add(self, action, limit, gaps, drawn=None, note=""):
        longest = max([g for g, _ in gaps] or [0])
        tags = sorted(set(t for g, t in gaps if g >= 20))
        value = drawn if drawn is not None else longest
        self.rows.append(dict(action=action, limit=limit, ms=value, longest=longest, tags=tags,
                              note=note, over=value > limit))
        print("  %-40s %6s ms  (limit %d)%s%s" % (action, value if value else "<20", limit,
                                                  "  OVER" if value > limit else "",
                                                  "  [%s]" % " ".join(tags) if tags else ""))


def settle(r, secs=1.5):
    r.wait(secs, limit=60)


def screen_rows(r):
    """the pointer off the window's working parts, for the shot"""
    from PIL import Image
    s = r.shot("_where")
    w, h = Image.open(s).size
    os.remove(s)
    return w, h


PROFILE = None


def profile_symbols(d):
    """the program's functions, for Hatari's profiler: nm's list, its
    addresses from the program's TEXT (linked at 0, data after it)"""
    path = os.path.join(d, "symbols.txt")
    out = subprocess.run(["m68k-linux-gnu-nm", "-n", PROFILE], capture_output=True, text=True).stdout
    with open(path, "w") as f:
        f.write("".join(l + "\n" for l in out.splitlines() if re.match(r"[0-9a-f]+ [Tt] ", l)))


NPROF = [0]


def profiled(r, con, d, name, fn):
    """fn() under the profiler, which starts again here; Hatari stops at
    the end of the turn (prof_turn_end, ui/main.c and field.c), and its
    debugger, reading the rig's pipe, saves what the profiler found. The
    start: from the boot to the first turn's end (start_symbols)"""
    NPROF[0] += 1
    r.cmd("hatari-debug", "profile", "on")
    profile_stop(r, d)
    if name == "start":
        return
    con.mark()
    fn()


def start_symbols(r, d, folder):
    """the start profiled from the boot: the symbols at the addresses
    the first run's log gives (set_logging's), the same at each boot"""
    log = open(os.path.join(folder, "ECONTACT.LOG"), encoding="latin-1").read()
    at = int(re.findall(r"\(program\): program at ([0-9a-f]+)", log)[-1], 16)
    rel = open(os.path.join(d, "symbols.txt")).read().splitlines()
    base = at - next(int(l.split()[0], 16) for l in rel if l.endswith(" set_logging"))
    path = os.path.join(d, "symbols-abs.txt")
    with open(path, "w") as f:
        for l in rel:
            a, k, n = l.split()
            f.write("%08x %s %s\n" % (int(a, 16) + base, k, n))
    r.cmd("hatari-debug", "symbols", path)
    r.symbols = True
    r.abs_symbols = path


def profile_stop(r, d):
    if not getattr(r, "symbols", False):
        r.cmd("hatari-debug", "symbols", os.path.join(d, "symbols.txt"), "TEXT")
        r.symbols = True
    r.cmd("hatari-debug", "b", "pc", "=", "prof_turn_end", ":once")
    r.proc.stdin.write(b"profile save %s\nc\n" % os.path.join(d, "prof-%02d.txt" % NPROF[0]).encode())
    r.proc.stdin.flush()
    time.sleep(0.3)


def profile_report(r, con, d, name):
    """hatari_profile's totals: the functions, the costs of what they
    call put on them too (the TOS's own, in ROM, are counted apart)"""
    time.sleep(1)
    raw = os.path.join(d, "prof-%02d.txt" % NPROF[0])
    syms = ["-a", r.abs_symbols] if getattr(r, "abs_symbols", None) else ["-r", os.path.join(d, "symbols.txt")]
    out = subprocess.run(["hatari_profile"] + syms + ["-s", "-t", "-p",
                          "-f", "25", raw], capture_output=True, text=True)
    with open(os.path.join(d, "profile.txt"), "a") as f:
        f.write("==== %s\n%s\n" % (name, out.stdout or out.stderr))


def prg_actions(r, con, run, d):
    w, h = screen_rows(r)
    ch = 8 if h <= 200 else 16

    def act(name, limit, fn, secs=1.5, drawn=None):
        r.move(w - 20, h - 4)
        time.sleep(0.5)
        if PROFILE:
            profiled(r, con, d, name, fn)
        else:
            con.mark()
            fn()
        settle(r, secs)
        if PROFILE:
            profile_report(r, con, d, name)
        gaps, dr = con.since()
        run.add(name, limit, gaps, max([g for g, t in dr if t == drawn] or [0]) if drawn else None)
        r.shot(re.sub(r"\W+", "-", name).strip("-")[:40])

    act("Clr/Home: a card with its photo", T_KEY, lambda: r.key(K["HOME"]), secs=4)
    act("Down: the next row", T_KEY, lambda: r.key(K["DOWN"]))
    s0 = r.shot("_bar")
    t0 = list_bar.thumb(s0)
    if t0:
        from PIL import Image
        im = Image.open(s0).convert("L").load()
        x0, x1 = list_bar.bar_x(im, w, h)
        xs = range(x0 + 3, x1 - 3)
        y = h - 1
        while y > t0[2] and not (0 < sum(1 for i in xs if im[i, y] < 128) < len(xs) - 2):
            y -= 1
        bx, arrow = t0[0] + 6, y - (3 if ch == 16 else 2)

        def press(x, y):
            r.move(x, y)
            time.sleep(1.5)
            con.mark()
            r.click()
        for name, limit, y in (("the list's down arrow: a line", T_LINE, arrow),
                               ("the track under the slider: a page", T_PAGE, None)):
            r.move(w - 20, h - 4)
            time.sleep(0.5)
            if y is None:
                t = list_bar.thumb(r.shot("_bar2"))
                y = (t[2] if t else t0[2]) + 6
            press(bx, y)
            settle(r)
            gaps, _ = con.since()
            run.add(name, limit, gaps)
            r.shot(name.split(":")[0].replace(" ", "-"))
    else:
        print("  (no list bar found: the scroll actions not done)")
    act("a letter typed in the search field", T_KEY, lambda: r.type("c"), secs=2)
    act("a second letter", T_KEY, lambda: r.type("o"), secs=2)
    act("both taken out (Backspace twice)", T_KEY,
        lambda: (r.key(K["BACKSPACE"]), r.key(K["BACKSPACE"])), secs=2)
    act("^G: New Group's dialog", T_DIALOG, lambda: r.ctrl("g"))
    act("a letter typed in its field", T_KEY, lambda: r.type("a"))
    act("Esc: closed, the window uncovered", T_WINDOW, lambda: r.key(K["ESC"]))
    act("^,: Settings opened", T_DIALOG, lambda: r.key(0x33, ctrl=True), secs=2)
    act("Esc: Settings closed, the window uncovered", T_WINDOW, lambda: r.key(K["ESC"]), secs=2)
    act("Delete: its alert", T_DIALOG, lambda: r.key(K["DELETE"]))
    act("Return (Cancel): uncovered", T_WINDOW, lambda: r.key(K["RETURN"]))


def boot_wait(con, what, limit=600):
    t0 = time.time()
    while time.time() - t0 < limit:
        time.sleep(2)
        if re.search(r"AESDRAWN \d+ ms %s" % what, con.text()):
            return True
    return False


def machine_run(work, machine, bin_dir, only):
    d = os.path.join(work, machine.replace(":", "-") + ("-nvdi" if os.environ.get("AERA_WITH_NVDI") else ""))
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    folder = os.path.join(hd, "ECONTACT")
    inf = seed(folder, d)
    open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
    run = Run(machine, os.path.basename(d))
    if PROFILE:
        profile_symbols(d)
    con = Console(os.path.join(d, "hatari.log"))
    if only in (None, "prg"):
        shutil.copy(os.path.join(bin_dir, "ECONTACT.PRG"), os.path.join(folder, "ECONTACT.PRG"))
        for n in (1, 2):
            r = Rig(d, machine, auto="C:\\ECONTACT\\ECONTACT.PRG",
                    stdin=subprocess.PIPE if PROFILE else None)
            try:
                if PROFILE and n == 2:		# the start, to its first turn's end
                    start_symbols(r, d, folder)
                    profiled(r, con, d, "start", lambda: None)
                ok = boot_wait(con, "start")
                settle(r, 3)
                gaps, drawn = con.since()
                start = max([g for g, t in drawn if t == "start"] or [0])
                if n == 1:
                    print("  (the first start, the index made: drawn after %d ms)" % start)
                else:
                    if PROFILE:
                        profile_report(r, con, d, "the start")
                    run.add("the program started: its window drawn", T_START, [], start,
                            note="" if ok else "never drawn")
                    after = [(g, t) for g, t in gaps]
                    run.add("  its longest stretch till then", T_START, after)
                    prg_actions(r, con, run, d)
                if n == 1:
                    time.sleep(40)		# CONTACTS.TXT written, in steps
                    r.key(0x10, ctrl=True)		# ^Q
                    time.sleep(3)
            finally:
                r.stop()
            os.rename(os.path.join(d, "hatari.log"), os.path.join(d, "hatari-prg%d.log" % n))
            con.at = 0
        os.remove(os.path.join(folder, "ECONTACT.PRG"))
    if only in (None, "acc"):
        shutil.copy(os.path.join(bin_dir, "ECONTACT.ACC"), os.path.join(hd, "ECONTACT.ACC"))
        log = os.path.join(folder, "ECONTACT.LOG")	# (before NVDI's boot, in which it may read them)
        n0 = open(log, encoding="latin-1").read().count("the list sorted") if os.path.exists(log) else 0
        r = Rig(d, machine, auto="")
        try:
            t0 = time.time()
            while time.time() - t0 < 600:
                time.sleep(2)
                if os.path.exists(log) and open(log, encoding="latin-1").read().count("the list sorted") > n0:
                    break
            time.sleep(3)
            w, h = screen_rows(r)
            ch = 8 if h <= 200 else 16
            bar = 19 if ch == 16 else 10
            r.click(30, bar // 2)		# Desk
            time.sleep(1.5)
            con.mark()
            r.click(60, bar + ch * 5 // 2)	# EContact, after Desktop info and the line
            settle(r, 3)
            gaps, drawn = con.since()
            run.add("the accessory opened: its window drawn", T_ACC, gaps,
                    max([g for g, t in drawn if t == "acc-open"] or [0]))
            run.add("  its longest stretch", T_ACC, gaps)
            r.shot("acc-open")
        finally:
            r.stop()
        os.rename(os.path.join(d, "hatari.log"), os.path.join(d, "hatari-acc.log"))
    return run


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    if "--nvdi" in opts:
        os.environ["AERA_WITH_NVDI"] = "1"
    work = os.path.abspath(args[0])
    bin_dir = os.path.join(work, "bin")
    only = None
    for i, a in enumerate(opts):
        if a == "--bin":
            bin_dir = os.path.abspath(opts[i + 1])
            args.remove(opts[i + 1])
        if a == "--profile":
            global PROFILE
            PROFILE = os.path.abspath(opts[i + 1])
            args.remove(opts[i + 1])
        if a == "--only":
            only = opts[i + 1]
            args.remove(opts[i + 1])
    machines = args[1:] or ["ST:mono", "ST:med", "Falcon-4:16"]
    runs = []
    for m in machines:
        print("%s%s:" % (m, " (NVDI)" if os.environ.get("AERA_WITH_NVDI") else ""))
        runs.append(machine_run(work, m, bin_dir, only))
    over = [(r.label, x["action"]) for r in runs for x in r.rows if x["over"]]
    with open(os.path.join(work, "speed.json"), "w") as f:
        json.dump([dict(machine=r.label, rows=r.rows) for r in runs], f, indent=1)
    with open(os.path.join(work, "speed.txt"), "w") as f:
        for r in runs:
            f.write("%s\n" % r.label)
            for x in r.rows:
                f.write("  %-40s %6d ms  limit %d%s\n" % (x["action"], x["ms"], x["limit"],
                                                        "  OVER" if x["over"] else ""))
    print("%d over their time" % len(over) if over else "all within their times")
    sys.exit(1 if over else 0)


if __name__ == "__main__":
    main()
