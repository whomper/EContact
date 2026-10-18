#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc stops the network (the AERA guidelines, 7.1): the accessory syncs
through a "gateway" that takes the connection and never answers, as a
server can. Esc while the busy bee shows must end the wait at once, not
after the 60 s timeout, and EContact must carry on.

Hatari has no network card: FAKESTNG.PRG carries the Atari's TCP
connections over the emulated serial port to serial_bridge.py; both are
common's, set up by machines.py's network() (common/tools).

    tests/hatari/stop_test.py WORKDIR [MACHINE]       (ST:mono by default)
"""
import os
import shutil
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(__file__))
import rig  # noqa: E402
from rig import K  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
GW = 18182


def silent_server(stop):
    """takes connections and says nothing"""
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", GW))
    s.listen(4)
    s.settimeout(0.5)
    held = []
    while not stop.is_set():
        try:
            c, _ = s.accept()
            held.append(c)
        except socket.timeout:
            pass
    for c in held:
        c.close()
    s.close()


def main():
    work, machine = os.path.abspath(sys.argv[1]), rig.machine_arg(sys.argv, 2, "ST:mono")
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(os.path.join(hd, "ECONTACT"))
    fails = []

    def check(what, ok):
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            fails.append(what)

    stop = threading.Event()
    threading.Thread(target=silent_server, args=(stop,), daemon=True).start()
    acc = "ECONTST.ACC" if machine.startswith("ST") else "ECONTACT.ACC"
    shutil.copy(os.path.join(ROOT, "binary", acc), os.path.join(hd, "ECONTACT.ACC"))
    with open(os.path.join(hd, "ECONTACT", "ECONTACT.INF"), "w", newline="") as f:
        f.write("gate=127.0.0.1\r\ngateport=%d\r\n"
                "sync_open=1\r\nlog=1\r\n[account]\r\nname=Server\r\nkind=0\r\n"
                "url=http://127.0.0.1:15232/\r\nuser=alice\r\npass=wonderland\r\n" % GW)
    r = rig.Rig(work, machine, auto=None, network=True)
    log = os.path.join(hd, "ECONTACT", "ECONTACT.LOG")
    try:
        time.sleep(18)
        r.click(18, 8)                  # the Desk menu: EContact, which syncs at once
        time.sleep(1)
        r.click(60, 8 + 3 * 16 + 8)
        time.sleep(12)                  # connected, and waiting for the answer
        r.shot("s1-waiting")
        r.key(K["ESC"])
        pressed = time.time()
        stopped = None
        while time.time() - pressed < 50 and stopped is None:
            time.sleep(1)
            text = open(log, encoding="latin-1").read() if os.path.exists(log) else ""
            if "stopped" in text:
                stopped = time.time() - pressed
        r.shot("s2-stopped")
        check("Esc ended the wait (%s)" % ("%.0f s" % stopped if stopped is not None else "not at all"),
              stopped is not None and stopped < 15)
        r.key(K["RETURN"])              # the alert that says so, if any
        time.sleep(3)
        r.shot("s3-after")
    finally:
        r.stop()
        stop.set()
    text = open(log, encoding="latin-1").read() if os.path.exists(log) else ""
    check("no timeout", "timed out" not in text)
    print("screenshots in", os.path.join(work, "shots"))
    print("stop test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
