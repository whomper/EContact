/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * abook.h - accounts, address books and their contacts, the settings in
 * ECONTACT.INF, the copies kept on disk, keeping them in step with the
 * servers, and the list of people EMail and ECal read.
 *
 * Two kinds of account, as on the Mac:
 *   CardDAV       iCloud, Fastmail, Nextcloud...: address books you can change
 *   On My Atari   an address book kept only on this computer
 */
#ifndef ABOOK_H
#define ABOOK_H

#include "vcard.h"
#include "ctime.h"

enum { AK_CARDDAV, AK_LOCAL };

typedef struct {
	char *href, *etag;	/* where the server keeps it (one malloc: etag follows href);
				   href without BOOK.hpre when rel: res_href() gives it whole */
	long off, len;		/* its vCard in the cache file (the photo replaced by a stand-in) */
	long poff, plen;	/* its PHOTO line in the photo file; plen 0: none there */
	unsigned short hh;	/* a hash of href, to find it quickly */
	short next;		/* the next with the same hash's low bits (BOOK.head) */
	char partial;		/* fetched without its photo and the lines EContact doesn't show */
	char rel;		/* href is the part after hpre */
	char done;		/* read already (from the parsed cache) */
	unsigned long th;	/* a hash of its text in the cache file: its entry in BOOKnnnn.IDX */
	char dead;		/* replaced or deleted by a record further on in the file */
} RESOURCE;

typedef struct {
	short id;		/* cache files BOOKnnnn.DAT, .PHO and .IDX */
	short acct;
	short ver;		/* of its cache file, as read */
	char name[64];		/* Atari text */
	char url[400];
	char ctag[100];
	unsigned char readonly;
	CTIME synced;		/* the Atari's clock; 0 = never */
	RESOURCE *res;
	int nres, maxres;
	short *head;		/* RES_BUCKETS chains of res by href hash */
	char hpre[120];		/* what every href starts with (the book's path), kept once */
	short hprelen;
	int ndead;		/* superseded records in the cache file (rewritten when many) */
	int count;		/* contacts in it (not groups) */
	int lost;		/* in the cache but not read: no memory for them */
	signed char idxcur;	/* BOOKnnnn.IDX: 1 this kind (added to), -1 not, 0 not known */
} BOOK;

typedef struct {
	char name[40];
	short kind;
	char url[400];
	char user[80], pass[80];
	char err[120];		/* last problem, shown in the sidebar */
} ACCOUNT;

typedef struct {
	short falcon, dsp;	/* Falcon mode: TLS here, RSA on the DSP */
	char gate[64];		/* the Raspberry Pi gateway */
	unsigned short gateport;
	short log;
	short font_id, font_pt, hebfont;
	short win_x, win_y, win_w, win_h;
	short sidebar;		/* the groups on the left */
	short sort_first;	/* sort by first name (else by last) */
	short last_first;	/* show "Last, First" (else "First Last") */
	short refresh;		/* minutes between automatic refreshes; 0 = by hand */
	short sync_open;	/* refresh when the window opens */
	short sync_after;	/* the accessory: once, minutes after boot (0: no) */
	short photos;		/* show contacts' photos */
	short offline;		/* Work offline: no connections */
	short hebrew;		/* start with the Hebrew keyboard */
	short heapkb;		/* the accessory's memory, KB; 0: chosen */
	short memkb;		/* what it needed the last time, measured (KB); 0: not known */
	short default_book;	/* new contacts go here (a BOOK id) */
	char me[80];		/* the UID of "my card" */
	char workdir[160];	/* EContact's folder in use: CACHE, ECONTACT.KEY, the log... */
	char folder[160];	/* FOLDER= as ECONTACT.INF had it */
	char start[12];		/* START=: when the accessory reads its books after boot */
} OPT;

#define HEB_ATARI 0	/* where the font has Hebrew (draw.c) */
#define HEB_ISO   1
#define HEB_DOS   2

extern OPT opt;
#define MAXACCT 12
#define MAXBOOK 24
extern ACCOUNT accts[MAXACCT];
extern int naccts;
extern BOOK books[MAXBOOK];
extern int nbooks;
extern char ab_err[200];		/* what went wrong last */
extern void (*ab_status)(const char *msg);	/* progress for the status line */
extern int store_lost_passwords;
extern int store_plain_passwords;

/* every contact and group card of every book, sorted by sortkey */
extern CONTACT **people;
extern int npeople;
/* counts up whenever people is read again: CONTACT pointers kept from
   before are gone */
extern unsigned long people_serial;

/* start-up: the settings from inf (NULL: dir\ECONTACT.INF); the rest of
   EContact's files in dir */
void store_init(const char *dir, const char *inf);
/* the same in steps: begin (the files, ECONTACT.INF loaded), then
   lines until it returns 0, at most max lines a call (an ST takes about
   a millisecond a line); -1: all of them */
void store_init_begin(const char *dir, const char *inf);
int  store_init_lines(int max);
void store_save_settings(void);
void store_release(void);		/* all of it let go, to be read again */
unsigned long store_stamp(void);	/* changes when the files do */
void book_load_all(void);
/* the same in short steps, for an accessory between its event calls:
   begin, then run until it returns 0 (more(), asked before each step,
   says when a slice is up) */
void books_load_begin(void);
int  books_load_run(int (*more)(void));
int  books_loading(void);
const char *books_load_what(void);
void books_load_cancel(void);		/* store_release does it */
/* the books brought up to date in steps, one exchange with a server a
   step (the accessory with its window closed): begin (0: nothing to do),
   then step until it returns 0; then books_load_run sorts them again */
