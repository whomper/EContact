#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Falcon mode in Hatari: TLS on the Falcon itself, with an ECDSA server,
the curves on the DSP (common/ui/dsp_ec.c), no gateway:

    tests/hatari/falcon_tls.py WORK [MACHINE] [--no-dsp]

A Radicale serves HTTPS with a certificate of a P-256 key signed by a
P-384 test root (added to CACERT.PEM, ROOTS.DAT made by build/mkroots
without CACERT.PEM's date). A second Radicale on the same store, plain
HTTP, lets card-cli make the account and its cache first; the INF then
points at the HTTPS one. ECONTACT.PRG starts twice on the Falcon (TT RAM,
the DSP: machines.py), refreshing as its window opens:

- the first time: a card new on the server comes over TLS 1.2, the key
  exchange and the root's ECDSA signature on the DSP, ROOTS.DAT given
  CACERT.PEM's date in place (PF_UPDATE), SIGS.DAT written;
- the second time: the signature is one "checked before" (SIGS.DAT);
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
                               "--server-ssl", "--server-certificate", chain, "--server-key", key] + common,
                              stdout=subprocess.DEVNULL, stderr=open(os.path.join(d, "radicale-tls.log"), "w"))]
    base = "http://127.0.0.1:%d/alice/" % hport
    auth = "Basic " + base64.b64encode(b"alice:wonderland").decode()

    def http(method, url, data=None, ctype="text/vcard"):
        rq = urllib.request.Request(url, data=data, method=method,
                                    headers={"Content-Type": ctype, "Authorization": auth})
        return urllib.request.urlopen(rq).read()

    def card(i, name):
        return ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:%s;Test;;;\r\nFN:Test %s\r\nEMAIL:t%d@example.org\r\n"
                "UID:FT-%03d\r\nEND:VCARD\r\n" % (name, name, i, i)).encode()

    def log_text():
        p = os.path.join(folder, "ECONTACT.LOG")
        return open(p, encoding="latin-1").read() if os.path.exists(p) else ""

    def run(tag, expect):
        """ECONTACT.PRG on the Falcon until the log has the refresh's end"""
        start = len(log_text())
        r = Rig(d, machine, auto="C:\\ECONTACT\\ECONTACT.PRG", network=True)
        try:
            t0 = time.time()
            while time.time() - t0 < 600:
                time.sleep(3)
                t = log_text()[start:]
                if expect in t and ("secure:" in t or "error" in t.lower()):
                    break
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
        http("PUT", base + "book/a.vcf", card(1, "Before"))
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
        pem = os.path.join(folder, "CACERT.PEM")
        open(pem, "wb").write(open(os.path.join(ROOT, "binary", "CACERT.PEM"), "rb").read() + open(ca, "rb").read())
        subprocess.run([MKROOTS, pem, os.path.join(folder, "ROOTS.DAT")], check=True, capture_output=True)
        roots0 = open(os.path.join(folder, "ROOTS.DAT"), "rb").read()
        inf = open(os.path.join(folder, "ECONTACT.INF"), "rb").read()
        inf = inf.replace(b"http://127.0.0.1:%d/" % hport, b"https://127.0.0.1:%d/" % sport)
        for k, v in ((b"LOG", b"1"), (b"SYNC_OPEN", b"1"), (b"FALCON", b"1"), (b"DSP", b"1" if dsp else b"0"),
                     (b"GATE", b""), (b"FOLDER", b"C:\\ECONTACT")):
            inf = re.sub(rb"\n" + k + rb"=[^\r\n]*", b"\n" + k + b"=" + v.replace(b"\\", b"\\\\"), inf)
        os.remove(os.path.join(folder, "ECONTACT.INF"))
        open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
        shutil.copy(os.path.join(ROOT, "binary", "ECONTACT.PRG"), folder)

        first, took1 = run("1-first", "Server")
        second, took2 = run("2-second", "Server")
    finally:
        for p in procs:
            p.terminate()

    for name, t, took in (("first", first, took1), ("second", second, took2)):
        print("%s run, %.0f s:" % (name, took))
        for l in t.splitlines():
            if re.search(r"secure:|certificate|on the DSP|on the 68030|self-test|seed|SEED|Server --", l):
                print("   " + l.strip()[:160])
    cache = ""
    for f in os.listdir(os.path.join(folder, "CACHE")):
        if f.endswith(".DAT"):
            cache += open(os.path.join(folder, "CACHE", f), "rb").read().decode("utf-8", "replace")
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
          open(os.path.join(folder, "ROOTS.DAT"), "rb").read() != roots0)
    check("SIGS.DAT written", os.path.exists(os.path.join(folder, "SIGS.DAT")))
    check("the second time, the signature one checked before",
          re.search(r"[1-9]\d* of \d+ ECDSA signatures checked before", second) is not None)
    check("TLS the second time too", "secure: TLS 1.2" in second)
    crash = [os.path.join(dd, f) for dd, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    for c in crash:
        print(open(c, "rb").read().decode("latin-1"))
    print("screenshots in", os.path.join(d, "shots"))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
