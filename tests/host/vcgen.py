#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""vCards to compare two versions of the parser on (tools/parser_diff.sh):

    tests/host/vcgen.py OUT [N]

Into OUT: N made-up cards (default 3000) with every property, parameter,
charset and fold vcard.c reads, as many again changed a byte here and
there (from those and tests/fuzz/seeds/vcard), addresses and birthdays at
their limits, NULs and line ends where they don't belong, the sample
cards and 600 cards as tests/hatari/quiet_start.py makes them. The same
files every time (fixed seeds).
"""
import base64
import glob
import os
import random
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "hatari"))

NAMES = ["N", "FN", "UID", "NICKNAME", "ORG", "TITLE", "NOTE", "CATEGORIES", "BDAY", "EMAIL", "TEL", "ADR",
         "URL", "X-ABSHOWAS", "X-ADDRESSBOOKSERVER-KIND", "KIND", "X-ADDRESSBOOKSERVER-MEMBER", "MEMBER",
         "PHOTO", "LOGO", "KEY", "SOUND", "X-ECONTACT-PHOTO", "X-ABLABEL", "X-ABDATE", "IMPP", "REV", "PRODID",
         "VERSION", "X-APPLE-OMIT-YEAR", "item", "begin", "end", "fn", "tel", "Email", "nOte"]
TYPES = ["home", "work", "cell", "CELL", "voice", "pref", "fax", "iphone", "IPHONE", "main", "pager", "other",
         "school", "internet", "x-mobileme", "INTERNET", "\"home,work\"", "mobile", "jpeg", "JPEG"]
VALS = ["Erez", "Yaary", "a@b.example", "+972 54-555-0101", "tel:+1 555", ";;1 Herzl St;Tel Aviv;;6100000;Israel",
        "\u05d0\u05e8\u05d6", "Caf\u00e9 M\u00fcller", "x\\,y\\;z\\nq\\\\", "COMPANY", "group", "org",
        "urn:uuid:ABC-123", "1970-01-21", "--05-17", "19800517", "1604-03-02", "_$!<Mobile>!$_", "_$!<HomeFAX>!$_",
        "My Label", "", " ", "http://example.com/x", "?", "1A2B3C4D", "=C3=A9t=C3=A9", "Family,Work",
        "a;b;c;d;e;f;g;h", "\U0001f600 emoji", "\x01\x02ctl", "q\"uote", "very " * 30 + "long"]


def made(out, n):
    R = random.Random(42)

    def prop():
        g = R.choice(["", "", "", "item1.", "item2.", "ITEM1.", "x.y.", "item3."])
        name = R.choice(NAMES)
        params = []
        for _ in range(R.choice([0, 0, 1, 2, 3])):
            k = R.choice(["TYPE", "type", "CHARSET", "ENCODING", "X-APPLE-OMIT-YEAR", "VALUE", "LABEL", "PREF", ""])
            if k == "CHARSET":
                v = R.choice(["utf-8", "UTF-8", "iso-8859-1", "ISO-8859-8", "windows-1255", "cp862", "bogus",
                              "\"utf-8\""])
            elif k == "ENCODING":
                v = R.choice(["QUOTED-PRINTABLE", "quoted-printable", "b", "BASE64", "8bit"])
            elif k == "X-APPLE-OMIT-YEAR":
                v = R.choice(["1604", "1970", "x"])
            elif k == "VALUE":
                v = R.choice(["uri", "URI", "date", "text"])
            elif k == "":
                params.append(R.choice(TYPES))
                continue
            else:
                v = ",".join(R.choice(TYPES) for _ in range(R.choice([1, 1, 2, 3])))
            params.append("%s=%s" % (k, v))
        val = R.choice(VALS)
        if name == "PHOTO" and R.random() < 0.7:
            val = base64.b64encode(bytes(R.randrange(256) for _ in range(R.choice([3, 50, 300])))).decode()
            if R.random() < 0.5:
                params.append("ENCODING=b")
        return g + name + "".join(";" + p for p in params) + ":" + val

    def fold(line):
        if len(line) > 40 and R.random() < 0.4:
            parts = []
            while len(line) > 40:
                k = R.randrange(10, 40)
                parts.append(line[:k])
                line = line[k:]
            parts.append(line)
            return ("\r\n" + R.choice([" ", "\t"])).join(parts)
        if line.endswith("=") or (R.random() < 0.1 and "QUOTED" in line.upper()):
            return line + "=\r\n" + "more=41"
        return line

    def card():
        eol = R.choice(["\r\n", "\r\n", "\n", "\r"])
        lines = ["BEGIN:VCARD", "VERSION:" + R.choice(["3.0", "2.1", "4.0"])]
        for _ in range(R.randrange(0, 25)):
            lines.append(fold(prop()))
        if R.random() < 0.9:
            lines.append("END:VCARD")
        return eol.join(lines) + eol

    def mutate(b):
        b = bytearray(b)
        for _ in range(R.randrange(1, 8)):
            if not b:
                break
            op, i = R.randrange(4), R.randrange(len(b))
            if op == 0:
                b[i] = R.randrange(256)
            elif op == 1:
                del b[i:i + R.randrange(1, 20)]
            elif op == 2:
                b[i:i] = R.choice([b":", b";", b"=", b"\r\n", b"\r\n ", b".", b"\"", b"\\", b"\xc3", b"\xff"])
            else:
                b[i:i] = b[R.randrange(len(b)):][:R.randrange(1, 40)]
        return bytes(b)

    base = []
    for i in range(n):
        t = "".join(card() for _ in range(R.choice([1, 1, 1, 2, 3])))
        if R.random() < 0.15:
            t = "junk before\r\n" + t + "trailing"
        data = t.encode("utf-8", "surrogatepass") if R.random() < 0.8 else t.encode("latin-1", "replace")
        base.append(data)
        open(os.path.join(out, "g%05d.vcf" % i), "wb").write(data)
    for d in ("seeds", "found"):
        for f in glob.glob(os.path.join(ROOT, "tests", "fuzz", d, "vcard", "*")):
            if os.path.isfile(f):
                base.append(open(f, "rb").read())
                shutil.copy(f, os.path.join(out, "%s-%s.vcf" % (d, os.path.basename(f))))
    for i in range(n):
        open(os.path.join(out, "m%05d.vcf" % i), "wb").write(mutate(R.choice(base)))


def limits(out):
    """addresses and birthdays at the edges of what vcard.c keeps"""
    R = random.Random(5)

    def part():
        k = R.choice([0, 0, 1, 5, 50, 120, 198, 199, 200, 201, 260, 390, 420])
        return "".join(R.choice(["a", "b", "c", " ", "X", "Y", "Z", "\\n", "\\,", "\\;", "."]) for _ in range(k))

    for i in range(1500):
        adr = ";".join(part() for _ in range(R.choice([1, 3, 6, 7, 9])))
        bd = R.choice(["1980-05-17", "--05-17", "--0517", "19800517", "0001-01-01", "9999-12-31", "1604-03-02",
                       "2000-13-01", "2000-00-10", "12345-01-01", " 1980-05-17T10:00", "1980-5-7", "--5-7", "x", "",
                       "1980-05-17 ", "-1980-05-17", "80-05-17"])
        omit = R.choice(["", ";X-APPLE-OMIT-YEAR=1604", ";X-APPLE-OMIT-YEAR=1980", ";x-apple-omit-year=0001"])
        tel = R.choice(["fax,home", "FAX", "cell", "IPHONE", "pager", "main,work", "x", "\"home,fax\"", "work,,fax",
                        "home,"])
        mail = R.choice(["x-mobileme", "internet", "home,x-mobileme", "school", "other", "WORK", ""])
        t = ("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Edge %d\r\nitem1.ADR;type=HOME:%s\r\nBDAY%s:%s\r\n"
             "TEL;TYPE=%s:+1 5\r\nEMAIL;type=%s:e@x\r\nEND:VCARD\r\n" % (i, adr, omit, bd, tel, mail))
        open(os.path.join(out, "e%04d.vcf" % i), "w").write(t)


def odd(out):
    """NULs, line ends and quoted-printable breaks where they don't belong"""
    R = random.Random(9)
    base = [b"BEGIN:VCARD\r\nVERSION:2.1\r\nN;CHARSET=utf-8:Doe;J\x00ohn\r\nFN:J\x00D\r\nTEL;CE\x00LL;HOME:12\r\n"
            b"NOTE;ENCODING=QUOTED-PRINTABLE:a=\r\nb=3D=\r\nc\r\nEMAIL;TY\x00PE=work:x@y\r\n"
            b"ADR;TYPE=home:;;St\x00reet;City\r\nEND:VCARD\r\n",
            b"BEGIN:VCA", b"BEGIN:VCARD", b"BEGIN:VCARD\r\nFN:x", b"BEGIN:VCARD\r\nN:a\r\nEND",
            b"BEGIN:\x00VCARD\r\nFN:x\r\nEND:VCARD", b"BEGIN:VCARD\r\nNOTE;ENCODING=QUOTED-PRINTABLE:a=",
            b"BEGIN:VCARD\r\nNOTE;ENCODING=QUOTED-PRINTABLE:a=\r\n",
            b"BEGIN:VCARD\r\nNOTE;ENCODING=QUOTED-PRINTABLE:a=\r\n =41=\r\nb\r\nEND:VCARD\r\n",
            b"BEGIN:VCARD\r\n\r\n\r\nFN:x\r\n \r\n\tcont\r\nEND:VCARD\r\n", b"BEGIN:VCARD\r\nFN:x\r\n  \r\nEND:VCARD",
            b"BEGIN:VCARD\nX-APPLE-OMIT-YEAR=1604;BDAY;X-APPLE-OMIT-YEAR=1604:1604-05-17\nEND:VCARD\n",
            b"BEGIN:VCARD\r\nTEL;type=\"ce;ll\":1\r\nEMAIL;\"type=home\":a\r\nEND:VCARD\r\n",
            b"BEGIN:VCARD\r\nPHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQ\r\n SkZJRg\r\n ABAQ\r\nFN:Photo\r\nEND:VCARD\r\n",
            b"BEGIN:VCARD\r\nPHOTO;ENCOD\r\n ING=b:/9j/4AAQ\r\n SkZJRg\r\nFN:Split\r\nEND:VCARD\r\n",
            b"BEGIN:VCARD\r\nPHOTO;ENCODING=QUOTED-PRINTABLE:a=\r\n b=\r\nFN:Swallowed\r\nEND:VCARD\r\n"]
    for i, b in enumerate(base):
        open(os.path.join(out, "b%02d.vcf" % i), "wb").write(b)
    for i in range(800):
        b = bytearray(R.choice(base))
        for _ in range(R.randrange(1, 5)):
            j = R.randrange(len(b) + 1)
            b[j:j] = bytes([R.choice([0, 0, 13, 10, 32, 9, 61, 58, 59, 34])])
        open(os.path.join(out, "n%03d.vcf" % i), "wb").write(bytes(b))


def main():
    out = os.path.abspath(sys.argv[1])
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    made(out, n)
    limits(out)
    odd(out)
    shutil.copy(os.path.join(ROOT, "tests", "hatari", "sample.vcf"), os.path.join(out, "sample.vcf"))
    import quiet_start
    quiet_start.cards(os.path.join(out, "quiet600.vcf"), 600)


if __name__ == "__main__":
    main()
