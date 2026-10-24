#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's files across its event calls (the guidelines, 2.2: no
file open across an event call), in Hatari, through the gateway:

    tests/hatari/acc_files.py WORK [MACHINE] [cards] [photos] [--prg]

--prg: ECONTACT.PRG in C:\\ECONTACT, started by Hatari (--auto), the same
steps in its window: the program's bigger memory (256 KB a job).

A Radicale behind tests/host/partialproxy.py (a server that leaves the
photos out when asked) and gateway/ecalgate.py, with cards (40 by
default) that have photos. card-cli fetches them first; then on the
server some change and some are new. The accessory opens its window
and refreshes, the cache files open across the network's waits (and
let go at each event call: ui/evwrap.c, pf_park); then Sync Contact
Photos fetches every photo the same way. What the accessory wrote must
read back whole with card-cli: every card, the changed ones changed, a
picture for every photo. The log says how many files each opened
("N files opened"), for TOS's internal pool (pfkeep.c). Hatari's GEMDOS
trace checks that no file the accessory opened is still open when it
next calls the AES (MACHINE is the Falcon in 16 colours by default).
"""
import base64
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
# another build to compare (make OBJDIR=... BIN=...): ACCF_BIN, ACCF_OBJ
BIN = os.environ.get("ACCF_BIN", os.path.join(ROOT, "binary"))
OBJ = os.environ.get("ACCF_OBJ", os.path.join(ROOT, "build"))
fails = 0


def check(what, ok):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def photos():
    """the drawn portraits of sample.vcf, folded as they are there"""
    text = open(os.path.join(HERE, "sample.vcf"), encoding="utf-8").read().replace("\r\n", "\n")
    out = []
    for card in text.split("BEGIN:VCARD")[1:]:
        m = re.search(r"\nPHOTO[^\n]*(\n [^\n]*)*", card)
        if m:
            out.append(m.group(0).strip("\n").replace("\n", "\r\n"))
    return out


def main():
    prg = "--prg" in sys.argv
    args = [a for a in sys.argv[1:] if a != "--prg"]
    work = os.path.abspath(args[0])
    machine = machine_arg(["", *args], 2, "Falcon:16")
    n = int(args[2]) if len(args) > 2 else 40
    nph = int(args[3]) if len(args) > 3 else n // 2
    step = max(1, n // nph) if nph else 0
    st = machine.startswith("ST")
    small = machine.startswith("ST-1") and not prg	# 1 MB: no Sync Contact Photos in the accessory
    d = os.path.join(work, machine.replace(":", "-") + ("-prg" if prg else ""))
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    with open(os.path.join(d, "users"), "w") as f:
        f.write("alice:wonderland\n")
    rport, pport, gport = free_port(), free_port(), free_port()
    procs = [subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % rport,
                               "--storage-filesystem-folder", os.path.join(d, "store"), "--auth-type", "htpasswd",
                               "--auth-htpasswd-filename", os.path.join(d, "users"),
                               "--auth-htpasswd-encryption", "plain"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL),
             subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "host", "partialproxy.py"), str(pport),
                               "http://127.0.0.1:%d" % rport],
                              stdout=subprocess.DEVNULL, stderr=open(os.path.join(d, "proxy.log"), "w")),
             subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "ecalgate.py"), "--port", str(gport),
                               "--allow-local"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)]
    base = "http://127.0.0.1:%d/alice/" % rport
    auth = "Basic " + base64.b64encode(b"alice:wonderland").decode()
    pics = photos()

    def http(method, url, data=None, ctype="text/vcard"):
        rq = urllib.request.Request(url, data=data, method=method,
                                    headers={"Content-Type": ctype, "Authorization": auth})
        return urllib.request.urlopen(rq).read()

    def card(i, given="Club"):
        photo = (pics[i % len(pics)] + "\r\n") if step and i % step == 0 and i // step < nph else ""
        return ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Member%03d;%s;;;\r\nFN:%s Member%03d\r\n"
                "EMAIL:m%03d@example.org\r\nTEL;type=CELL:+972 54-555-%04d\r\n%sUID:AF-%03d\r\nEND:VCARD\r\n"
                % (i, given, given, i, i, i, photo, i)).encode()

    try:
        for _ in range(50):
            try:
                http("PROPFIND", base, b"", "application/xml")
                break
            except Exception:
                time.sleep(0.2)
        http("MKCOL", base + "club/", b'<?xml version="1.0" encoding="utf-8"?><D:mkcol xmlns:D="DAV:" '
             b'xmlns:C="urn:ietf:params:xml:ns:carddav"><D:set><D:prop><D:resourcetype><D:collection/>'
             b'<C:addressbook/></D:resourcetype><D:displayname>Club</D:displayname></D:prop></D:set></D:mkcol>',
             "application/xml")
        for i in range(n):
            http("PUT", base + "club/m%03d.vcf" % i, card(i))
        subprocess.run([CLI, folder, "carddav", "Club", "http://127.0.0.1:%d/" % pport, "alice", "wonderland"],
                       check=True, stdout=subprocess.DEVNULL)
        # meanwhile, on the server: some changed, some new (with photos too)
        for i in range(1, n, 4):
            http("PUT", base + "club/m%03d.vcf" % i, card(i, "Changed"))
        for i in range(n, n + 10):
            http("PUT", base + "club/m%03d.vcf" % i, card(i, "New"))
        inf = open(os.path.join(folder, "ECONTACT.INF"), "rb").read()
        for k, v in ((b"LOG", b"1"), (b"SYNC_OPEN", b"1"), (b"SYNC_AFTER", b"0"), (b"START", b"now"),
                     (b"GATE", b"127.0.0.1"), (b"GATEPORT", b"%d" % gport), (b"FOLDER", b"C:\\\\ECONTACT")):
            inf = re.sub(rb"\n" + k + rb"=[^\r\n]*", b"\n" + k + b"=" + v, inf)
        os.remove(os.path.join(folder, "ECONTACT.INF"))
        open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
        if prg:
            shutil.copy(os.path.join(BIN, "ECONTACT.PRG"), os.path.join(folder, "ECONTACT.PRG"))
        else:
            shutil.copy(os.path.join(BIN, "ECONTACT.ACC"),
                        os.path.join(hd, "ECONTACT.ACC"))
        log = os.path.join(folder, "ECONTACT.LOG")

        def log_text():
            return open(log, encoding="latin-1").read() if os.path.exists(log) else ""

        def wait_for(what, count, secs):
            if "ACCF_WAIT" in os.environ:	# an older build doesn't log "files opened"
                time.sleep(int(os.environ["ACCF_WAIT"]) * count)
                return True
            t0 = time.time()
            while time.time() - t0 < secs:
                if log_text().count(what) >= count:
                    return True
                time.sleep(3)
            return False

        r = Rig(d, machine, auto="C:\\ECONTACT\\ECONTACT.PRG" if prg else None, network=True,
                extra=("--trace", "gemdos,aes", "--trace-file", os.path.join(d, "trace.txt")))
        try:
            time.sleep(20)
            if not prg:
                r.click(30, 8)		# Desk
                time.sleep(1.5)
                r.click(60, 59 if machine.endswith("mono") or not st else 61)	# EContact
            synced = wait_for("files opened", 1, 900)
            time.sleep(3)
            r.shot("1-synced")
            r.key(0x01)			# Esc: an alert there (contacts that don't fit) goes
            # Sync Contact Photos: the menu (the button at the toolbar's right)
            r.click(*MENU)
            time.sleep(2)
            r.shot("2-menu")
            r.click(PHOTOS[0], PHOTOS_ST if st else PHOTOS[1])
            # a 1 MB machine: the accessory's memory is the contacts', it
            # says so at once (Erez, 19 Oct 2026)
            photod = wait_for(DECLINED, 1, 120) if small else wait_for("files opened", 2, 1500)
            time.sleep(3)
            r.shot("3-photos")
            r.key(0x1c)			# the closing alert's OK
            time.sleep(3)
        finally:
            r.stop()
    finally:
        for p in procs:
            p.terminate()

    t = log_text()
    opened = re.findall(r"(\d+) files opened", t)
    print("files opened: the refresh %s, Sync Contact Photos %s" % tuple((opened + ["?", "?"])[:2]))
    # the guidelines, 6.5: about one open a file a job, and one a buffer
    # written (the photos, the cards and the pack through the job's
    # memory: 24 KB on a 1 MB ST to 256 KB with 8 MB free, the log 16 KB)
    made = len(pack_pictures(os.path.join(folder, "CACHE", "PHOTOS.PAK")))
    # a fixed count, and a round of writes each 16 photos or so (the
    # photos when the job's memory is full, the cards and the parsed cache
    # when theirs is, the log each 16 KB); the accessory on a 4 MB ST has
    # 64 KB a job (job_memory; Erez, 19 Oct: its memory kept small), about
    # one round each 2 photos (103 opens for 192)
    mid = st and not small and not prg
    budget = (16, 14 + (made // 2 if mid else made // 16))
    if opened and "ACCF_WAIT" not in os.environ:
        check("the refresh within its budget (%s of %d opens)" % (opened[0], budget[0]), int(opened[0]) <= budget[0])
    if len(opened) >= 2 and "ACCF_WAIT" not in os.environ:
        check("Sync Contact Photos within its budget (%s of %d opens, %d photos)" % (opened[1], budget[1], made),
              int(opened[1]) <= budget[1])
    check("refreshed with the window open", synced)
    if small:
        check("Sync Contact Photos declined on a 1 MB machine", photod and len(opened) == 1)
    else:
        check("Sync Contact Photos ran", photod)
    shutil.copy(os.path.join(hd, "ECONTACT.INF"), os.path.join(folder, "ECONTACT.INF"))	# for card-cli
    out = subprocess.run([CLI, folder, "list"], check=True, capture_output=True, text=True).stdout
    changed = len(range(1, n, 4))
    check("every card (%d)" % (n + 10), len([l for l in out.splitlines() if "Member" in l]) == n + 10)
    check("the changed ones (%d)" % changed, out.count("Changed Member") == changed)
    check("the new ones", out.count("New Member") == 10)
    pics_made = pack_pictures(os.path.join(folder, "CACHE", "PHOTOS.PAK"))
    with_photo = len([i for i in range(n + 10) if step and i % step == 0 and i // step < nph])
    if not small:
        check("a picture for every photo (%d of %d)" % (len(pics_made), with_photo), len(pics_made) >= with_photo)
    crash = [os.path.join(dd, f) for dd, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    if not prg:		# (2.2 is the accessory's: a program's files are its own)
        left = open_across_aes(os.path.join(d, "trace.txt"), t, st)
        check("no file open across an AES event call (%s)" % left[1], left[0] == 0)
    print("screenshots in", os.path.join(d, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


# where the toolbar's menu button is, and its Sync Contact Photos line
MENU = (598, 73)
PHOTOS = (520, 201)
PHOTOS_ST = 171			# on an ST's 400 lines
DECLINED = "Sync Contact Photos: not in the accessory, "


def pack_pictures(path):
    """the pictures made in CACHE\\PHOTOS.PAK (src/photopack.c): its index's
    entries that aren't failures"""
    if not os.path.exists(path):
        return []
    b = open(path, "rb").read()
    off, count = struct.unpack(">II", b[8:16])
    return [i for i in range(count) if not b[off + 28 * i + 22]]


