/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * photopack.c - the contacts' pictures in one file (see photopack.h).
 */
#include <stdlib.h>
#include <string.h>
#include "photopack.h"
#include "plat.h"
#include "util.h"

#define HEAD 24			/* "ECPACK 1", the index's offset, its count, unused bytes, 0 */
#define ENT 28			/* an index entry in the file */
#define MAXENT 32000

void photo_migrate(const char *pack);	/* photomig.c: the old CACHE\PHOTOS, once */

long pack_buf_max = 64L * 1024;

static char ppath[200];
static PKENT *ent;
static int nent, maxent;
static int loaded;		/* the index is in memory (or there is no file) */
static int fresh;		/* the next flush writes the file anew */
static long fsize;		/* the file as we know it: its size, */
static long idxoff;		/* where its index starts (0: no file) */
static long dead;		/* bytes no entry uses */
static char *pbuf;		/* records waiting for pack_flush: */
static long pn, pcap;		/* an entry's off is -(where in pbuf + 1) */
static int dirty;		/* entries dropped since the flush */
static int batch;

static unsigned long get32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static void put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

void pack_path(const char *path)
{
	if (strcmp(ppath, path))
		pack_forget();
	str_copy(ppath, path, sizeof(ppath));
}

void pack_forget(void)
{
	free(ent);
	free(pbuf);
	ent = 0;
	pbuf = 0;
	nent = maxent = 0;
	pn = pcap = 0;
	loaded = dirty = fresh = 0;
	fsize = idxoff = dead = 0;
}

static int room(int n)
{
	if (n > maxent) {
		int m = maxent ? maxent : 16;
		PKENT *ne;
		while (m < n)
			m *= 2;
		if (!(ne = realloc(ent, m * sizeof(PKENT))))
			return 0;
		ent = ne;
		maxent = m;
	}
	return 1;
}

/* the index from the file, in one open */
static void load(void)
{
	unsigned char hd[HEAD], b[ENT * 32];
	long size, count, i;
	int h;
	if (loaded)
		return;
	loaded = 1;
	nent = 0;
	fsize = idxoff = dead = 0;
	if (!ppath[0])
		return;
	h = pf_open(ppath, PF_READ);
	if (h < 0) {
		photo_migrate(ppath);	/* the old CACHE\PHOTOS, the first time */
		return;
	}
	size = pf_size_of(h);
	fresh = 1;		/* written anew unless it reads as it should */
	if (size >= HEAD && pf_read(h, hd, HEAD) == HEAD && !memcmp(hd, "ECPACK 1", 8)) {
		long off = (long)get32(hd + 8);
		count = (long)get32(hd + 12);
		/* (more after the index: a write a reset cut short, unused) */
		if (off >= HEAD && count >= 0 && count <= MAXENT && off + count * ENT <= size && room((int)count + 1) &&
		    pf_seek(h, off) == off) {
			for (i = 0; i < count;) {
				long k = count - i < 32 ? count - i : 32, j;
				if (pf_read(h, b, k * ENT) != k * ENT)
					break;
				for (j = 0; j < k; j++, i++) {
					const unsigned char *p = b + j * ENT;
					PKENT *e = &ent[i];
					e->name = get32(p);
					e->key = get32(p + 4);
					e->cetag = get32(p + 8);
					e->off = (long)get32(p + 12);
					e->len = (long)get32(p + 16);
					e->book = (short)(p[20] << 8 | p[21]);
					e->failed = p[22];
					e->why = p[23];
					e->dec = p[24];
					e->link = p[25];
					if (e->off < HEAD || e->len <= 0 || e->off + e->len > off)
						break;
				}
				if (j < k)
					break;
			}
			if (i == count) {
				nent = (int)count;
				fsize = size;
				idxoff = off;
				dead = (long)get32(hd + 16) + (size - off - count * ENT);
				fresh = 0;
			}
		}
	}
	pf_close(h);
}

static int find(unsigned long name)
{
	int i;
	for (i = 0; i < nent; i++)
		if (ent[i].name == name)
			return i;
	return -1;
}

int pack_find(unsigned long name, PKENT *e)
{
	int i;
	load();
	if ((i = find(name)) < 0)
		return 0;
	*e = ent[i];
	return 1;
}

