/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * photomig.c - EContact 1.50 keeps the contacts' pictures in one file,
 * CACHE\PHOTOS.PAK (photopack.c), where 1.49 and before kept a file each in
 * CACHE\PHOTOS. The first time there is no pack, the old files go into it
 * as they are, and are removed with their folder.
 *
 * TEMPORARY: it goes once Erez has run 1.50 (the hub asks for its removal
 * a few days after the release). Its one call is in photopack.c's load();
 * its test is the block marked TEMPORARY in tests/integration.sh.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "photo.h"
#include "photopack.h"
#include "plat.h"
#include "util.h"
#include "conn.h"

void photo_migrate(const char *pack);

typedef struct {
	char (*names)[14];
	int n, max;
} PICS;

static int one(const char *name, long size, void *ud)
{
	PICS *p = ud;
	int l = (int)strlen(name);
	(void)size;
	if (l < 5 || l > 12 || strcasecmp(name + l - 4, ".PIC"))
		return 0;
	if (p->n >= p->max) {
		int max = p->max ? p->max * 2 : 64;
		char (*nn)[14] = realloc(p->names, max * sizeof(*nn));
		if (!nn)
			return 1;
		p->names = nn;
		p->max = max;
	}
	str_copy(p->names[p->n++], name, 14);
	return 0;
}

static const char *field(const char *t, const char *key, char *out, int size)
{
	const char *k = strstr(t, key);
	int n;
	out[0] = 0;
	if (!k)
		return out;
	k += strlen(key);
	n = (int)strcspn(k, "\n");
	if (n >= size)
		n = size - 1;
	memcpy(out, k, n);
	out[n] = 0;
	return out;
}

void photo_migrate(const char *pack)
{
	char dir[220], path[240], v[110], *e;
	PICS p;
	int i, moved = 0, was;
	str_copy(dir, pack, sizeof(dir));
	e = strrchr(dir, PF_SEP);
	if (!e)
		return;
	str_copy(e + 1, "PHOTOS", sizeof(dir) - (int)(e + 1 - dir));
	memset(&p, 0, sizeof(p));
	pf_list(dir, one, &p);
	was = pack_batch(1);
	for (i = 0; i < p.n; i++) {
		char *t, *blank;
		long len;
		PKENT k;
		path_join(path, sizeof(path), dir, p.names[i]);
		if (!(t = pf_load(path, &len)))
			continue;
		blank = strstr(t, "\n\n");
		if (len > 12 && !strncmp(t, "ECPHOTO 1\n", 10) && blank &&
		    (len - (blank + 2 - t) == 0 || len - (blank + 2 - t) == (long)PHOTO_N * PHOTO_N * 2)) {
			*blank = 0;	/* (the fields read from the text alone) */
			memset(&k, 0, sizeof(k));
			k.name = strtoul(p.names[i] + 1, 0, 16);
			k.book = (short)atoi(field(t, "\nBOOK=", v, sizeof(v)));
			k.key = strtoul(field(t, "\nKEY=", v, sizeof(v)), 0, 16);
			k.cetag = pack_hash(field(t, "\nCETAG=", v, sizeof(v)));
			k.link = (unsigned char)atoi(field(t, "\nLINK=", v, sizeof(v)));
			k.failed = !strcmp(field(t, "\nSTATE=", v, sizeof(v)), "FAIL");
			k.why = (unsigned char)atoi(field(t, "\nWHY=", v, sizeof(v)));
			k.dec = (unsigned char)atoi(field(t, "\nDEC=", v, sizeof(v)));
			*blank = '\n';
			if (pack_put(&k, t, len, 0, 0))
				moved++;
		}
		free(t);
	}
	if (pack_flush()) {
		for (i = 0; i < p.n; i++) {
			path_join(path, sizeof(path), dir, p.names[i]);
			pf_remove(path);
		}
		pf_rmdir(dir);
		if (moved) {
			char m[80];
			snprintf(m, sizeof(m), "%d pictures moved from CACHE\\PHOTOS into PHOTOS.PAK", moved);
			conn_log("Photo", " -- ", m, (long)strlen(m));
		}
	}
	pack_batch(was);
	free(p.names);
}
