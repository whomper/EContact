/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * photopack.h - the contacts' pictures in one file, CACHE\PHOTOS.PAK (the
 * guidelines, 6.5: many small items in one file with an index, not a
 * file each). photo.c keeps its cache here.
 *
 * The file: a header ("ECPACK 1", where the index is, how many), the
 * records, and the index at the end. A record is what a .PIC file was:
 * "ECPHOTO 1" and its lines (UID=, BOOK=, KEY=, CETAG=, IETAG=, ILMOD=,
 * LINK=, STATE=, WHY=, DEC=), a blank line, and the pixels when it was
 * made. Records are only ever added: new ones go after the old index,
 * then a new index and the header, in one open (pack_flush), so a reset
 * half way leaves the header on the old index, whole. One replaced or
 * removed is left where it is, and the file is written anew when more
 * than half of it is such (pack_tidy).
 *
 * The index is kept in memory (a few dozen bytes a picture): whether a
 * contact has its picture is known without opening a file, and showing
 * one reads its record, one open.
 */
#ifndef PHOTOPACK_H
#define PHOTOPACK_H

typedef struct {
	unsigned long name;	/* the book's id and the contact's UID, hashed */
	unsigned long key;	/* the PHOTO line's fingerprint it was made from */
	unsigned long cetag;	/* the card's ETag, hashed (a link: checked again when it moves) */
	long off, len;		/* the record in the file */
	short book;		/* BOOK id */
	unsigned char failed, why, dec, link;
} PKENT;

/* the pack's file (store_init: CACHE\PHOTOS.PAK); "" until set */
void pack_path(const char *path);
/* the entry for name: 1 found (*e filled). The index is read the first
   time (one open) */
int  pack_find(unsigned long name, PKENT *e);
/* e's record read (one open): its text in head (NUL-terminated, cut to
   hsize, the blank line after it left out), its pixels in px (pxlen
   bytes, big-endian) when px and it has them. 0: not there, or not what
   the index says; 1: the text; 2: and the pixels */
int  pack_read(const PKENT *e, char *head, int hsize, unsigned char *px, long pxlen);
/* a record for e (e->off, e->len set here): its text and pixels (px 0:
   none) gather in memory and are written at pack_flush, or when the
   memory set aside is full. 0: not kept (no memory, or the disk) */
int  pack_put(PKENT *e, const char *head, long hlen, const unsigned char *px, long pxlen);
/* the entry for name gone (its record is left, unused) */
int  pack_drop(unsigned long name);
/* what pack_put and pack_drop did, written: one open. 0: the disk refused */
int  pack_flush(void);
/* 1: pack_put waits for pack_flush (a job); 0: each written at once.
   Returns what it was */
int  pack_batch(int on);
/* the file written anew without unused records, when they are more than
   half of it; two opens and a rename */
void pack_tidy(void);
/* every entry, for photo_prune: cb returns 1 to drop it */
int  pack_each(int (*cb)(const PKENT *e, void *ud), void *ud);
/* the index in memory let go (store_release); read again when needed */
void pack_forget(void);
/* the file changed since it was read (another EContact wrote it): its
   index is read again. One look at the folder */
void pack_check(void);
/* a string's hash (a card's ETag in PKENT.cetag) */
unsigned long pack_hash(const char *s);
/* memory the pack's buffer may take (ab_job_bytes) */
extern long pack_buf_max;

#endif
