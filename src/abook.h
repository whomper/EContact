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
	char *href, *etag;	/* where the server keeps it */
	long off, len;		/* its vCard in the cache file (the photo replaced by a stand-in) */
	long poff, plen;	/* its PHOTO line in the photo file; plen 0: none there */
	unsigned short hh;	/* a hash of href, to find it quickly */
	char partial;		/* fetched without its photo and the lines EContact doesn't show */
	char dead;		/* replaced or deleted by a record further on in the file */
} RESOURCE;

typedef struct {
	short id;		/* cache files BOOKnnnn.DAT and .PHO */
	short acct;
	short ver;		/* of its cache file, as read */
	char name[64];		/* Atari text */
	char url[400];
	char ctag[100];
	unsigned char readonly;
	CTIME synced;		/* the Atari's clock; 0 = never */
	RESOURCE *res;
	int nres, maxres;
	int ndead;		/* superseded records in the cache file (rewritten when many) */
	int count;		/* contacts in it (not groups) */
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
	short photos;		/* show contacts' photos */
	short offline;		/* Work offline: no connections */
	short hebrew;		/* start with the Hebrew keyboard */
	short heapkb;		/* the accessory's memory, KB; 0: chosen */
	short default_book;	/* new contacts go here (a BOOK id) */
	char me[80];		/* the UID of "my card" */
	char workdir[160];	/* EContact's folder in use: CACHE, ECONTACT.KEY, the log... */
	char folder[160];	/* FOLDER= as ECONTACT.INF had it */
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
void store_save_settings(void);
void book_load_all(void);
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

/* ---- for EMail and ECal ----
 * CONTACTS.TXT in EContact's folder: every email address with its name,
 * and the groups, written whenever the contacts change. EMail's address
 * book and ECal's invitations read it (see share/people.c). */
void people_export(void);

#endif
