#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The screenshots in docs/ and the README, taken in Hatari with the
sample contacts (tests/hatari/sample.vcf, made-up people):

    tests/hatari/docs_shots.py WORK [--email [EMail@vX.Y]] [--ecal [ECal@vX.Y]]
                               [--ecal-src DIR] [--falcon M] [--st M] [--update]

On the family's machines (common/tools/machines.py): the Falcon at 640x480
in 16 colours (--falcon names another, Falcon:256 say) and the ST in mono
(--st).

--memory takes memory-full: ECONTST.ACC on a 1 MB ST (ST-1 in mono)
with 1,000 contacts, more than fit, and the alert that says so.

--email and --ecal take the shots of EContact working with EMail (1.35 or
later: it asks with PEOPLE) and ECal (2.5 or later): their latest
releases, what Erez runs (the guidelines, 1.2), or the one named, fetched
by common/tools/getrelease.py; the shots' folders say which (*.VERSION).
--ecal-src names a checkout of ECal (../ecal if not given, with its
build/cal-cli made) for its test
data only: ui_tour.seed() writes the calendar the shots show.
They land in WORK/*/shots; --update copies them into docs/.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "binary")
CLI = os.path.join(ROOT, "build", "card-cli")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import getrelease  # noqa: E402


def seed(folder):
    """an On My Atari address book with the sample people and two groups"""
    def cli(*a):
        subprocess.run([CLI, folder] + list(a), check=True, stdout=subprocess.DEVNULL)
    cli("local", "Contacts")
    cli("import", "Contacts", os.path.join(ROOT, "tests", "hatari", "sample.vcf"))
    cli("group-new", "Contacts", "Atari Club")
    for n in ("Grace Baker", "Daniel Hughes", "Priya Patel"):
        cli("group-add", "Atari Club", n)
    cli("group-new", "Contacts", "Family")
    cli("group-add", "Family", "Emma Johnson")


def as_accessory(hd):
    """EContact's folder at C:\\ECONTACT, its settings in the root"""
    folder = os.path.join(hd, "ECONTACT")
    seed(folder)
    with open(os.path.join(folder, "ECONTACT.INF"), newline="") as f:
        t = f.read()
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    lines = [("FOLDER=C:\\ECONTACT" if ln.startswith("FOLDER=") else ln) for ln in t.split("\r\n")]
    with open(os.path.join(hd, "ECONTACT.INF"), "w", newline="") as f:
        f.write("\r\n".join(lines))
    shutil.copy(os.path.join(BIN, "ECONTACT.ACC"), os.path.join(hd, "ECONTACT.ACC"))


def program(work, machine, shots):
    """ECONTACT.PRG on its own"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    seed(hd)
    shutil.copy(os.path.join(BIN, "ECONTACT.PRG"), os.path.join(hd, "ECONTACT.PRG"))
    r = Rig(work, machine)
    try:
        time.sleep(16)
        shots(r)
    finally:
        r.stop()
    return os.path.join(work, "shots")


def memory_full(work, machine):
    """the accessory on a 1 MB ST with more contacts than fit"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    first = "Anna Ben Clara David Emma Frank Grace Henry Iris Jack Kate Liam Mia Noah Olivia Paul".split()
    last = "Anderson Baker Carter Davis Evans Foster Green Hughes Irwin Jones Klein Lewis Moore".split()
    vcf = os.path.join(work, "many.vcf")
    with open(vcf, "w", newline="") as f:
        for i in range(1000):
            g, fam = first[i % len(first)], last[(i // len(first)) % len(last)]
            f.write("BEGIN:VCARD\r\nVERSION:3.0\r\nN:%s;%s;;;\r\nFN:%s %s\r\nORG:Globex;\r\n"
                    "EMAIL;type=INTERNET;type=HOME:%s.%s%d@example.com\r\n"
                    "TEL;type=CELL:+1 555 01%02d\r\nUID:DOCS-%04d\r\nEND:VCARD\r\n"
                    % (fam, g, g, fam, g.lower(), fam.lower(), i, i % 100, i))
    subprocess.run([CLI, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([CLI, folder, "import", "Contacts", vcf], check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(folder, "ECONTACT.INF"), newline="") as f:
        t = f.read()
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    lines = [("FOLDER=C:\\ECONTACT" if ln.startswith("FOLDER=") else "START=5" if ln.startswith("START=")
              else ln) for ln in t.split("\r\n")]
    with open(os.path.join(hd, "ECONTACT.INF"), "w", newline="") as f:
        f.write("\r\n".join(lines))
    shutil.copy(os.path.join(BIN, "ECONTST.ACC"), os.path.join(hd, "ECONTACT.ACC"))
    r = Rig(work, machine, auto="")
    try:
        time.sleep(120)                 # read in the background, as far as memory goes
        r.click(30, 8)                  # Desk
        time.sleep(1.5)
        r.click(60, 59)                 # EContact: the window, and the alert
        time.sleep(12)
        r.move(600, 30)                 # the pointer off the alert
        time.sleep(1)
        r.shot("memory-full")
        r.key(K["RETURN"])
    finally:
        r.stop()
    return os.path.join(work, "shots")


def falcon_shots(r):
    time.sleep(4)                        # the first card's photo
    r.click(200, 204)                    # Maya Carter
    time.sleep(5)                        # her photo, decoded the first time
    r.shot("window")
    r.ctrl("e")
    time.sleep(2)
    r.shot("edit")
    r.click(120, 191)                    # Phone
    time.sleep(1)
    r.shot("edit-phone")
    r.key(K["ESC"])
    time.sleep(1)
    r.click(80, 166)                     # the Atari Club group
    time.sleep(5)
    r.shot("group")
    r.click(80, 101)                     # All Contacts
    time.sleep(1)
    r.type("pi", pace=0.15)
    time.sleep(1.5)
    r.shot("search")
    r.key(K["ESC"])
    time.sleep(1)
    r.click(200, 132)
    time.sleep(1)
    r.xdo("click", "3")                  # right-click: the contact's menu
    time.sleep(1.5)
    r.shot("context-menu")
    r.key(K["ESC"])
    time.sleep(1)
    r.click(600, 73)                     # the menu
    time.sleep(1.5)
    r.click(500, 201)                    # Sync Contact Photos...
    time.sleep(3.5)
    r.shot("photo-sync")
    time.sleep(20)
    r.key(K["RETURN"])                   # "6 photos made."


def st_shots(r):
    time.sleep(8)                        # an 8 MHz ST decodes the photo
    r.shot("st-mono")


def with_email(work, machine, spec):
    """EContact as an accessory over EMail: ^B completes a name"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    as_accessory(hd)
    getrelease.fetch(spec, os.path.join(work, "shots"), ["EMAIL.PRG"])
    shutil.move(os.path.join(work, "shots", "EMAIL.PRG"), os.path.join(hd, "EMAIL.PRG"))
    with open(os.path.join(hd, "EMAIL.INF"), "w", newline="") as f:
        f.write("[OPTIONS]\r\nOFFLINE=1\r\n\r\n[ACCOUNT]\r\nNAME=Home\r\nFULLNAME=Erez Yaary\r\n"
                "EMAIL=erez@example.com\r\nIN=imap\r\nHOST=192.168.1.2\r\nPORT=143\r\nUSER=erez\r\n")
    r = Rig(work, machine, auto="C:\\EMAIL.PRG")
    try:
        time.sleep(18)
        r.click(300, 300)
        time.sleep(0.5)
        r.ctrl("n")
        time.sleep(1.5)
        r.type("gr", pace=0.1)
        r.ctrl("b")
        time.sleep(1.5)
        r.shot("email-complete")
        r.key(K["ESC"])
    finally:
        r.stop()
    return os.path.join(work, "shots")


def with_ecal(work, machine, spec, ecal_src):
    """ECal's invitations from EContact's people"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    as_accessory(hd)
    getrelease.fetch(spec, os.path.join(work, "shots"), ["ECAL.PRG"])
    shutil.move(os.path.join(work, "shots", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    # ECal's ui_tour: its seed() writes an On My Atari calendar. It takes
    # ECal's own rig.py, not this one
    ours = sys.modules.pop("rig")
    sys.path.insert(0, os.path.join(ecal_src, "tests", "hatari"))
    try:
        import ui_tour
    finally:
        sys.path.pop(0)
        sys.modules["rig"] = ours
    ui_tour.seed(hd, english=True)
    r = Rig(work, machine, auto="C:\\ECAL.PRG")
    try:
        time.sleep(18)
        r.ctrl("n")
        time.sleep(2)
        r.type("Atari meet", pace=0.07)
        r.click(505, 125)               # People: Add...
        time.sleep(2)
        r.type("ma", pace=0.1)          # Name or address
        r.key(K["RETURN"])              # OK: ECal asks EContact (PEOPLE pick:),
        time.sleep(8)                   # and Maya Carter is in People (ECal 2.21)
        r.shot("ecal-invitees")
        r.key(K["ESC"])
    finally:
        r.stop()
    return os.path.join(work, "shots")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--falcon", default="Falcon:16")
    ap.add_argument("--st", default="ST:mono")
    ap.add_argument("--memory", action="store_true", help="memory-full, on ST-1")
    ap.add_argument("--email", nargs="?", const="EMail")
    ap.add_argument("--ecal", nargs="?", const="ECal")
    ap.add_argument("--ecal-src", default=os.path.join(ROOT, "..", "ecal"))
    ap.add_argument("--update", action="store_true", help="copy them into docs/")
    a = ap.parse_args()
    dirs = [program(os.path.join(a.work, "falcon"), a.falcon, falcon_shots),
            program(os.path.join(a.work, "st"), a.st, st_shots)]
    if a.memory:
        dirs.append(memory_full(os.path.join(a.work, "memory"), "ST-1:mono"))
    if a.email:
        dirs.append(with_email(os.path.join(a.work, "email"), a.falcon, a.email))
    if a.ecal:
        dirs.append(with_ecal(os.path.join(a.work, "ecal"), a.falcon, a.ecal, a.ecal_src))
    for d in dirs:
        for f in sorted(os.listdir(d)):
            if not f.endswith(".png") or f.startswith("grab"):
                continue
            print(os.path.join(d, f))
            if a.update:
                shutil.copy(os.path.join(d, f), os.path.join(ROOT, "docs", f))


if __name__ == "__main__":
    main()
