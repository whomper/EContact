#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""Writes tests/fuzz/seeds/: the starting inputs for make fuzz, answers as
contacts servers give them, vCards and photos. For fuzz_dav the first
byte picks the request (see fuzz_dav.c); the rest is what the server says.

tests/fuzz/found/ holds the inputs that once broke a parser, as the fuzzer
saved them; make test runs them all again (common/tests/fuzz/replay.c)."""
import base64, gzip, io, os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.join(HERE, "..", "hatari", "sample.vcf")


def put(target, name, data):
    d = os.path.join(HERE, "seeds", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())


def http(status, body, headers=(), chunked=False, gz=False):
    b = body.encode() if isinstance(body, str) else body
    h = ["HTTP/1.1 %s" % status, "Content-Type: application/xml; charset=utf-8"] + list(headers)
    if gz:
        b = gzip.compress(b)
        h.append("Content-Encoding: gzip")
    if chunked:
        h.append("Transfer-Encoding: chunked")
        parts = [b[i:i + 40] for i in range(0, len(b), 40)]
        b = b"".join(b"%x\r\n" % len(p) + p + b"\r\n" for p in parts) + b"0\r\n\r\n"
    else:
        h.append("Content-Length: %d" % len(b))
    return ("\r\n".join(h) + "\r\n\r\n").encode() + b


def ms(*responses):
    return ('<?xml version="1.0" encoding="utf-8"?>\n<d:multistatus xmlns:d="DAV:" '
            'xmlns:card="urn:ietf:params:xml:ns:carddav" xmlns:cs="http://calendarserver.org/ns/">' +
            "".join(responses) + "</d:multistatus>")


def resp(href, props, status="HTTP/1.1 200 OK"):
    return "<d:response><d:href>%s</d:href><d:propstat><d:prop>%s</d:prop><d:status>%s</d:status></d:propstat></d:response>" % (href, props, status)


def jpeg(w, h, **kw):
    im = Image.new("RGB", (w, h))
    for y in range(h):
        for x in range(w):
            im.putpixel((x, y), ((x * 255) // max(w - 1, 1), (y * 255) // max(h - 1, 1), 128))
    b = io.BytesIO()
    im.save(b, "JPEG", quality=80, **kw)
    return b.getvalue()


def main():
    cards = open(SAMPLE, newline="").read()
    first = cards.split("END:VCARD")[0] + "END:VCARD\r\n"
    photo = base64.b64encode(jpeg(32, 32)).decode()
    with_photo = ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Carter;Maya;;;\r\nFN:Maya Carter\r\nORG:Pixel Press;\r\n"
                  "EMAIL;type=INTERNET;type=WORK;type=pref:maya@example.com\r\nTEL;type=CELL:+1 555 0100\r\n"
                  "item1.ADR;type=HOME:;;1 Main St;Springfield;IL;62701;USA\r\nitem1.X-ABLabel:_$!<Home>!$_\r\n"
                  "BDAY:1984-02-29\r\nNOTE:Line one\\nline two\\, with a comma\r\nCATEGORIES:Family,Work\r\n"
                  "PHOTO;ENCODING=b;TYPE=JPEG;X-ABCROP-RECTANGLE=ABClipRect_1&4&4&24&24&abc:" +
                  "\r\n ".join(photo[i:i + 74] for i in range(0, len(photo), 74)) + "\r\nUID:maya-1\r\nEND:VCARD\r\n")
    group = ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Club;;;;\r\nFN:Club\r\nX-ADDRESSBOOKSERVER-KIND:group\r\n"
             "X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:maya-1\r\nUID:club-1\r\nEND:VCARD\r\n")
    v21 = ("BEGIN:VCARD\r\nVERSION:2.1\r\nN;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:=D7=A9=D7=9C;Dana\r\n"
           "TEL;CELL:0501234567\r\nPHOTO;VALUE=URI:https://example.com/p.jpg\r\nEND:VCARD\r\n")
    put("vcard", "sample", cards)
    put("vcard", "one", first)
    put("vcard", "photo", with_photo)
    put("vcard", "group", group)
    put("vcard", "v21", v21)
    put("jpeg", "baseline", jpeg(48, 40))
    put("jpeg", "progressive", jpeg(40, 48, progressive=True))
    put("jpeg", "small", jpeg(8, 8))
    g = Image.new("L", (32, 24), 90)
    b = io.BytesIO()
    g.save(b, "JPEG", quality=70)
    put("jpeg", "gray", b.getvalue())
    home = "/addressbooks/me/"
    principal = http("207 Multi-Status", ms(resp("/", "<d:current-user-principal><d:href>/principals/me/</d:href></d:current-user-principal>")))
    homeset = http("207 Multi-Status", ms(resp("/principals/me/", "<card:addressbook-home-set><d:href>%s</d:href></card:addressbook-home-set>" % home)))
    books = http("207 Multi-Status", ms(
        resp(home, "<d:resourcetype><d:collection/></d:resourcetype>"),
        resp(home + "card/", "<d:resourcetype><d:collection/><card:addressbook/></d:resourcetype>"
             "<d:displayname>Contacts &amp; more</d:displayname><cs:getctag>c1</cs:getctag>"
             "<d:current-user-privilege-set><d:privilege><d:write/></d:privilege></d:current-user-privilege-set>")), chunked=True)
    put("dav", "discover", b"\x00" + principal + homeset + books)
    put("dav", "discover-gateway", b"\x80" + principal + homeset + books)
    put("dav", "discover-401", b"\x00" + http("401 Unauthorized", "no", ['WWW-Authenticate: Digest realm="dav", nonce="n1", qop="auth"']) + principal + homeset + books)
    put("dav", "ctag", b"\x01" + http("207 Multi-Status", ms(resp(home + "card/", "<cs:getctag>c2</cs:getctag>")), gz=True))
    items = "".join(resp(home + "card/%d.vcf" % i, "<d:getetag>\"e%d\"</d:getetag>" % i) for i in range(6))
    put("dav", "list", b"\x02" + http("207 Multi-Status", ms(items)))
    put("dav", "list-small-budget", b"\x42" + http("207 Multi-Status", ms(items), chunked=True))
    esc = lambda t: t.replace("&", "&amp;").replace("<", "&lt;")
    data = ms(resp(home + "card/a.vcf", "<d:getetag>\"e1\"</d:getetag><card:address-data>%s</card:address-data>" % esc(with_photo)),
              resp(home + "card/b.vcf", "<d:getetag>\"e2\"</d:getetag><card:address-data>%s</card:address-data>" % esc(group)))
    put("dav", "multiget", b"\x03" + http("207 Multi-Status", data))
    put("dav", "multiget-partial", b"\x0b" + http("207 Multi-Status", data, gz=True))
    put("dav", "put", b"\x04" + http("201 Created", "", ['ETag: "e9"']))
    put("dav", "delete", b"\x05" + http("204 No Content", ""))
    put("dav", "get", b"\x06" + http("200 OK", with_photo, ["Content-Type: text/vcard", 'ETag: "e1"']))


if __name__ == "__main__":
    main()
