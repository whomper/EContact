/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pfkeep.c - files kept open while a sync runs (plat.h, pf_keep).
 *
 * Every Fopen takes an entry in TOS's small internal pool, and a long
 * Sync Contact Photos opened the address book's cache and the log
 * hundreds of times; on Erez's Falcon the pool ran out ("OUT OF INTERNAL
 * MEMORY"). While pf_keep is on, a file opened for reading or appending
 * stays open when it is closed, and the next open of it gets the same
 * handle (at the start for reading, at the end for appending).
 *
 * TOS writes a file's length into its folder only when it is closed, and
 * a second handle doesn't see what the first one added: so one handle a
 * file at a time, and before anything else is done with it (opened to
 * be written anew or in the other mode, its size or date asked for,
 * removed, renamed) it is closed. A file opened while its kept handle is
 * in use gets one of its own, closed as before. A file is also closed once it has been
 * open 30 seconds, so a crash loses no more than that of the log (and
 * of the cache, which fetches it again).
 */
#include <string.h>
#include "plat.h"

#define KEEP_N 8		/* a book's DAT, PHO and IDX, the log, some more */
#define KEEP_MS 30000UL

typedef struct {
	char path[200];		/* "" for a free entry */
	int mode, h, busy;	/* busy: opened, and not closed yet */
	unsigned long at;	/* when it was opened */
} KEPT;

static KEPT kept[KEEP_N];
static int keeping;

static int same(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

static void drop(KEPT *k)
{
	os_close(k->h);
	k->path[0] = 0;
}

/* closed, before something else is done with the file; one still in
   use is the caller's own, as before */
static void let_go(const char *path)
{
	int i;
	for (i = 0; i < KEEP_N; i++)
		if (kept[i].path[0] && !kept[i].busy && same(kept[i].path, path))
			drop(&kept[i]);
}

void pf_keep(int on)
{
	int i;
	keeping = on;
	if (!on)
		for (i = 0; i < KEEP_N; i++)
			if (kept[i].path[0] && !kept[i].busy)
				drop(&kept[i]);
}

int pf_open(const char *path, int mode)
{
	int i, h, free_i = -1, old_i = -1;
	if (mode == PF_WRITE || !keeping || strlen(path) >= sizeof(kept[0].path)) {
		let_go(path);
		return os_open(path, mode);
	}
	for (i = 0; i < KEEP_N; i++)
		if (kept[i].path[0] && kept[i].busy && same(kept[i].path, path))
			return os_open(path, mode);	/* in use: a handle of its own, closed as before */
	for (i = 0; i < KEEP_N; i++) {
		KEPT *k = &kept[i];
		if (!k->path[0] || !same(k->path, path))
			continue;
		if (k->mode == mode && pf_ms() - k->at < KEEP_MS &&
		    (mode == PF_READ ? pf_seek(k->h, 0) == 0 : os_seek_end(k->h) >= 0)) {
			k->busy = 1;
			return k->h;
		}
		drop(k);	/* the other mode, or open long enough */
	}
	h = os_open(path, mode);
	if (h < 0)
		return h;
	for (i = 0; i < KEEP_N; i++) {
		if (!kept[i].path[0]) {
			if (free_i < 0)
				free_i = i;
		} else if (!kept[i].busy && (old_i < 0 || kept[i].at - kept[old_i].at > 0x80000000UL)) {
			old_i = i;	/* the one opened longest ago */
		}
	}
	if (free_i < 0 && old_i >= 0) {
		drop(&kept[old_i]);
		free_i = old_i;
	}
	if (free_i >= 0) {
		KEPT *k = &kept[free_i];
		strcpy(k->path, path);
		k->mode = mode;
		k->h = h;
		k->busy = 1;
		k->at = pf_ms();
	}
	return h;
}

void pf_close(int h)
{
	int i;
	for (i = 0; i < KEEP_N; i++) {
		KEPT *k = &kept[i];
		if (k->path[0] && k->busy && k->h == h) {
			k->busy = 0;
			if (!keeping || pf_ms() - k->at >= KEEP_MS)
				drop(k);	/* written into its folder now and then */
			return;
		}
	}
	os_close(h);
}

long pf_size(const char *path)
{
	let_go(path);
	return os_size(path);
}

unsigned long pf_stamp(const char *path)
{
	let_go(path);
	return os_stamp(path);
}

int pf_exists(const char *path)
{
	let_go(path);
	return os_exists(path);
}

int pf_remove(const char *path)
{
	let_go(path);
	return os_remove(path);
}

int pf_rename(const char *from, const char *to)
{
	let_go(from);
	let_go(to);
	return os_rename(from, to);
}
