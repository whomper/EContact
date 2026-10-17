#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Fetch another AERA program's files from its GitHub release, for a test
that needs that program (guidelines 1.2: a test uses what Erez has, the
other program's latest release, not a build of its main).

    getrelease.py APP DIR FILE...            the latest release
    getrelease.py APP@v1.8 DIR FILE...       that release

APP is the repository's name under github.com/whomper (EHypView,
EContact...). The files land in DIR, and DIR/APP.VERSION says which
release they came from (a rig prints it, so a report names it). Standard
Python only.
"""
import os
import sys
import urllib.request

OWNER = "whomper"


def latest(app):
    """the tag of the latest release: GitHub redirects .../latest to it"""
    with urllib.request.urlopen("https://github.com/%s/%s/releases/latest" % (OWNER, app)) as r:
        url = r.geturl()
    if "/releases/tag/" not in url:
        sys.exit("%s: no release found" % app)
    return url.rsplit("/", 1)[1]


def fetch(spec, d, names):
    app, _, tag = spec.partition("@")
    tag = tag or latest(app)
    os.makedirs(d, exist_ok=True)
    for n in names:
        url = "https://github.com/%s/%s/releases/download/%s/%s" % (OWNER, app, tag, n)
        with urllib.request.urlopen(url) as r, open(os.path.join(d, n), "wb") as f:
            f.write(r.read())
    with open(os.path.join(d, app.upper() + ".VERSION"), "w") as f:
        f.write(tag + "\n")
    print("%s %s: %s" % (app, tag, " ".join(names)))
    return tag


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    fetch(sys.argv[1], sys.argv[2], sys.argv[3:])
