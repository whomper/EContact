#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's refresh with its window closed (SYNC_AFTER=, REFRESH=),
in Hatari on the family's ST (common/tools/machines.py; or the machine
named), with an edition built to time the stretches between its event
calls (see quiet_start.py):

    make OBJDIR=WORK/build BIN=WORK/bin EXTRA_CFLAGS=-DAES_PROFILE \\
        WORK/bin/ECONTACT.ACC
    tests/hatari/quiet_sync.py WORK [MACHINE] [contacts]

A Radicale with 40 contacts (by default) behind the gateway
(gateway/ecalgate.py); card-cli fetches them, then on the server some
change, some go and some are new. The accessory reads its books in the
background after boot (START=5) and a minute later refreshes them
(SYNC_AFTER=1), its window closed: what changed must be in the cache,
added to its end, and no stretch between event calls longer than 100 ms
on a 68000 (50 on a 68030) but the exchanges with the server. Hatari has
no network card: machines.py's network() puts common's FAKESTNG.PRG in
C:\\AUTO and starts its serial_bridge.py (common/tools/fakesting). FAKESTNG
reads and sends each byte through the emulated serial port, waiting in
the ROM (seconds for a list of contacts), so the exchanges' stretches
here are the line's, reported but not judged; with STinG the answer is
already buffered. It also drops what overflows its buffer, so a much
bigger book (200) stalls the rig, not EContact.
"""
import base64
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
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


def main():
    work = os.path.abspath(sys.argv[1])
    machine = machine_arg(sys.argv, 2, "ST:mono")
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    st = machine.startswith("ST")          # a 68000
    limit = 100 if st else 50
    d = os.path.join(work, machine.replace(":", "-"))
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    procs = []
    rport, gport = free_port(), 18183
    with open(os.path.join(d, "users"), "w") as f:
        f.write("alice:wonderland\n")
    procs.append(subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % rport,
                                   "--storage-filesystem-folder", os.path.join(d, "store"), "--auth-type", "htpasswd",
                                   "--auth-htpasswd-filename", os.path.join(d, "users"),
                                   "--auth-htpasswd-encryption", "plain"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    procs.append(subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "ecalgate.py"), "--port", str(gport),
                                   "--allow-local"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    base = "http://127.0.0.1:%d/alice/" % rport
    auth = "Basic " + base64.b64encode(b"alice:wonderland").decode()

    def http(method, url, data=None, ctype="text/vcard"):
        rq = urllib.request.Request(url, data=data, method=method,
                                    headers={"Content-Type": ctype, "Authorization": auth})
        return urllib.request.urlopen(rq).read()

    def card(i, given="Club"):
        return ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Member%03d;%s;;;\r\nFN:%s Member%03d\r\n"
                "EMAIL:m%03d@example.org\r\nTEL;type=CELL:+972 54-555-%04d\r\nORG:Company %d;\r\n"
                "NOTE:Met at the meeting %d\\, call back later.\r\nUID:QS-%03d\r\nEND:VCARD\r\n"
                % (i, given, given, i, i, i, i % 40, i, i)).encode()

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
        subprocess.run([CLI, folder, "carddav", "Club", "http://127.0.0.1:%d/" % rport, "alice", "wonderland"],
                       check=True, stdout=subprocess.DEVNULL)
        dat = [os.path.join(folder, "CACHE", f) for f in os.listdir(os.path.join(folder, "CACHE")) if f.endswith(".DAT")]
        gen = open(dat[0], "rb").readline()
        # meanwhile, on the server: 10 changed, 5 gone, 10 new
        for i in range(0, min(n, 50), 5):
            http("PUT", base + "club/m%03d.vcf" % i, card(i, "Changed"))
        for i in range(1, min(n, 50), 10):
            http("DELETE", base + "club/m%03d.vcf" % i)
        for i in range(n, n + 10):
            http("PUT", base + "club/m%03d.vcf" % i, card(i, "New"))
        inf = open(os.path.join(folder, "ECONTACT.INF"), "rb").read()
        inf = inf.replace(b"\nLOG=0", b"\nLOG=1").replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
        inf = re.sub(rb"\nSTART=[^\r\n]*", rb"\nSTART=5", inf)
        inf = re.sub(rb"\nSYNC_AFTER=[^\r\n]*", rb"\nSYNC_AFTER=1", inf)
        inf = re.sub(rb"\nGATE=[^\r\n]*", rb"\nGATE=127.0.0.1", inf)
        inf = re.sub(rb"\nGATEPORT=[^\r\n]*", b"\nGATEPORT=%d" % gport, inf)
        inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
        os.remove(os.path.join(folder, "ECONTACT.INF"))
        open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
        shutil.copy(os.path.join(work, "bin", "ECONTACT.ACC"),
                    os.path.join(hd, "ECONTACT.ACC"))
        log = os.path.join(folder, "ECONTACT.LOG")
        r = Rig(d, machine, auto=None, network=True)
        try:
            t0 = time.time()
            done = False
            while time.time() - t0 < int(os.environ.get("QS_WAIT", "900")) and not done:
                time.sleep(3)
                t = open(log, encoding="latin-1").read() if os.path.exists(log) else ""
                i = t.find("refreshing in the background")
                done = i >= 0 and "the list sorted" in t[i:]
            took = time.time() - t0
            time.sleep(3)
            r.click(30, 8)
            time.sleep(1.5)
            r.click(60, 59)
            time.sleep(10)
            r.shot("open-after-sync")
        finally:
            r.stop()
    finally:
        for p in procs:
            p.terminate()
    con = open(os.path.join(d, "hatari.log"), encoding="latin-1").read()
    gaps = {}
    for m in re.finditer(r"AESGAP (\d+) ms (\S+)", con):
        gaps.setdefault(m.group(2), []).append(int(m.group(1)))
    print("%s: refreshed %s, %.0f s after boot" % (machine, "in the background" if done else "NOT", took))
    t = open(log, encoding="latin-1").read()
    for l in t.splitlines():
        if "Contacts --" in l or "background" in l:
            print("   " + l)
    for tag, g in sorted(gaps.items()):
        print("   %-14s %3d over 50 ms, %3d over %d, the longest %d ms"
              % (tag, len(g), len([x for x in g if x > limit]), limit, max(g)))
    # the exchanges with the server: FAKESTNG sends each byte through the
    # emulated serial port at 19,200 baud, waiting in the ROM, so their
    # time here is mostly the line's (with STinG a send is buffered)
    net = ("sync-ctag", "sync-list", "sync-fetch")
    quiet = [g for tg, v in gaps.items() if not tg.startswith("open") and tg != "redraw" and tg not in net for g in v]
    check("refreshed with the window closed", done)
    # the family's machines keep timer C at 200 Hz: a boot that
    # "corrected" its clock measured it wrong, and its times are off
    check("the clock taken as it is", "times corrected" not in t)
    check("no stretch over %d ms but the opening's and the serial line's (%s)"
          % (limit, "the longest %d ms" % max(quiet) if quiet else "none reached 50 ms"), max(quiet or [0]) <= limit)
    check("added to the cache, not written anew", open(dat[0], "rb").readline() == gen)
    shutil.copy(os.path.join(hd, "ECONTACT.INF"), os.path.join(folder, "ECONTACT.INF"))	# for card-cli
    out = subprocess.run([CLI, folder, "list"], check=True, capture_output=True, text=True).stdout
    changed, gone = len(range(0, min(n, 50), 5)), len(range(1, min(n, 50), 10))
    check("the changed ones (%d)" % changed, out.count("Changed Member") == changed)
    check("the new ones", out.count("New Member") == 10)
    check("the gone ones gone", "Club Member001 |" not in out and "Member011 |" not in out)
    check("all of them (%d)" % (n + 10 - gone), len([l for l in out.splitlines() if "Member" in l]) == n + 10 - gone)
    print("screenshots in", os.path.join(d, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