int  quiet_sync_begin(void);
int  quiet_sync_step(void);
int  quiet_syncing(void);
int  quiet_sync_changed(void);		/* the books the last one changed */
const char *quiet_sync_what(void);
void quiet_sync_cancel(void);
void quiet_sync_stop(void);		/* where it is, what came sorted in */
extern int store_unsaved;		/* it changed ECONTACT.INF's ctags: not written yet */
/* contacts in the caches that couldn't be read for lack of memory */
int  contacts_lost(void);
void book_load(BOOK *b);
CTIME now_local(void);

/* accounts and books */
ACCOUNT *acct_add(int kind);
void     acct_delete(int a);
BOOK    *book_add(int acct, const char *name);
void     book_delete(int i);
BOOK    *book_by_id(int id);
int      book_writable(const BOOK *b);
int      acct_discover(int a);
/* where a new contact goes: the default book, else the first one that
   can be written; -1 if there is none */
int      book_for_new(void);

/* bring books up to date with their servers: one account, or all.
   Returns how many books changed, -1 and ab_err on failure */
int  sync_account(int a, int force);
int  sync_all(int force);
int  sync_book(BOOK *b, int force);

/* ---- contacts ---- */
/* the contact's vCard text from the cache (malloc'ed), or NULL. A card
   fetched in part comes without its photo (and what EContact doesn't show) */
char *contact_raw(const CONTACT *c, long *len);
int  contact_partial(const CONTACT *c);
const char *contact_href(const CONTACT *c);
/* a card fetched in part, fetched whole from its server and kept. c goes
   (the list is read again): the new one comes back, NULL with ab_err */
CONTACT *contact_complete(const CONTACT *c);
/* the cards of b at these hrefs fetched whole (photo_sync_all, a few at
   a time); how many came, -1 and ab_err if none could */
int  book_fetch_whole(BOOK *b, char **hrefs, int n);
/* a job of fetches (Sync Contact Photos): book bi's cards fetched whole
   gather in memory, read from there, and are written at
   book_fetch_job_end (or when the job's memory is full): one open a file
   (6.5). 0 if the cache can't be added to; end: 0 with ab_err when the
   disk refused (then read again as it is) */
int  book_fetch_job(int bi);
int  book_fetch_job_end(void);
/* between fetches: what the job holds written when next bytes more
   (the next round's cards) would take it past ab_job_bytes (after the
   photos are made from it) */
int  book_fetch_job_room(long next);
/* what the job holds written now (a job with little memory) */
int  book_fetch_job_flush(void);
/* save e as a new contact in book (old NULL) or over old; the saved
   contact comes back (the list was read again), NULL with ab_err */
CONTACT *contact_save(const CEDIT *e, CONTACT *old, int book);
int  contact_delete(CONTACT *c);
CONTACT *contact_by_uid(const char *uid, int book);	/* book -1: any */
CONTACT *contact_by_href(int book, const char *href);	/* found again after the list was read anew */
/* everything matching text: names, company, emails, phones, notes */
int  contact_matches(const CONTACT *c, const char *text);
/* the contacts of a vCard file into book; how many in *count */
int  contacts_import(int book, const char *path, int *count);
/* contacts (n of them) into a .vcf file */
int  contacts_export(CONTACT **list, int n, const char *path);

/* ---- groups ----
 * iCloud keeps a group as a card of its own listing its members; Nextcloud
 * and others put the group's name in each member's CATEGORIES. Both show
 * the same way. */
typedef struct {
	char name[64];
	short book;		/* the book it belongs to */
	CONTACT *card;		/* iCloud style; NULL: a category */
} GROUP;
extern GROUP *groups;
extern int ngroups;
void groups_collect(void);		/* after any change to people */
int  group_has(const GROUP *g, const CONTACT *c);
int  group_count(const GROUP *g);
int  group_add(GROUP *g, CONTACT *c);		/* 1 ok (or already in it) */
int  group_remove(GROUP *g, CONTACT *c);
int  group_new(int book, const char *name);	/* -1 on failure, else its index */
int  group_rename(GROUP *g, const char *name);
int  group_delete(GROUP *g);			/* the group, not its members */

/* ---- memory for bulk work (the guidelines, 6.5) ----
 * ab_job_bytes: what a refresh or a photo sync gathers in memory before
 * it writes; ab_out_max: each file's write buffer. The UI sets them from
 * the memory there is (the accessory's heap) */
extern long ab_job_bytes, ab_out_max;
/* the cache's reads: 16 KB on a 68000, 32 KB on a 68030 (6.5) */
extern long ab_read_bytes;

/* ---- for EMail and ECal (PEOPLE, ui/answer.c) ----
 * people_names_stamp: the names and addresses in one number; it changes
 * when they do (after a load, a sync or a change) */
extern unsigned long people_names_stamp;
int people_name_for(const char *addr, char *name, int size);	/* 1 found */
/* CONTACTS.TXT in the folder, for EMail and ECal when EContact isn't
   loaded (the guidelines 4.1): due after groups_collect (a load, a
   refresh, an edit), made again when its stamp isn't the file's; in
   steps (people_file_step: 1 while there is more), or all at once; the
   file there looked at anew after store_release */
#define PEOPLE_NAME_MAX 63
int  people_file_due(void);
int  people_file_step(void);
void people_file_write(void);
void people_file_forget(void);
const char *people_file_what(void);	/* AES_PROFILE's note */

#endif
