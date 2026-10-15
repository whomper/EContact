#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A CardDAV server that does partial retrieval (RFC 6352 8.4), for the
tests: a proxy in front of Radicale (which ignores it) that answers an
addressbook-multiget asking for some properties with only those, and a
PHOTO asked for with novalue="yes" without its value, as a server that
does it would. Logs each REPORT to stderr:

    REPORT partial 40 hrefs, 31200 bytes
    REPORT whole 3 hrefs, 61000 bytes

    partialproxy.py PORT UPSTREAM [--refuse | --blank-photos]

--refuse answers a partial request with 400, as a server might that
doesn't take it. --blank-photos sends every line but the photo's value,
as a server might that does only half of it.
"""
import html
import http.client
import http.server
import re
import sys
import urllib.parse

port, upstream = int(sys.argv[1]), urllib.parse.urlparse(sys.argv[2])
refuse = "--refuse" in sys.argv
blank = "--blank-photos" in sys.argv
HOP = {"connection", "keep-alive", "transfer-encoding", "content-length", "content-encoding"}


def unfold(card):
    lines = []
    for ln in card.replace("\r\n", "\n").split("\n"):
        if ln[:1] in (" ", "\t") and lines:
            lines[-1] += ln[1:]
        elif ln:
            lines.append(ln)
    return lines


def name_of(line):
    head = re.split(r"[;:]", line, 1)[0]
    return head.split(".")[-1].upper()


def filtered(card, want, novalue):
    out = []
    for ln in unfold(card):
        n = name_of(ln)
        if n in ("BEGIN", "END"):
            out.append(ln)
        elif n in novalue:
            out.append(ln.split(":", 1)[0] + ":")
        elif blank or n in want:
            out.append(ln)
    return "\r\n".join(out) + "\r\n"


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle_any(self):
        body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        text = body.decode("utf-8", "replace")
        props = re.findall(r'<[\w]*:?prop\s+name="([^"]+)"(\s+novalue="yes")?', text)
        partial = self.command == "REPORT" and "multiget" in text and props
        if partial and refuse:
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        c = http.client.HTTPConnection(upstream.hostname, upstream.port)
        hdrs = {k: v for k, v in self.headers.items() if k.lower() not in HOP and k.lower() != "host"}
        hdrs["Host"] = "%s:%d" % (upstream.hostname, upstream.port)
        hdrs["Accept-Encoding"] = "identity"
        c.request(self.command, self.path, body=body, headers=hdrs)
        r = c.getresponse()
        data = r.read()
        if partial and r.status == 207:
            want = {n.upper() for n, nv in props if not nv}
            novalue = {n.upper() for n, nv in props if nv}

            def sub(m):
                card = html.unescape(m.group(2))
                return m.group(1) + html.escape(filtered(card, want, novalue), quote=False) + m.group(3)
            data = re.sub(r"(<[\w]*:?address-data[^>]*>)(.*?)(</[\w]*:?address-data>)", sub,
                          data.decode("utf-8"), flags=re.S).encode("utf-8")
        if self.command == "REPORT" and "multiget" in text:
            sys.stderr.write("REPORT %s %d hrefs, %d bytes\n" %
                             ("partial" if partial else "whole", text.count("href>") // 2, len(data)))
            sys.stderr.flush()
        self.send_response(r.status, r.reason)
        for k, v in r.getheaders():
            if k.lower() not in HOP:
                self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        c.close()

    do_GET = do_PUT = do_DELETE = do_PROPFIND = do_REPORT = do_OPTIONS = do_MKCOL = do_HEAD = handle_any

    def log_message(self, *a):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