int pack_read(const PKENT *e, char *head, int hsize, unsigned char *px, long pxlen)
{
	char *rec, *blank;
	long hl;
	int ok = 0;
	if (e->len <= 0 || e->len > 64L * 1024)
		return 0;
	if (e->off < 0) {
		/* not written yet: from memory */
		long at = -e->off - 1;
		if (at + e->len > pn)
			return 0;
		rec = malloc(e->len + 1);
		if (!rec)
			return 0;
		memcpy(rec, pbuf + at, e->len);
	} else {
		int h = pf_open(ppath, PF_READ);
		if (h < 0)
			return 0;
		rec = malloc(e->len + 1);
		if (!rec || pf_seek(h, e->off) != e->off || pf_read(h, rec, e->len) != e->len) {
			pf_close(h);
			free(rec);
			return 0;
		}
		pf_close(h);
	}
	rec[e->len] = 0;
	if (!strncmp(rec, "ECPHOTO 1\n", 10) && (blank = strstr(rec, "\n\n")) != 0) {
		hl = (long)(blank + 2 - rec);
		blank[1] = 0;
		str_copy(head, rec, hsize);
		ok = 1;
		if (px && e->len - hl == pxlen) {
			memcpy(px, rec + hl, pxlen);
			ok = 2;
		}
	}
	free(rec);
	return ok;
}

/* unused from now on: its bytes counted (in the file) */
static void unuse(const PKENT *e)
{
	if (e->off >= 0)
		dead += e->len;
}

int pack_put(PKENT *e, const char *head, long hlen, const unsigned char *px, long pxlen)
{
	long len = hlen + pxlen;
	int i;
	load();
	if (!ppath[0])
		return 0;
	if (pn && pn + len > pack_buf_max && !pack_flush())
		return 0;
	if (pn + len > pcap) {
		long cap = pcap ? pcap : 9 * 1024;
		char *nb;
		while (cap < pn + len)
			cap *= 2;
		if (!(nb = realloc(pbuf, cap)))
			return 0;	/* not kept: made again another time */
		pbuf = nb;
		pcap = cap;
	}
	i = find(e->name);
	if (i < 0) {
		if (nent >= MAXENT || !room(nent + 1))
			return 0;
		i = nent++;
	} else {
		unuse(&ent[i]);
	}
	memcpy(pbuf + pn, head, hlen);
	if (pxlen)
		memcpy(pbuf + pn + hlen, px, pxlen);
	e->off = -(pn + 1);
	e->len = len;
	pn += len;
	ent[i] = *e;
	return batch ? 1 : pack_flush();
}

int pack_drop(unsigned long name)
{
	int i;
	load();
	if ((i = find(name)) < 0)
		return 0;
	unuse(&ent[i]);
	ent[i] = ent[--nent];
	dirty = 1;
	return batch ? 1 : pack_flush();
}

int pack_batch(int on)
{
	int was = batch;
	batch = on;
	return was;
}

unsigned long pack_hash(const char *s)
{
	unsigned long h = 2166136261UL;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h = (h * 16777619UL) & 0xffffffffUL;
	}
	return h;
}

/* the index after base, the header at the start: h is open for writing */
static int write_index(int h, long at)
{
	unsigned char b[ENT * 32], hd[HEAD];
	int i, k;
	for (i = 0; i < nent; i += k) {
		for (k = 0; k < 32 && i + k < nent; k++) {
			unsigned char *p = b + k * ENT;
			const PKENT *e = &ent[i + k];
			memset(p, 0, ENT);
			put32(p, e->name);
			put32(p + 4, e->key);
			put32(p + 8, e->cetag);
			put32(p + 12, (unsigned long)e->off);
			put32(p + 16, (unsigned long)e->len);
			p[20] = (unsigned char)(e->book >> 8);
			p[21] = (unsigned char)e->book;
			p[22] = e->failed;
			p[23] = e->why;
			p[24] = e->dec;
			p[25] = e->link;
		}
		if (pf_write(h, b, k * ENT) != k * ENT)
			return 0;
	}
	memset(hd, 0, HEAD);
	memcpy(hd, "ECPACK 1", 8);
	put32(hd + 8, (unsigned long)at);
	put32(hd + 12, (unsigned long)nent);
	put32(hd + 16, (unsigned long)dead);
	return pf_seek(h, 0) == 0 && pf_write(h, hd, HEAD) == HEAD;
}

