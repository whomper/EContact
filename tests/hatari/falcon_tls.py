#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A Direct connection in Hatari: TLS on the Falcon itself (TLS.OVL, the
guidelines 7.4), with an ECDSA server, the curves on the DSP, no gateway:

    tests/hatari/falcon_tls.py WORK [MACHINE] [--no-dsp]

A Radicale serves HTTPS with a certificate of a P-256 key signed by a
P-384 test root (added to CACERT.PEM, ROOTS.DAT made by build/mkroots
without CACERT.PEM's date). A second Radicale on the same store, plain
HTTP, lets card-cli make the account and its cache first; the INF then
points at the HTTPS one. ECONTACT.PRG starts twice on the Falcon (TT RAM,
the DSP: machines.py), refreshing as its window opens:

- the first time: TLS.OVL loaded ("TLS.OVL 1.0 loaded" in the log); a
  card new on the server comes over TLS 1.2, the key
  exchange and the root's ECDSA signature on the DSP, ROOTS.DAT given
  CACERT.PEM's date in place (PF_UPDATE), SIGS.DAT written;
- the second time: the signature is one "checked before" (SIGS.DAT);
- a third time without TLS.OVL: the log says it is missing, and no crash;
- no CRASH.LOG.

With --no-dsp (DSP=0) the curves are BearSSL's on the 68030: the same,
slower. Hatari has no network card: machines.py's network() puts
FAKESTNG.PRG in C:\\AUTO and starts its serial bridge.
"""
import base64
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, machine_arg  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(ROOT, "build", "card-cli")
MKROOTS = os.path.join(ROOT, "build", "mkroots")
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


def openssl(*args):
    subprocess.run(["openssl"] + list(args), check=True, capture_output=True)


def certs(d):
    """a P-384 root and a P-256 server key it signs, for 127.0.0.1"""
    ca_key, ca, key, csr, crt = (os.path.join(d, n) for n in ("ca.key", "ca.pem", "srv.key", "srv.csr", "srv.pem"))
    ext = os.path.join(d, "srv.ext")
    openssl("ecparam", "-name", "secp384r1", "-genkey", "-noout", "-out", ca_key)
    openssl("req", "-x509", "-new", "-key", ca_key, "-days", "3", "-subj", "/CN=EContact test root",
            "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign",
            "-out", ca)
    openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key)
    openssl("req", "-new", "-key", key, "-subj", "/CN=127.0.0.1", "-out", csr)
    open(ext, "w").write("subjectAltName=DNS:127.0.0.1,IP:127.0.0.1\nextendedKeyUsage=serverAuth\n")
    openssl("x509", "-req", "-in", csr, "-CA", ca, "-CAkey", ca_key, "-CAcreateserial", "-days", "2",
            "-sha384", "-extfile", ext, "-out", crt)
    chain = os.path.join(d, "chain.pem")
    open(chain, "wb").write(open(crt, "rb").read() + open(ca, "rb").read())
    return ca, chain, key


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dsp = "--no-dsp" not in sys.argv
    work = os.path.abspath(args[0])
    machine = machine_arg(["", *args], 2, "Falcon:16")
    d = os.path.join(work, machine.replace(":", "-") + ("" if dsp else "-nodsp"))
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    ca, chain, key = certs(d)
    with open(os.path.join(d, "users"), "w") as f:
        f.write("alice:wonderland\n")
    hport, sport = free_port(), free_port()
    common = ["--storage-filesystem-folder", os.path.join(d, "store"), "--auth-type", "htpasswd",
              "--auth-htpasswd-filename", os.path.join(d, "users"), "--auth-htpasswd-encryption", "plain"]
    procs = [subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % hport] + common,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL),
             subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % sport,
                               "--server-ssl", "--server-certificate", chain, "--server-key", key,
                               # 30 s by default: the 68030 alone takes about that for the P-384 check
                               "--server-timeout", "180"] + common,
                              stdout=subprocess.DEVNULL, stderr=open(os.path.join(d, "radicale-tls.log"), "w"))]
    base = "http://127.0.0.1:%d/alice/" % hport
    auth = "Basic " + base64.b64encode(b"alice:wonderland").decode()

    def http(method, url, data=None, ctype="text/vcard"):
        rq = urllib.request.Request(url, data=data, method=method,
                                    headers={"Content-Type": ctype, "Authorization": auth})
        return urllib.request.urlopen(rq).read()

    # the first sample card's photo (folded lines and all): with PIC.OVL
    # beside the program, its dither goes to the DSP that TLS uses too
    sample = open(os.path.join(ROOT, "tests", "hatari", "sample.vcf"), encoding="latin-1").read().split("\n")
    k = next(n for n, l in enumerate(sample) if l.startswith("PHOTO"))
    photo = [sample[k]]
    for l in sample[k + 1:]:
        if not l.startswith(" "):
            break
        photo.append(l)
    photo = "\n".join(photo).replace("\r", "").replace("\n", "\r\n") + "\r\n"

    def card(i, name, with_photo=False):
        return ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:%s;Test;;;\r\nFN:Test %s\r\nEMAIL:t%d@example.org\r\n"
                "%sUID:FT-%03d\r\nEND:VCARD\r\n" % (name, name, i, photo if with_photo else "", i)).encode()

    def log_text():
        p = os.path.join(folder, "econtact.log")
        return open(p, encoding="latin-1").read() if os.path.exists(p) else ""

    def wait_log(start, what, secs=300):
        t0 = time.time()
        while time.time() - t0 < secs:
            time.sleep(2)
            if what in log_text()[start:]:
                return True
        return False

    def run(tag, expect, until=("secure:", "error"), then=None):
        """ECONTACT.PRG on the Falcon until the log has the refresh's end;
        then(r, start) after it, if given"""
        start = len(log_text())
        r = Rig(d, machine, auto="C:\\ECONTACT\\econtact.prg", network=True)
        try:
            t0 = time.time()
            while time.time() - t0 < 600:
                time.sleep(3)
                t = log_text()[start:]
                if expect in t and any(u in t or u in t.lower() for u in until):
                    break
            if then:
                then(r, start)
            time.sleep(5)
            r.shot(tag)
            r.ctrl("q")
            time.sleep(8)
        finally:
            r.stop()
        return log_text()[start:], time.time() - t0

    try:
        for _ in range(50):
            try:
                http("PROPFIND", base, b"", "application/xml")
                break
            except Exception:
                time.sleep(0.2)
        http("MKCOL", base + "book/", b'<?xml version="1.0" encoding="utf-8"?><D:mkcol xmlns:D="DAV:" '
             b'xmlns:C="urn:ietf:params:xml:ns:carddav"><D:set><D:prop><D:resourcetype><D:collection/>'
             b'<C:addressbook/></D:resourcetype><D:displayname>Book</D:displayname></D:prop></D:set></D:mkcol>',
             "application/xml")
        http("PUT", base + "book/a.vcf", card(1, "Before", with_photo=True))
        subprocess.run([CLI, folder, "carddav", "Server", "http://127.0.0.1:%d/" % hport, "alice", "wonderland"],
                       check=True, stdout=subprocess.DEVNULL)
        # meanwhile, on the server: one new
        http("PUT", base + "book/b.vcf", card(2, "OverTLS"))
        # the HTTPS one answers (and Python, with the test root, trusts it)
        ctx = ssl.create_default_context(cafile=ca)
        for _ in range(50):
            try:
                urllib.request.urlopen(urllib.request.Request("https://127.0.0.1:%d/" % sport), context=ctx)
                break
            except urllib.error.HTTPError:
                break
            except Exception:
                time.sleep(0.2)
        pem = os.path.join(folder, "cacert.pem")
        open(pem, "wb").write(open(os.path.join(ROOT, "binary", "cacert.pem"), "rb").read() + open(ca, "rb").read())
        subprocess.run([MKROOTS, pem, os.path.join(folder, "roots.dat")], check=True, capture_output=True)
        roots0 = open(os.path.join(folder, "roots.dat"), "rb").read()
        inf = open(os.path.join(folder, "econtact.inf"), "rb").read()
        inf = inf.replace(b"http://127.0.0.1:%d/" % hport, b"https://127.0.0.1:%d/" % sport)
        for k, v in ((b"LOG", b"1"), (b"SYNC_OPEN", b"1"), (b"FALCON", b"1"), (b"DSP", b"1" if dsp else b"0"),
                     (b"GATE", b""), (b"FOLDER", b"C:\\ECONTACT")):
            inf = re.sub(rb"\n" + k + rb"=[^\r\n]*", b"\n" + k + b"=" + v.replace(b"\\", b"\\\\"), inf)
        os.remove(os.path.join(folder, "econtact.inf"))
        open(os.path.join(hd, "econtact.inf"), "wb").write(inf)
        shutil.copy(os.path.join(ROOT, "binary", "econtact.prg"), folder)
        shutil.copy(os.path.join(ROOT, "binary", "tls.ovl"), folder)     # beside CACERT.PEM (7.4)
        picovl = os.path.exists(os.path.join(ROOT, "binary", "pic.ovl"))
        if picovl:
            shutil.copy(os.path.join(ROOT, "binary", "pic.ovl"), folder)

        def photo_after(r, start):
            # the Direct sync done (the DSP's curves), then the photo (its dither)
            r.key(0x47)		# Clr/Home: the first contact, with the photo
            wait_log(start, "shown after")

        first, took1 = run("1-first", "Server", then=photo_after)
        second, took2 = run("2-second", "Server")
        # the other way round: the photo first (its dither on the DSP), then
        # a Direct sync (^R: the curves on the DSP), PIC.OVL beside it
        fourth = ""
        if picovl:
            pak = os.path.join(folder, "cache", "photos.pak")
            if os.path.exists(pak):
                os.remove(pak)		# (made again, not taken from the cache)
            inf4 = open(os.path.join(hd, "econtact.inf"), "rb").read()
            open(os.path.join(hd, "econtact.inf"), "wb").write(re.sub(rb"\nSYNC_OPEN=[^\r\n]*", b"\nSYNC_OPEN=0", inf4))

            def sync_after(r, start):
                r.key(0x47)
                wait_log(start, "shown after")
                r.ctrl("r")
                wait_log(start, "secure:")

            fourth, _ = run("4-photo-then-sync", "", until=("",), then=sync_after)
            open(os.path.join(hd, "econtact.inf"), "wb").write(inf4)
        os.remove(os.path.join(folder, "tls.ovl"))
        third, took3 = run("3-no-tls-ovl", "tls.ovl", ("is missing",))
    finally:
        for p in procs:
            p.terminate()

    for name, t, took in (("first", first, took1), ("second", second, took2), ("third", third, took3)):
        print("%s run, %.0f s:" % (name, took))
        for l in t.splitlines():
            if re.search(r"secure:|certificate|on the DSP|on the 68030|self-test|seed|SEED|Server --|TLS.OVL", l):
                print("   " + l.strip()[:160])
    cache = ""
    for f in os.listdir(os.path.join(folder, "cache")):
        if f.endswith(".dat"):
            cache += open(os.path.join(folder, "cache", f), "rb").read().decode("utf-8", "replace")
    check("TLS.OVL loaded", re.search(r"TLS.OVL \d+\.\d+ loaded, \d+ KB", first) is not None)
    check("TLS 1.2 on the Falcon, with ECDHE and ECDSA", re.search(r"secure: TLS 1\.2, ECDHE?-ECDSA", first) is not None)
    check("the card new on the server came over it", "OverTLS" in cache)
    if dsp:
        check("the DSP passed its self-test", "elliptic-curve self-test" in first)
        check("the ECDSA signature checked on the DSP", re.search(r"ECDSA P-384 on the DSP", first) is not None)
        check("the key exchange on the DSP", re.search(r"(X25519|ECDH P-256|P-256) [^\n]*on the DSP", first) is not None
              or first.count("on the DSP") >= 2)
    else:
        check("the curves not on the DSP", "on the DSP" not in first)
    check("ROOTS.DAT given CACERT.PEM's date, in place",
          open(os.path.join(folder, "roots.dat"), "rb").read() != roots0)
    check("SIGS.DAT written", os.path.exists(os.path.join(folder, "sigs.dat")))
    check("the second time, the signature one checked before",
          re.search(r"[1-9]\d* of \d+ ECDSA signatures checked before", second) is not None)
    check("TLS the second time too", "secure: TLS 1.2" in second)
    check("without TLS.OVL: the log says it is missing", "TLS.OVL is missing" in third)
    crash = [os.path.join(dd, f) for dd, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    if picovl:
        p1 = first.find("shown after")
        check("photo after a Direct sync: TLS over the DSP first, then the photo",
              first.find("secure:") >= 0 and p1 > first.find("secure:") and "PIC.OVL loaded" in first)
        s4 = fourth.find("shown after")
        check("a Direct sync after a photo: the photo first, then TLS with the DSP's curves",
              s4 >= 0 and fourth.find("secure:", s4) > s4 and "on the DSP" in fourth[s4:])
    check("no crash (CRASH.LOG)", not crash)
    for c in crash:
        print(open(c, "rb").read().decode("latin-1"))
    print("screenshots in", os.path.join(d, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