EVENTS = re.compile(r"^AES 0x(19|18|32|34|5A|5B) ")


def open_across_aes(trace, logtext, st):
    """Hatari's GEMDOS and AES trace: files the accessory opened (Fopen,
    Fcreate from its own code: the log's first line says where that is)
    and not yet closed when an event call comes (evnt_multi, evnt_timer,
    form_do, form_alert, fsel_input, fsel_exinput). Returns (how many
    times, what)"""
    m = re.search(r"program at ([0-9a-f]+).*memory at ([0-9a-f]+)", logtext)
    if not m or not os.path.exists(trace):
        return (1, "no trace or no address in the log")
    # the program's code starts where set_logging is, less its place in it
    elf = os.path.join(OBJ, "contact.elf")
    nm = subprocess.run(["m68k-linux-gnu-nm", elf], capture_output=True, text=True).stdout
    off = int(re.search(r"^([0-9a-f]+) T set_logging$", nm, re.M).group(1), 16)
    lo, hi = int(m.group(1), 16) - off, int(m.group(2), 16)
    ours = lambda line: (lambda a: a is not None and lo <= int(a.group(1), 16) < hi)(  # noqa: E731
        re.search(r"PC[= ]0x([0-9A-Fa-f]+)", line))
    open_now, pending, bad, calls, opens = {}, None, 0, 0, 0
    first = ""
    for line in open(trace, encoding="latin-1"):
        if line.startswith("GEMDOS 0x"):
            pending = None
            if re.match(r"GEMDOS 0x(3D|3C) ", line) and ours(line):
                pending = line.strip()
            else:
                c = re.match(r"GEMDOS 0x3E Fclose\((\d+)\)", line)
                if c and ours(line):
                    open_now.pop(int(c.group(1)), None)
        elif line.startswith("-> FD ") and pending:
            open_now[int(line.split()[2])] = pending
            opens += 1
            pending = None
        elif EVENTS.match(line):
            calls += 1
            if open_now:
                bad += 1
                if not first:
                    first = "; ".join(open_now.values())[:200]
    return (bad, "%d of %d event calls with a file open, %d opens%s"
            % (bad, calls, opens, (": " + first) if first else ""))


if __name__ == "__main__":
    main()
