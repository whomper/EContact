#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""A picture server for the photo tests: serves DIR's files with an ETag
(their MD5), answers If-None-Match with 304, and logs each request (and
whether it carried a login) to stderr.

    picserver.py PORT DIR
"""
import hashlib
import http.server
import os
import sys

port, root = int(sys.argv[1]), sys.argv[2]


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = os.path.join(root, os.path.basename(self.path))
        auth = "auth" if self.headers.get("Authorization") else "noauth"
        if not os.path.exists(path):
            sys.stderr.write("GET %s 404 %s\n" % (self.path, auth))
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        data = open(path, "rb").read()
        etag = '"%s"' % hashlib.md5(data).hexdigest()
        if self.headers.get("If-None-Match") == etag:
            sys.stderr.write("GET %s 304 %s\n" % (self.path, auth))
            self.send_response(304)
            self.send_header("ETag", etag)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        sys.stderr.write("GET %s 200 %s\n" % (self.path, auth))
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("ETag", etag)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *a):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
