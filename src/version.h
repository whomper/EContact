/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * version.h - EContact's version: VERSION holds the last release
 * (major.minor, e.g. "1.33"), which a release build shows; other builds
 * show "1.33-dev". The Makefile writes version.c; tools/bump.py starts a
 * release.
 */
#ifndef VERSION_H
#define VERSION_H
extern const char app_version[];	/* "1.33" or "1.33-dev" */
/* the parser's sources, summed by the Makefile: BOOKnnnn.IDX is good
   while they are the same */
extern const char parse_id[];
#endif