int pack_flush(void)
{
	long base, at;
	int h, i, ok;
	if (!loaded || (!pn && !dirty))
		return 1;
	if (!fresh && idxoff) {
		/* added after the old index, which stays whole until the
		   header moves past it */
		h = pf_open(ppath, PF_UPDATE);
		base = fsize;
		ok = h >= 0 && pf_seek(h, base) == base;
	} else {
		unsigned char z[HEAD];
		memset(z, 0, HEAD);
		h = pf_open(ppath, PF_WRITE);
		base = HEAD;
		dead = 0;
		ok = h >= 0 && pf_write(h, z, HEAD) == HEAD;
	}
	ok = ok && (!pn || pf_write(h, pbuf, pn) == pn);
	if (ok) {
		for (i = 0; i < nent; i++)
			if (ent[i].off < 0)
				ent[i].off = base + (-ent[i].off - 1);
		if (idxoff && !fresh)
			dead += fsize - idxoff;	/* the old index */
		at = base + pn;
		ok = write_index(h, at);
		if (ok) {
			idxoff = at;
			fsize = at + (long)nent * ENT;
		}
	}
	if (h >= 0)
		pf_close(h);
	pn = 0;
	dirty = 0;
	fresh = 0;
	if (!ok) {
		/* as the file is now: read again when needed */
		pack_forget();
		return 0;
	}
	return 1;
}

int pack_each(int (*cb)(const PKENT *e, void *ud), void *ud)
{
	int i, n = 0;
	load();
	for (i = 0; i < nent;) {
		if (cb(&ent[i], ud)) {
			unuse(&ent[i]);
			ent[i] = ent[--nent];
			dirty = 1;
			n++;
		} else {
			i++;
		}
	}
	return n;
}

static int by_off(const void *a, const void *b)
{
	const PKENT *x = a, *y = b;
	return x->off < y->off ? -1 : x->off > y->off;
}

void pack_tidy(void)
{
	char tmp[210], *rec = 0;
	long at = HEAD, cap = 0;
	int hi, ho, i, ok;
	if (!loaded || !pack_flush() || dead < 32L * 1024 || 2 * dead < fsize)
		return;
	str_copy(tmp, ppath, sizeof(tmp));
	tmp[strlen(tmp) - 1] = '$';	/* PHOTOS.PA$ */
	hi = pf_open(ppath, PF_READ);
	ho = hi >= 0 ? pf_open(tmp, PF_WRITE) : -1;
	ok = ho >= 0;
	qsort(ent, nent, sizeof(PKENT), by_off);	/* read in order */
	{
		unsigned char z[HEAD];
		memset(z, 0, HEAD);
		ok = ok && pf_write(ho, z, HEAD) == HEAD;
	}
	for (i = 0; ok && i < nent; i++) {
		PKENT *e = &ent[i];
		if (e->len > cap) {
			free(rec);
			cap = e->len;
			if (!(rec = malloc(cap)))
				ok = 0;
		}
		ok = ok && pf_seek(hi, e->off) == e->off && pf_read(hi, rec, e->len) == e->len &&
		     pf_write(ho, rec, e->len) == e->len;
		e->off = at;
		at += e->len;
	}
	free(rec);
	if (hi >= 0)
		pf_close(hi);
	if (ok) {
		long was = dead;
		dead = 0;
		ok = write_index(ho, at);
		dead = was;
	}
	if (ho >= 0)
		pf_close(ho);
	if (ok && pf_remove(ppath) == 0 && pf_rename(tmp, ppath) == 0) {
		idxoff = at;
		fsize = at + (long)nent * ENT;
		dead = 0;
		return;
	}
	pf_remove(tmp);
	pack_forget();		/* read again as it is */
}

void pack_check(void)
{
	long size;
	if (!loaded || pn || dirty || !ppath[0])
		return;
	pf_info(ppath, &size);
	if (size != (idxoff ? fsize : -1L))
		pack_forget();
}
