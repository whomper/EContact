/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pfkeep.c - the files EContact has open (plat.h: pf_open, pf_keep,
 * pf_park).
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
 *
 * An accessory's files belong to the program in front: when that program
 * ends, TOS closes them, and their numbers go to the next files opened,
 * which would then take our writes (the guidelines, 2.2). So the
 * accessory lets go of every file before each event call (pf_park, from
 * ui/evwrap.c), those in use too: a handle pf_open gives is EContact's
 * own number (FH + the entry), and a file let go is opened again where
 * it was at its next read, write or seek. Kept files are simply closed.
 */
#include <string.h>
#include "plat.h"

#define ENT_N 24		/* open at once: a sync's cache files, the log, a photo, ... */
#define FH 0x4000		/* our handles: FH + the entry; below, the machine's own */
#define KEEP_MS 30000UL

typedef struct {
	char path[200];		/* "" for a free entry */
	int mode, h;		/* h: the machine's handle, -1 while let go */
	int busy;		/* opened, and not closed yet */
	int keep;		/* may stay open when closed (read or append, keeping) */
	long pos;		/* where it was when let go */
	unsigned long at;	/* when it was opened */
} ENT;

static ENT ent[ENT_N];
static int keeping;
long pf_opens;

static int same(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

static int os_open_counted(const char *path, int mode)
{
	pf_opens++;
	return os_open(path, mode);
}

static void drop(ENT *e)
{
	if (e->h >= 0)
		os_close(e->h);
	e->h = -1;
	e->path[0] = 0;
}

/* closed, before something else is done with the file; one still in
   use is the caller's own, as before */
static void let_go(const char *path)
{
	int i;
	for (i = 0; i < ENT_N; i++)
		if (ent[i].path[0] && !ent[i].busy && same(ent[i].path, path))
			drop(&ent[i]);
}

void pf_keep(int on)
{
	int i;
	keeping = on;
	if (!on)
		for (i = 0; i < ENT_N; i++)
			if (ent[i].path[0] && !ent[i].busy)
				drop(&ent[i]);
}

void pf_park(void)
{
	int i;
	for (i = 0; i < ENT_N; i++) {
		ENT *e = &ent[i];
		if (!e->path[0] || e->h < 0)
			continue;
		if (!e->busy) {
			drop(e);	/* kept: opened again by the next pf_open */
			continue;
		}
		e->pos = os_tell(e->h);
		os_close(e->h);
		e->h = -1;
	}
}

/* the machine's handle for h, the file opened again if it was let go */
static int real(int h)
{
	ENT *e;
	if (h < FH || h >= FH + ENT_N)
		return h;		/* the machine's own (the table was full) */
	e = &ent[h - FH];
	if (e->h >= 0 || !e->path[0])
		return e->h;
	/* written: opened as it is now, not made anew, and where it was */
	e->h = os_open_counted(e->path, e->mode == PF_READ ? PF_READ : PF_UPDATE);
	if (e->h >= 0 && e->pos > 0 && os_seek(e->h, e->pos) != e->pos) {
		os_close(e->h);
		e->h = -1;
	}
	return e->h;
}

int pf_open(const char *path, int mode)
{
	int i, h, free_i = -1, old_i = -1, keep;
	keep = keeping && (mode == PF_READ || mode == PF_APPEND) && strlen(path) < sizeof(ent[0].path);
	if (!keep) {
		let_go(path);
	} else {
		for (i = 0; i < ENT_N; i++) {
			ENT *e = &ent[i];
			if (!e->path[0] || !same(e->path, path))
				continue;
			if (e->busy)
				break;		/* in use: a handle of its own, closed as before */
			if (e->keep && e->mode == mode && pf_ms() - e->at < KEEP_MS && e->h >= 0 &&
			    (mode == PF_READ ? os_seek(e->h, 0) == 0 : os_seek_end(e->h) >= 0)) {
				e->busy = 1;
				return FH + i;
			}
			drop(e);	/* the other mode, or open long enough */
		}
	}
	for (i = 0; i < ENT_N; i++) {
		if (!ent[i].path[0]) {
			if (free_i < 0)
				free_i = i;
		} else if (!ent[i].busy && (old_i < 0 || ent[i].at - ent[old_i].at > 0x80000000UL)) {
			old_i = i;	/* the one opened longest ago */
		}
	}
	if (free_i < 0 && old_i >= 0) {
		drop(&ent[old_i]);
		free_i = old_i;
	}
	h = os_open_counted(path, mode);
	if (h < 0 || free_i < 0 || strlen(path) >= sizeof(ent[0].path))
		return h;		/* (no room: the machine's own handle, never let go) */
	{
		ENT *e = &ent[free_i];
		strcpy(e->path, path);
		e->mode = mode;
		e->h = h;
		e->busy = 1;
		e->keep = keep;
		e->pos = 0;
		e->at = pf_ms();
	}
	return FH + free_i;
}

void pf_close(int h)
{
	ENT *e;
	if (h < FH || h >= FH + ENT_N) {
		if (h >= 0)
			os_close(h);
		return;
	}
	e = &ent[h - FH];
	e->busy = 0;
	if (!e->keep || !keeping || e->h < 0 || pf_ms() - e->at >= KEEP_MS)
		drop(e);	/* written into its folder now and then */
}

long pf_read(int h, void *buf, long n)
{
	h = real(h);
	return h < 0 ? -1 : os_read(h, buf, n);
}

long pf_write(int h, const void *buf, long n)
{
	h = real(h);
	return h < 0 ? -1 : os_write(h, buf, n);
}

long pf_seek(int h, long off)
{
	h = real(h);
	return h < 0 ? -1 : os_seek(h, off);
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
