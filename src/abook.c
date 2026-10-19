/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * abook.c - the address books: settings, caches on disk, syncing with
 * CardDAV servers, saving changes, groups, and the names for EMail
 * and ECal. Built like ECal's calstore.c.
 *
 * Each address book has cache files in CACHE\ next to the settings:
 *   BOOKnnnn.DAT  every contact's vCard as the server sent it, with its
 *                 href and etag, but for its photo
 *   BOOKnnnn.PHO  the photos (PHOTO lines), apart: the cards are read at
 *                 every start, the photos only when one is shown
 * so the contacts open at once, offline too, and a contact can be
 * written back without losing what this program doesn't understand.
 *
 * Syncing asks the server for the cards without their photos (CardDAV's
 * partial retrieval): 500 contacts with photos are 12 MB, without them
 * half a megabyte. A photo is fetched, with the rest of its card, when
 * the card is shown, or for all of them by Sync Contact Photos. A server
 * that sends the photos anyway has them kept as they come.
 */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include "abook.h"
#include "version.h"
#include "carddav.h"
#include "photo.h"
#include "photopack.h"
#include "http.h"
#include "conn.h"
#include "secret.h"
#include "charset.h"
#include "plat.h"
#include "util.h"

OPT opt;
ACCOUNT accts[MAXACCT];
int naccts;
BOOK books[MAXBOOK];
int nbooks;
char ab_err[200];
void (*ab_status)(const char *msg);
int store_lost_passwords;
int store_plain_passwords;		/* ECONTACT.INF has a password not yet encrypted */
CONTACT **people;
int npeople;
unsigned long people_serial;
static int maxpeople;
GROUP *groups;
int ngroups;

static char inf_path[200], cache_dir[200];

static void status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void status(const char *fmt, ...)
{
	char buf[120];
	va_list ap;
	if (!ab_status)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ab_status(buf);
}

static void status_clear(void)
{
	if (ab_status)
		ab_status("");
}

CTIME now_local(void)
{
	PFTIME t;
	pf_now(&t);
	return ct_make(t.year, t.mon, t.day, t.hour, t.min);
}

/* ---------------- settings ---------------- */

static void defaults(void)
{
	memset(&opt, 0, sizeof(opt));
	opt.gateport = 8180;
	opt.dsp = 1;
	opt.font_id = 1;
	opt.sidebar = 1;
	opt.sync_open = 1;
	opt.photos = 1;
	opt.default_book = -1;
	str_copy(opt.start, "60", sizeof(opt.start));
	opt.sync_after = 0;
}

static void set_opt(const char *k, const char *v)
{
	/* the first letter first: strcasecmp on each costs a 68000 */
	int c = *k | 0x20;
#define S(name) if (c == #name[0] && !strcasecmp(k, #name)) { str_copy(opt.name, v, sizeof(opt.name)); return; }
#define I(name) if (c == #name[0] && !strcasecmp(k, #name)) { opt.name = (short)atoi(v); return; }
	S(gate) S(folder) S(me) S(start)
	I(falcon) I(dsp) I(log) I(font_id) I(font_pt) I(hebfont) I(win_x) I(win_y) I(win_w) I(win_h)
	I(sidebar) I(sort_first) I(last_first) I(refresh) I(sync_open) I(sync_after) I(photos) I(offline) I(hebrew) I(heapkb) I(memkb) I(default_book)
	if (!strcasecmp(k, "gateport")) {
		opt.gateport = (unsigned short)atoi(v);
		return;
	}
#undef S
#undef I
}

static void set_acct(ACCOUNT *a, const char *k, const char *v)
{
	if (!strcasecmp(k, "name"))
		str_copy(a->name, v, sizeof(a->name));
	else if (!strcasecmp(k, "kind"))
		a->kind = (short)atoi(v);
	else if (!strcasecmp(k, "url"))
		str_copy(a->url, v, sizeof(a->url));
	else if (!strcasecmp(k, "user"))
		str_copy(a->user, v, sizeof(a->user));
	else if (!strcasecmp(k, "pass")) {
		if (*v && strncmp(v, SECRET_TAG, strlen(SECRET_TAG)))
			store_plain_passwords = 1;	/* typed in by hand: encrypted at start-up */
		if (!secret_decode(v, a->pass, sizeof(a->pass))) {
			a->pass[0] = 0;
			store_lost_passwords = 1;
		}
	}
}

static void set_book(BOOK *b, const char *k, const char *v)
{
	if (!strcasecmp(k, "id"))
		b->id = (short)atoi(v);
	else if (!strcasecmp(k, "name"))
		str_copy(b->name, v, sizeof(b->name));
	else if (!strcasecmp(k, "url"))
		str_copy(b->url, v, sizeof(b->url));
	else if (!strcasecmp(k, "ctag"))
		str_copy(b->ctag, v, sizeof(b->ctag));
	else if (!strcasecmp(k, "readonly"))
		b->readonly = (unsigned char)atoi(v);
	else if (!strcasecmp(k, "synced"))
		b->synced = atol(v);
}

static void apply_http(void)
{
	str_copy(http_cfg.gate, opt.gate, sizeof(http_cfg.gate));
	http_cfg.gateport = opt.gateport;
	http_cfg.falcon = opt.falcon;
	http_cfg.offline = opt.offline;
}

void store_init(const char *dir, const char *inf)
{
	char key[200], *text, *line;
	long len;
	ACCOUNT *a = 0;
	BOOK *b = 0;
	defaults();
	str_copy(opt.workdir, dir, sizeof(opt.workdir));
	if (inf)
		str_copy(inf_path, inf, sizeof(inf_path));
	else
		path_join(inf_path, sizeof(inf_path), dir, "ECONTACT.INF");
	path_join(cache_dir, sizeof(cache_dir), dir, "CACHE");
	pf_mkdir(cache_dir);
	photo_dir(cache_dir);		/* the pictures: CACHE\PHOTOS.PAK */
	path_join(key, sizeof(key), dir, "ECONTACT.KEY");
	secret_init(key);
	text = pf_load(inf_path, &len);
	if (!text) {
		apply_http();
		return;
	}
	for (line = text; line && *line;) {
		char *nl = strchr(line, '\n'), *eq;
		if (nl)
			*nl = 0;
		if (nl && nl > line && nl[-1] == '\r')
			nl[-1] = 0;
		if (*line == ';' || !*line) {
			/* a note: passed at once (each line costs an ST a millisecond) */
		} else if (!strcasecmp(line, "[options]")) {
			a = 0;
			b = 0;
		} else if (!strcasecmp(line, "[account]")) {
			a = naccts < MAXACCT ? &accts[naccts++] : 0;
			if (a)
				memset(a, 0, sizeof(*a));
			b = 0;
		} else if (!strcasecmp(line, "[book]")) {
			b = (a && nbooks < MAXBOOK) ? &books[nbooks++] : 0;
			if (b) {
				memset(b, 0, sizeof(*b));
				b->acct = (short)(a - accts);
			}
		} else if ((eq = strchr(line, '='))) {
			*eq = 0;
			if (b)
				set_book(b, line, eq + 1);
			else if (a)
				set_acct(a, line, eq + 1);
			else {
				/* a note after a setting: "7  ; a week" */
				char *v = eq + 1, *sc = strchr(v, ';');
				if (sc && sc > v && (sc[-1] == ' ' || sc[-1] == '\t')) {
					while (sc > v && (sc[-1] == ' ' || sc[-1] == '\t'))
						sc--;
					*sc = 0;
				}
				set_opt(line, v);
			}
		}
		line = nl ? nl + 1 : 0;
	}
	free(text);
	apply_http();
}

/* each setting with a note, so the file explains itself (as ECal's) */
/* (appended, not printf'ed: formatting the notes took an ST 200 ms) */
static void note(SBUF *b, const char *text)
{
	if (*text) {
		sb_adds(b, "; ");
		sb_adds(b, text);
		sb_adds(b, "\r\n");
	}
}

static void kv(SBUF *b, const char *text, const char *key, const char *value)
{
	note(b, text);
	sb_adds(b, key);
	sb_adds(b, "=");
	sb_adds(b, value);
	sb_adds(b, "\r\n\r\n");
}

static void kn(SBUF *b, const char *text, const char *key, long value)
{
	char v[16], *p = v + sizeof(v) - 1;
	unsigned long u = value < 0 ? -(unsigned long)value : (unsigned long)value;
	*p = 0;
	do
		*--p = (char)('0' + u % 10);
	while (u /= 10);
	if (value < 0)
		*--p = '-';
	kv(b, text, key, p);
}

void store_save_settings(void)
{
	store_unsaved = 0;
	SBUF b;
	int i, j;
	char enc[200];
	sb_init(&b);
	sb_adds(&b, "; EContact settings, written by ECONTACT.ACC / ECONTACT.PRG. Lines starting with ; are notes.\r\n"
		    "; Most of these are set in the menu > Settings; see the guide (the Help key).\r\n"
		    "; 1 means on, 0 means off. Keys may be in any case.\r\n\r\n[OPTIONS]\r\n");
#define S(key, name, text) kv(&b, text, key, opt.name);
#define I(key, name, text) kn(&b, text, key, opt.name);
	/* first: the accessory reads it from the start of the file at boot */
	sb_adds(&b, "; EContact's folder, with its address books, keys and certificates;\r\n"
		    "; ECONTACT.PRG writes where it was started from\r\nFOLDER=");
	sb_adds(&b, opt.workdir);
	sb_adds(&b, "\r\n\r\n");
	I("HEAPKB", heapkb, "memory the accessory takes at boot, in KB, to choose it yourself; 0: what it measured (MEMKB)")
	I("MEMKB", memkb, "what the accessory needed the last time, as it measured it (KB): it takes that at the next start; 0: not known yet (then 1.5 MB on a Falcon/TT, 4 MB in TT RAM, half the free memory on an ST)")
	S("START", start, "when the accessory reads the address books after the Atari starts, a little at a time: now, opened (when its window first opens), or seconds after boot")
	I("FONT_ID", font_id, "the font, as a GDOS id; 1: the system font (the accessory loads GDOS fonts only as the Atari starts, when one is chosen here)")
	I("FONT_PT", font_pt, "the font's size in points; 0: the system font's")
	I("FALCON", falcon, "Falcon mode: TLS on this Atari (Falcon, TT); 0: through the Pi gateway")
	I("DSP", dsp, "check certificates on the Falcon's DSP56001")
	S("GATE", gate, "the Raspberry Pi gateway's address (Settings > Connection); ECal's gateway serves both")
	sb_printf(&b, "; the gateway's port\r\nGATEPORT=%u\r\n\r\n", opt.gateport);
	I("OFFLINE", offline, "work offline: no connections at all (the menu > Work offline)")
	I("LOG", log, "write the conversation with the servers to ECONTACT.LOG (passwords left out)")
	I("REFRESH", refresh, "minutes between refreshes of all address books (the accessory's in the background too, its window closed); 0: only by hand")
	I("SYNC_OPEN", sync_open, "refresh when the window opens")
	I("SYNC_AFTER", sync_after, "the accessory: refresh once in the background, this many minutes after the Atari starts; 0: no")
	I("PHOTOS", photos, "show contacts' photos (kept small in CACHE\\PHOTOS)")
	I("SORT_FIRST", sort_first, "sort by first name; 0: by last name")
	I("LAST_FIRST", last_first, "show names as Last, First; 0: First Last")
	I("DEFAULT_BOOK", default_book, "new contacts go into this address book (its ID below); -1: the first")
	S("ME", me, "the UID of your own card (right-click a contact > Make This My Card)")
	I("HEBFONT", hebfont, "where the font has the Hebrew letters: 0 Atari, 1 ISO-8859-8, 2 DOS 862")
	I("HEBREW", hebrew, "start with the Hebrew keyboard (F10 switches)")
	I("SIDEBAR", sidebar, "show the groups on the left")
	I("WIN_X", win_x, "the window's left edge, in pixels")
	I("WIN_Y", win_y, "the window's top edge, in pixels")
	I("WIN_W", win_w, "the window's width, in pixels")
	I("WIN_H", win_h, "the window's height, in pixels")
#undef S
#undef I
	for (i = 0; i < naccts; i++) {
		ACCOUNT *a = &accts[i];
		sb_adds(&b, "[ACCOUNT]\r\n");
		kv(&b, "the account's name in the list on the left", "NAME", a->name);
		kn(&b, "0 CardDAV (iCloud, Fastmail...), 1 On My Atari", "KIND", a->kind);
		kv(&b, "the server's address", "URL", a->url);
		kv(&b, "the user name (iCloud: the Apple ID)", "USER", a->user);
		if (a->pass[0]) {
			secret_encode(a->pass, enc, sizeof(enc));
			kv(&b, "the password, encrypted with ECONTACT.KEY (type a new one in plain text: it is encrypted at start-up)",
			   "PASS", enc);
		}
		for (j = 0; j < nbooks; j++) {
			BOOK *k = &books[j];
			if (k->acct != i)
				continue;
			sb_adds(&b, "[BOOK]\r\n");
			kn(&b, "its cache file in CACHE: BOOKnnnn.DAT", "ID", k->id);
			kv(&b, "the address book's name", "NAME", k->name);
			kv(&b, "where the server keeps it", "URL", k->url);
			kv(&b, "the server's mark for its contents, to see what changed", "CTAG", k->ctag);
			kn(&b, "can't be changed on this Atari", "READONLY", k->readonly);
			kn(&b, "last brought up to date (minutes since 1970, the Atari's clock); 0: never", "SYNCED",
			   (long)k->synced);
		}
	}
	pf_save(inf_path, b.s, b.len);
	sb_free(&b);
	apply_http();
}

/* ---------------- the people ---------------- */

static int cmp_people(const void *a, const void *b)
{
	const CONTACT *x = *(CONTACT *const *)a, *y = *(CONTACT *const *)b;
	int r = strcmp(x->sortkey, y->sortkey);
	if (r)
		return r;
	return x->book != y->book ? x->book - y->book : x->res - y->res;
}

static void people_sort(void)
{
	int i;
	/* read from the parsed cache, one book's people come in order */
	for (i = 1; i < npeople; i++)
		if (cmp_people(&people[i - 1], &people[i]) > 0)
			break;
	if (i < npeople)
		qsort(people, npeople, sizeof(CONTACT *), cmp_people);
}

/* The same in steps, for a load in the background: a look whether they
   are in order (they are when read from the parsed cache, which is
   written in order), else a merge sort, a few compares at a time */
static struct {
	CONTACT **tmp;
	int st, n, i, w, lo, a, ae, b, be, o;
} srt;

static void sort_cancel(void)
{
	free(srt.tmp);
	memset(&srt, 0, sizeof(srt));
}

static void sort_begin(void)
{
	sort_cancel();
	srt.i = 1;
	srt.st = 1;
}

/* 1 while there is more to do */
static int sort_step(void)
{
	int k = 0;
	switch (srt.st) {
	case 1:
		while (srt.i < npeople) {
			if (cmp_people(&people[srt.i - 1], &people[srt.i]) > 0)
				break;
			srt.i++;
			if (++k >= 128)
				return 1;
		}
		if (srt.i >= npeople)
			break;
		srt.n = npeople;
		srt.tmp = malloc(srt.n * sizeof(CONTACT *));
		if (!srt.tmp) {
			qsort(people, npeople, sizeof(CONTACT *), cmp_people);
			break;
		}
		srt.w = 1;
		srt.lo = 0;
		srt.st = 2;
		return 1;
	case 2:
		/* the next two runs of w, merged into tmp */
		if (srt.lo >= srt.n) {
			memcpy(people, srt.tmp, srt.n * sizeof(CONTACT *));
			srt.w *= 2;
			srt.lo = 0;
			if (srt.w >= srt.n)
				break;
			return 1;
		}
		srt.a = srt.o = srt.lo;
		srt.ae = srt.b = srt.lo + srt.w < srt.n ? srt.lo + srt.w : srt.n;
		srt.be = srt.lo + 2 * srt.w < srt.n ? srt.lo + 2 * srt.w : srt.n;
		srt.st = 3;
		/* fall through */
	case 3:
		while (srt.a < srt.ae || srt.b < srt.be) {
			if (srt.b >= srt.be || (srt.a < srt.ae && cmp_people(&people[srt.a], &people[srt.b]) <= 0))
				srt.tmp[srt.o++] = people[srt.a++];
			else
				srt.tmp[srt.o++] = people[srt.b++];
			if (++k >= 64)
				return 1;
		}
		srt.lo += 2 * srt.w;
		srt.st = 2;
		return 1;
	}
	sort_cancel();
	return 0;
}

static int people_add(CONTACT *c)
{
	if (npeople >= maxpeople) {
		int max = maxpeople ? maxpeople * 2 : 128;
		CONTACT **n = realloc(people, max * sizeof(CONTACT *));
		if (!n)
			return 0;
		people = n;
		maxpeople = max;
	}
	people[npeople++] = c;
	return 1;
}

/* the contacts that came from book bi (and resource r, or all: -1) go */
static void people_drop(int bi, int r)
{
	int i, n = 0;
	people_serial++;
	for (i = 0; i < npeople; i++) {
		if (people[i]->book == bi && (r < 0 || people[i]->res == r))
			free(people[i]);
		else
			people[n++] = people[i];
	}
	npeople = n;
}

CONTACT *contact_by_uid(const char *uid, int book)
{
	int i;
	if (!uid || !*uid)
		return 0;
	for (i = 0; i < npeople; i++)
		if (people[i]->uid && !strcmp(people[i]->uid, uid) && (book < 0 || people[i]->book == book))
			return people[i];
	return 0;
}

static void count_books(void)
{
	int i;
	for (i = 0; i < nbooks; i++)
		books[i].count = 0;
	for (i = 0; i < npeople; i++)
		if (!people[i]->group && people[i]->book >= 0 && people[i]->book < nbooks)
			books[people[i]->book].count++;
}

/* sorted again, groups and counts again; names: they may have changed
   (EMail and ECal are told) */
static void names_stamp(void);
static unsigned long names_of(unsigned long h, const CONTACT *c);

static void people_changed(int names)
{
	people_serial++;
	people_sort();
	count_books();
	groups_collect();
	if (names)
		names_stamp();
}

/* ---------------- accounts and books ---------------- */

ACCOUNT *acct_add(int kind)
{
	ACCOUNT *a;
	if (naccts >= MAXACCT)
		return 0;
	a = &accts[naccts++];
	memset(a, 0, sizeof(*a));
	a->kind = (short)kind;
	return a;
}

static void cache_path(const BOOK *b, const char *ext, char *out, int size)
{
	char n[16];
	snprintf(n, sizeof(n), "BOOK%04d.%s", b->id, ext);
	path_join(out, size, cache_dir, n);
}

static void book_free(BOOK *b)
{
	int i;
	for (i = 0; i < b->nres; i++)
		free(b->res[i].href);	/* the etag with it */
	free(b->res);
	free(b->head);
	b->res = 0;
	b->head = 0;
	b->hprelen = 0;
	b->nres = b->maxres = 0;
	b->ndead = 0;
}

void book_delete(int i)
{
	char p[200];
	int k;
	BOOK *b = &books[i];
	cache_path(b, "DAT", p, sizeof(p));
	pf_remove(p);
	cache_path(b, "PHO", p, sizeof(p));
	pf_remove(p);
	cache_path(b, "IDX", p, sizeof(p));
	pf_remove(p);
	people_drop(i, -1);
	book_free(b);
	if (opt.default_book == b->id)
		opt.default_book = -1;
	for (k = i; k < nbooks - 1; k++)
		books[k] = books[k + 1];
	nbooks--;
	/* the contacts know their book by index */
	for (k = 0; k < npeople; k++)
		if (people[k]->book > i)
			people[k]->book--;
	people_changed(1);
}

void acct_delete(int a)
{
	int i;
	for (i = nbooks - 1; i >= 0; i--)
		if (books[i].acct == a)
			book_delete(i);
	for (i = 0; i < nbooks; i++)
		if (books[i].acct > a)
			books[i].acct--;
	for (i = a; i < naccts - 1; i++)
		accts[i] = accts[i + 1];
	naccts--;
}

/* everything read from ECONTACT.INF and the caches let go, to be read
   again with store_init and book_load_all: the accessory does it when
   the program changed the files while it waited */
void store_release(void)
{
	int i;
	books_load_cancel();		/* a load in steps, part done */
	quiet_sync_cancel();		/* and a sync */
	for (i = 0; i < nbooks; i++)
		book_free(&books[i]);
	for (i = 0; i < npeople; i++)
		free(people[i]);
	npeople = 0;
	people_serial++;
	nbooks = naccts = 0;
	groups_collect();
	store_lost_passwords = store_plain_passwords = 0;
	pack_forget();
}

/* the files as they are now: ECONTACT.INF, ECONTACT.KEY and the books'
   caches, their sizes and when they were written */
unsigned long store_stamp(void)
{
	char p[200];
	unsigned long h = 5381;
	int i;
	for (i = -2; i < nbooks; i++) {
		if (i == -2)
			str_copy(p, inf_path, sizeof(p));
		else if (i == -1)
			path_join(p, sizeof(p), opt.workdir, "ECONTACT.KEY");
		else
			cache_path(&books[i], "DAT", p, sizeof(p));
		{
			long size;
			h = (h * 33) ^ pf_info(p, &size);	/* one look a file */
			h = (h * 33) ^ (unsigned long)size;
		}
	}
	return h;
}

BOOK *book_add(int acct, const char *name)
{
	BOOK *b;
	int id = 1, i;
	if (nbooks >= MAXBOOK)
		return 0;
	for (i = 0; i < nbooks; i++)
		if (books[i].id >= id)
			id = books[i].id + 1;
	b = &books[nbooks++];
	memset(b, 0, sizeof(*b));
	b->id = (short)id;
	b->acct = (short)acct;
	str_copy(b->name, name, sizeof(b->name));
	return b;
}

BOOK *book_by_id(int id)
{
	int i;
	for (i = 0; i < nbooks; i++)
		if (books[i].id == id)
			return &books[i];
	return 0;
}

int book_writable(const BOOK *b)
{
	return b && !b->readonly;
}

int book_for_new(void)
{
	BOOK *b = book_by_id(opt.default_book);
	int i;
	if (b && book_writable(b))
		return (int)(b - books);
	for (i = 0; i < nbooks; i++)
		if (book_writable(&books[i]))
			return i;
	return -1;
}

static DAVAUTH auth_of(int a)
{
	DAVAUTH d;
	d.user = accts[a].user[0] ? accts[a].user : 0;
	d.pass = accts[a].pass;
	return d;
}

int acct_discover(int a)
{
	DAVBOOK *db;
	int n, i, j, changed = 0;
	DAVAUTH au = auth_of(a);
	char seen[MAXBOOK];
	status("Looking for the address books of %s...", accts[a].name);
	if (!carddav_discover(accts[a].url, &au, &db, &n, ab_err, sizeof(ab_err))) {
		str_copy(accts[a].err, ab_err, sizeof(accts[a].err));
		return 0;
	}
	accts[a].err[0] = 0;
	memset(seen, 0, sizeof(seen));
	for (i = 0; i < n; i++) {
		BOOK *b = 0;
		for (j = 0; j < nbooks; j++)
			if (books[j].acct == a && !strcmp(books[j].url, db[i].url))
				b = &books[j];
		if (!b) {
			b = book_add(a, db[i].name);
			if (!b)
				break;
			str_copy(b->url, db[i].url, sizeof(b->url));
			changed++;
		} else if (strcmp(b->name, db[i].name)) {
			str_copy(b->name, db[i].name, sizeof(b->name));
			changed++;
		}
		b->readonly = (unsigned char)db[i].readonly;
		seen[b - books] = 1;
	}
	/* address books deleted on the server */
	for (j = nbooks - 1; j >= 0; j--)
		if (books[j].acct == a && !seen[j]) {
			book_delete(j);
			changed++;
		}
	free(db);
	store_save_settings();
	return 1 + changed;
}

/* ---------------- the cache files ---------------- */

/* A file written through memory (the guidelines, 6.5): what a job adds
   gathers here and goes to the file in one open, at the end (out_flush)
   or when ab_out_max is reached. No file is open meanwhile, so a job's
   waits and steps need nothing closed or opened again (2.2). With h set
   it writes straight to that open file (a cache written anew) */
typedef struct {
	char path[200];
	int h;			/* >= 0: straight to this open file */
	int fresh;		/* the first write makes the file anew */
	char *m;
	long n, cap;
	int ok;
	long base;		/* where m's first byte goes in the file */
} OUT;

long ab_out_max = 32L * 1024;
long ab_job_bytes = 64L * 1024;

static void out_file(OUT *o, const char *path, int fresh)
{
	memset(o, 0, sizeof(*o));
	str_copy(o->path, path, sizeof(o->path));
	o->h = -1;
	o->fresh = fresh;
	o->ok = 1;
}

static void out_handle(OUT *o, int h)
{
	memset(o, 0, sizeof(*o));
	o->h = h;
	o->ok = h >= 0;
}

/* d (n bytes, may be 0) after what is gathered, in one open */
static int out_put(OUT *o, const void *d, long n)
{
	int h;
	if (!o->ok)
		return 0;
	if (!o->n && !n && !o->fresh)
		return 1;
	h = pf_open(o->path, o->fresh ? PF_WRITE : PF_APPEND);
	o->ok = h >= 0 && (!o->n || pf_write(h, o->m, o->n) == o->n) && (!n || pf_write(h, d, n) == n);
	if (h >= 0)
		pf_close(h);
	o->fresh = 0;
	o->base += o->n + n;
	o->n = 0;
	return o->ok;
}

static int out_flush(OUT *o)
{
	return o->h >= 0 ? o->ok : out_put(o, 0, 0);
}

static int out_write(OUT *o, const void *d, long n)
{
	if (!o->ok)
		return 0;
	if (o->h >= 0)
		return pf_write(o->h, d, n) == n;
	if (o->n + n > ab_out_max)
		return out_put(o, d, n);	/* the buffer full: written, this with it */
	if (o->n + n > o->cap) {
		long cap = o->cap ? o->cap : 2048;
		char *m;
		while (cap < o->n + n)
			cap *= 2;
		if (cap > ab_out_max)
			cap = ab_out_max;
		if (!(m = realloc(o->m, cap)))
			return out_put(o, d, n);	/* no room: written now */
		o->m = m;
		o->cap = cap;
	}
	memcpy(o->m + o->n, d, n);
	o->n += n;
	return 1;
}

/* n bytes from off when they are still in memory: 1 */
static int out_has(const OUT *o, long off, char *buf, long n)
{
	if (o->h >= 0 || !o->m || off < o->base || off + n > o->base + o->n)
		return 0;
	memcpy(buf, o->m + (off - o->base), n);
	return 1;
}

/* its memory let go (what wasn't flushed is dropped) */
static void out_done(OUT *o)
{
	free(o->m);
	o->m = 0;
	o->n = o->cap = 0;
}

/* a buffered reader that knows where it is. Its file is opened when a
   read needs it and closed between the steps of a load (fr_park): an
   accessory's files belong to the program in front, and must not be
   kept open across an event call */
typedef struct {
	int h;			/* -1: closed, opened again at off */
	char path[200];
	char *buf;		/* big reads (6.5): each open and read costs */
	int bsize;
	int pos, len;
	long off;		/* file offset of buf[pos] */
} FR;

/* the reader's buffer: 16 KB, or ab_read_bytes (32 KB on a 68030) from
   the heap while a file is read */
long ab_read_bytes = 16384;
static char fr_small[16384];
static char *fr_big;

static void fr_open(FR *f, const char *path)
{
	if (ab_read_bytes > (long)sizeof(fr_small) && !fr_big)
		fr_big = malloc(ab_read_bytes);
	f->buf = fr_big ? fr_big : fr_small;
	f->bsize = fr_big ? (int)ab_read_bytes : (int)sizeof(fr_small);
	f->h = -1;
	str_copy(f->path, path, sizeof(f->path));
	f->pos = f->len = 0;
	f->off = 0;
}

/* the file closed; the next read opens it again where it was */
static void fr_park(FR *f)
{
	if (f->h >= 0 && f->path[0])	/* (one never opened is all 0) */
		pf_close(f->h);
	f->h = -1;
}

static void fr_close(FR *f)
{
	fr_park(f);
	f->path[0] = 0;
	f->pos = f->len = 0;
	if (fr_big) {
		free(fr_big);
		fr_big = 0;
	}
}

static int fr_fill(FR *f)
{
	if (f->pos < f->len)
		return 1;
	if (f->h < 0) {
		if (!f->path[0] || (f->h = pf_open(f->path, PF_READ)) < 0)
			return 0;
		if (f->off && pf_seek(f->h, f->off) != f->off) {
			fr_close(f);
			return 0;
		}
	}
	f->len = (int)pf_read(f->h, f->buf, f->bsize);
	f->pos = 0;
	if (f->len < 0)
		f->len = 0;
	return f->len > 0;
}

static int fr_line(FR *f, char *out, int size)
{
	int o = 0;
	for (;;) {
		char c;
		if (!fr_fill(f))
			break;
		c = f->buf[f->pos++];
		f->off++;
		if (c == '\n') {
			out[o] = 0;
			return 1;
		}
		if (o < size - 1)
			out[o++] = c;
	}
	out[o] = 0;
	return o > 0;
}

static int fr_bytes(FR *f, char *out, long n)
{
	while (n > 0) {
		int k;
		if (!fr_fill(f))
			return 0;
		k = f->len - f->pos;
		if (k > n)
			k = (int)n;
		if (out) {
			memcpy(out, f->buf + f->pos, k);
			out += k;
		}
		f->pos += k;
		f->off += k;
		n -= k;
	}
	return 1;
}

#define CACHE_VER 2

/* a card's text hashed as it is read: shifts and adds (no multiply for a
   68000), the running sum making it depend on where each byte is */
typedef struct {
	unsigned long a, b;
} TH;

static void th_add(TH *t, const char *p, long n)
{
	unsigned long a = t->a, b = t->b;
	const unsigned char *q = (const unsigned char *)p, *e = q + n;
	while (q < e) {
		/* 32 bits on any machine (the mask costs a 68000 nothing) */
		a = (((a << 5) | ((a & 0xFFFFFFFFUL) >> 27)) ^ *q++) & 0xFFFFFFFFUL;
		b = (b + a) & 0xFFFFFFFFUL;
	}
	t->a = a;
	t->b = b;
}

static unsigned long th_end(const TH *t)
{
	return (t->a ^ ((t->b << 16) | (t->b >> 16))) & 0xFFFFFFFFUL;
}

/* skipped, hashed on the way */
static int fr_hash(FR *f, long n, TH *t)
{
	while (n > 0) {
		int k;
		if (!fr_fill(f))
			return 0;
		k = f->len - f->pos;
		if (k > n)
			k = (int)n;
		th_add(t, f->buf + f->pos, k);
		f->pos += k;
		f->off += k;
		n -= k;
	}
	return 1;
}

static unsigned short href_hash(const char *s)
{
	unsigned short h = 0;
	while (*s)
		h = (unsigned short)(((h << 3) | (h >> 13)) ^ (unsigned char)*s++);
	return h;
}

#define RES_BUCKETS 256

/* An href without what all of the book's start with (iCloud's
   "/123456789/carddavhome/card/", Nextcloud's longer one): kept once in
   BOOK.hpre, not in every contact. *rel: it was taken off */
static const char *href_rest(const BOOK *b, const char *href, int *rel)
{
	*rel = b->hprelen > 0 && !strncmp(href, b->hpre, b->hprelen) && href[b->hprelen];
	return *rel ? href + b->hprelen : href;
}

/* the whole href again; a few at a time can be in use */
static const char *res_href(const BOOK *b, const RESOURCE *r)
{
	static char buf[4][520];
	static int k;
	char *o;
	if (!r->rel)
		return r->href;
	o = buf[k = (k + 1) & 3];
	snprintf(o, sizeof(buf[0]), "%s%s", b->hpre, r->href);
	return o;
}

static int live_res(BOOK *b, const char *href)
{
	int rel, i;
	const char *k = href_rest(b, href, &rel);
	unsigned short hh = href_hash(k);
	if (!b->head)
		return -1;
	for (i = b->head[hh & (RES_BUCKETS - 1)]; i >= 0; i = b->res[i].next)
		if (b->res[i].hh == hh && b->res[i].rel == rel && !b->res[i].dead && !strcmp(b->res[i].href, k))
			return i;
	return -1;
}

static void dead_res(BOOK *b, int r)
{
	b->res[r].dead = 1;
	b->ndead++;
}

static void kill_res(BOOK *b, int r)
{
	dead_res(b, r);
	people_drop((int)(b - books), r);
}

/* a contact read from the cache into the list */
/* 1 added, 0 not a card, -1 no memory for it (counted as lost) */
static int add_parsed(BOOK *b, int r, CONTACT *c)
{
	c->book = (short)(b - books);
	c->res = (short)r;
	if (opt.sort_first)
		vcard_sort_key(c, 1);
	if (!people_add(c)) {
		free(c);
		b->lost++;
		return -1;
	}
	return 1;
}

static int add_contact(BOOK *b, int r, const char *raw, long len)
{
	CONTACT *c = vcard_parse(raw, len);
	if (!c) {
		if (!vcard_nomem)
			return 0;
		b->lost++;
		return -1;
	}
	return add_parsed(b, r, c);
}

int contacts_lost(void)
{
	int i, n = 0;
	for (i = 0; i < nbooks; i++)
		n += books[i].lost;
	return n;
}

static int res_add(BOOK *b, const char *etag, const char *href, long off, long len, long poff, long plen,
		   int partial)
{
	RESOURCE *r;
	long hl, el;
	int k, rel;
	if (!etag)
		etag = "";
	el = (long)strlen(etag);
	if (b->nres >= 32000)
		return -1;		/* CONTACT.res is a short */
	if (!b->head) {
		URL u;
		if (!(b->head = malloc(RES_BUCKETS * sizeof(short))))
			return -1;
		for (k = 0; k < RES_BUCKETS; k++)
			b->head[k] = -1;
		/* the book's path, as its hrefs start (fixed while they're in memory) */
		b->hprelen = 0;
		if (b->url[0] && url_parse(b->url, &u) && strlen(u.path) < sizeof(b->hpre) &&
		    u.path[strlen(u.path) - 1] == '/') {
			str_copy(b->hpre, u.path, sizeof(b->hpre));
			b->hprelen = (short)strlen(b->hpre);
		}
	}
	href = href_rest(b, href, &rel);
	hl = (long)strlen(href);
	if (b->nres >= b->maxres) {
		RESOURCE *n;
		int max = b->maxres ? b->maxres * 2 : 64;
		n = realloc(b->res, max * sizeof(RESOURCE));
		if (!n)
			return -1;
		b->res = n;
		b->maxres = max;
	}
	r = &b->res[b->nres];
	r->href = malloc(hl + el + 2);
	if (!r->href)
		return -1;
	memcpy(r->href, href, hl + 1);
	r->etag = r->href + hl + 1;
	memcpy(r->etag, etag, el + 1);
	r->rel = (char)rel;
	r->done = 0;
	r->th = 0;
	r->hh = href_hash(href);
	r->next = b->head[r->hh & (RES_BUCKETS - 1)];
	b->head[r->hh & (RES_BUCKETS - 1)] = (short)b->nres;
	r->off = off;
	r->len = len;
	r->poff = poff;
	r->plen = plen;
	r->partial = (char)(partial != 0);
	r->dead = 0;
	return b->nres++;
}

/* Each record in BOOKnnnn.DAT is a line
     R etag<tab>href<tab>length<tab>W or P<tab>photo offset<tab>photo length
   then the card, its PHOTO line replaced by a stand-in (see vcard.h):
   the PHOTO line itself is in BOOKnnnn.PHO at that offset. P: the card
   was fetched in part (without its photo and the lines EContact doesn't
   show) and is fetched whole before it is changed. "X href" lines say a
   card is gone. EContact 1.9's cache ("CARDCACHE 1": photos in the cards,
   no last three fields) is written anew the first time it is read. */
static int parse_old;
static void load_one(BOOK *b);
static int cache_rewrite(BOOK *b, int drop, const char *href, const char *etag, const char *raw, long len,
			 int partial);

/* ---- the parsed cache, BOOKnnnn.IDX ----
 * Parsing a card takes a 68000 tens of milliseconds, so what parsing
 * made is kept: each contact as vcard_pack gave it, under the hash and
 * length of its text in BOOKnnnn.DAT. At start-up a card whose text is
 * the same is taken from here; the others are parsed, and the file
 * written again. Changes add to its end. Only for this layout and this
 * parser: the header has VC_PACK_ID and parse_id (the Makefile's sum of
 * the parser's sources), so a new version whose parser is the same
 * keeps it.
 * Hashing every card's text costs an ST seconds, so the header also says
 * which BOOKnnnn.DAT it was made from: the number on its first line
 * (written anew each time the whole file is, and only added to between),
 * or, for a file without one, its size and date. When it is that file,
 * each card is found by where it is in it, its hash taken from here,
 * and nothing is hashed. EContact 1.39's index (CARDIDX, without the
 * offsets) is still read, by the hashes, and written anew. */
#define IDX_MAGIC_OLD "CARDIDX\n"
#define IDX_MAGIC "CARDIDX2"

typedef struct {
	char magic[8];
	long packid;
	char version[16];	/* parse_id */
	unsigned long gen;	/* BOOKnnnn.DAT's number, 0: none */
	long datsize;		/* and its size and date when it has none */
	unsigned long datstamp;
} IDXHEAD;

#define IDXHEAD_OLD ((long)offsetof(IDXHEAD, gen))	/* the first three */

typedef struct {
	unsigned long th;	/* its text's hash and length, */
	long tlen, blen;
	unsigned short hh;	/* and its href's: the same card, unchanged */
	short pad;
	long off;		/* where its text is in BOOKnnnn.DAT */
} IDXENT;

#define IDXENT_OLD ((long)offsetof(IDXENT, off))	/* without off */

/* 2: this kind, 1: 1.39's, 0: neither or another parser's */
static int idx_kind(const IDXHEAD *h)
{
	int k = !memcmp(h->magic, IDX_MAGIC, 8) ? 2 : !memcmp(h->magic, IDX_MAGIC_OLD, 8) ? 1 : 0;
	return k && h->packid == VC_PACK_ID && !strncmp(h->version, parse_id, sizeof(h->version)) ? k : 0;
}

/* the book's parsed cache is this kind, to be added to */
static int idx_current(BOOK *b)
{
	char path[200];
	IDXHEAD hd;
	int h, ok;
	if (b->idxcur)
		return b->idxcur > 0;	/* known from the load or the last write */
	cache_path(b, "IDX", path, sizeof(path));
	h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	ok = pf_read(h, &hd, sizeof(hd)) == (long)sizeof(hd) && idx_kind(&hd) == 2;
	pf_close(h);
	b->idxcur = ok ? 1 : -1;
	return ok;
}

static int idx_put(OUT *h, const CONTACT *c, const RESOURCE *r)
{
	static char *buf;
	static long cap;
	IDXENT e;
	long n = c->bsize + (c->bsize & 1);
	if (n > cap) {
		free(buf);
		cap = n > 2048 ? n : 2048;
		if (!(buf = malloc(cap))) {
			cap = 0;
			return 0;
		}
	}
	if (!vcard_pack(c, buf))
		return 1;		/* not kept: parsed next time */
	buf[c->bsize] = 0;
	memset(&e, 0, sizeof(e));
	e.th = r->th;
	e.tlen = r->len;
	e.blen = c->bsize;
	e.hh = r->hh;
	e.off = r->off;
	return out_write(h, &e, sizeof(e)) && out_write(h, buf, n);
}

/* "1.23 s" */
static char *secs_of(unsigned long ms, char *out)
{
	sprintf(out, "%lu.%02lu s", ms / 1000UL, ms % 1000UL / 10UL);
	return out;
}

/* ---- reading the books, in steps ----
 * A book is read in four passes: the records alone (a card changed since
 * the file was written is in it twice, and only the later copy is read),
 * the parsed cache, the cards it didn't have, parsed, and the parsed
 * cache written again when that parsed some. Then the list is sorted, its
 * groups collected, and the names stamp made again. Each step is short (a
 * few records, one card), so the accessory can read its books between
 * event calls (common/ui/bgwork.c); book_load_all does all the steps at
 * once. */
enum { LB_IDLE, LB_START, LB_REC, LB_TRIM, LB_IDX, LB_PARSE, LB_END, LB_SORT, LB_WRITE, LB_GROUPS, LB_NAMES };

typedef struct {
	int phase, bi, last, all;	/* the book read, the last one; all: then sorted */
	int begun;			/* the pass has opened its file */
	int used, stale, parsed, full, wrote, trust, kind, inend, quiet;
	unsigned long gen, datstamp;
	long datsize, esize;
	unsigned long at, t[LB_NAMES + 1], t0, tsort;	/* the work's time in each pass */
	unsigned long names;		/* the names stamp so far */
	int r, wi, wok;
	OUT wo;				/* the parsed cache, written through memory */
	long wneed;			/* the books whose parsed cache is written after the sort */
	unsigned long wgen[MAXBOOK], wstamp[MAXBOOK];
	long wsize[MAXBOOK];
	char *raw;
	long max;
	short head[64], *next;
	RESOURCE *trim;			/* the list of records, no bigger than it is */
} LD;

static LD ld;
static void sort_begin(void);
static int sort_step(void);
static void sort_cancel(void);
static FR lf;			/* its buffer not on the accessory's stack */

/* the time since the last look, to the pass it went to */
static void ld_clock(void)
{
	unsigned long now = pf_ms();
	ld.t[ld.phase] += now - ld.at;
	ld.at = now;
}

static void ld_to(int phase)
{
	ld_clock();
	ld.phase = phase;
	ld.begun = 0;
}

static unsigned ent_bucket(const LD *l, unsigned long th, long off)
{
	return (unsigned)((l->trust ? (unsigned long)off ^ ((unsigned long)off >> 6) : th) & 63);
}

static void ld_book(int bi)
{
	char path[200], line[700];
	BOOK *b = &books[bi];
	IDXHEAD hd;
	int h, k;
	memset(ld.t, 0, sizeof(ld.t));
	ld.t0 = pf_ms();
	ld.used = ld.stale = ld.parsed = ld.full = ld.wrote = ld.trust = ld.kind = ld.quiet = 0;
	ld.gen = 0;
	ld.bi = bi;
	ld_to(LB_START);
	if (ld.all)
		status("Reading %s...", b->name);
	people_drop(bi, -1);
	book_free(b);
	b->ver = 0;
	b->lost = 0;
	cache_path(b, "DAT", path, sizeof(path));
	fr_open(&lf, path);
	if (!fr_line(&lf, line, sizeof(line)) || strncmp(line, "CARDCACHE ", 10) ||
	    (b->ver = (short)atoi(line + 10)) < 1 || b->ver > CACHE_VER) {
		b->ver = 0;
		fr_close(&lf);
		ld.quiet = 1;		/* nothing to say */
		ld_to(LB_END);
		return;
	}
	/* "CARDCACHE 2 1A2B3C4D": the number of this copy of the file */
	{
		const char *p = line + 10;
		while (*p >= '0' && *p <= '9')
			p++;
		ld.gen = *p == ' ' ? strtoul(p + 1, 0, 16) : 0;
	}
	ld.datstamp = pf_info(path, &ld.datsize);
	{
		/* room for about as many records as the file holds (a card is
		   rarely under 400 bytes): growing the list would copy it, a long
		   stretch for a 68000. The room left is given back after */
		long est = ld.datsize / 400 + 16;
		RESOURCE *r;
		if (est > 32000)
			est = 32000;
		if (est > b->maxres && (r = malloc(est * sizeof(RESOURCE))) != 0) {
			free(b->res);
			b->res = r;
			b->maxres = (int)est;
		}
	}
	/* the parsed cache made from this file: nothing is hashed */
	cache_path(b, "IDX", path, sizeof(path));
	if (b->ver == CACHE_VER && !parse_old && (h = pf_open(path, PF_READ)) >= 0) {
		memset(&hd, 0, sizeof(hd));
		k = (int)pf_read(h, &hd, sizeof(hd));
		pf_close(h);
		if (k >= IDXHEAD_OLD)
			ld.kind = idx_kind(&hd);
		b->idxcur = ld.kind == 2 && k == (int)sizeof(hd) ? 1 : -1;
		if (ld.kind == 2 && k == (int)sizeof(hd))
			ld.trust = ld.gen ? hd.gen == ld.gen
					  : !hd.gen && hd.datsize == ld.datsize && hd.datstamp == ld.datstamp && ld.datsize > 0;
	}
	ld_to(LB_REC);
}

static void ld_after_records(void);

static void ld_next_book(void)
{
	if (ld.bi < ld.last)
		ld_book(ld.bi + 1);
	else if (ld.all)
		ld_to(LB_SORT);
	else
		ld_to(LB_IDLE);
}

/* up to 4 records (hashing them takes a 68000 about 10 ms each) */
static void ld_records(void)
{
	BOOK *b = &books[ld.bi];
	char line[700];
	int n;
	for (n = 0; n < 4; n++) {
		char *fld[6], *p;
		long len, poff = -1, plen = 0;
		int k, partial = 0, old;
		if (!fr_line(&lf, line, sizeof(line)))
			break;
		if (line[0] == 'X' && line[1] == ' ') {
			/* a contact deleted since the file was written */
			old = live_res(b, line + 2);
			if (old >= 0)
				dead_res(b, old);
			continue;
		}
		if (line[0] != 'R' || line[1] != ' ')
			break;
		for (k = 0, p = line + 2; k < 6 && p; k++) {
			fld[k] = p;
			p = strchr(p, '\t');
			if (p)
				*p++ = 0;
		}
		if (k < 3)
			break;
		len = atol(fld[2]);
		if (k == 6) {
			partial = fld[3][0] == 'P';
			poff = atol(fld[4]);
			plen = atol(fld[5]);
		}
		old = live_res(b, fld[1]);
		if (old >= 0)
			dead_res(b, old);	/* a newer copy, appended when it changed */
		k = res_add(b, fld[0], fld[1], lf.off, len, poff, plen, partial);
		if (k < 0) {
			b->lost++;		/* no memory: counted, the rest still looked at */
			if (!fr_bytes(&lf, 0, len + 1))
				break;
		} else if (ld.trust) {
			if (!fr_bytes(&lf, 0, len + 1))	/* its hash comes from the index */
				break;
		} else {
			TH t = { 0, 0 };
			if (!fr_hash(&lf, len, &t) || !fr_bytes(&lf, 0, 1))
				break;
			b->res[k].th = th_end(&t);
		}
	}
	if (n == 4)
		return;
	fr_close(&lf);
	/* the list of them no bigger than it is: the room it leaves is taken
	   by the cards read next. Copied a part at a time (24 KB for 600
	   takes an ST 30 ms) */
	if (b->maxres > b->nres + b->nres / 8 + 8 && (ld.trim = malloc((b->nres + 8) * sizeof(RESOURCE))) != 0) {
		ld.r = 0;
		ld_to(LB_TRIM);
		return;
	}
	ld_after_records();
}

static void ld_trim(void)
{
	BOOK *b = &books[ld.bi];
	int n = b->nres - ld.r < 128 ? b->nres - ld.r : 128;
	memcpy(ld.trim + ld.r, b->res + ld.r, n * sizeof(RESOURCE));
	ld.r += n;
	if (ld.r < b->nres)
		return;
	free(b->res);
	b->res = ld.trim;
	b->maxres = b->nres + 8;
	ld.trim = 0;
	ld_after_records();
}

static void ld_after_records(void)
{
	BOOK *b = &books[ld.bi];
	if (b->ver != CACHE_VER && !parse_old)
		ld_to(LB_END);		/* the old kind is only listed: read after it is written anew */
	else
		ld_to(ld.kind ? LB_IDX : LB_PARSE);
}

/* the cards as they were parsed before, where their text is the same:
   up to 8 of them */
static void ld_index(void)
{
	BOOK *b = &books[ld.bi];
	IDXHEAD hd;
	IDXENT e;
	int n, r;
	if (!ld.begun) {
		char path[200];
		ld.begun = 1;
		cache_path(b, "IDX", path, sizeof(path));
		fr_open(&lf, path);
		ld.esize = ld.kind == 2 ? (long)sizeof(IDXENT) : IDXENT_OLD;
		ld.next = malloc((b->nres + 1) * sizeof(short));
		if (!ld.next || !fr_bytes(&lf, (char *)&hd, ld.kind == 2 ? (long)sizeof(hd) : IDXHEAD_OLD)) {
			ld.esize = 0;	/* ends at once */
		} else {
			/* the cards to find, by hash, or by where they are */
			for (r = 0; r < 64; r++)
				ld.head[r] = -1;
			for (r = b->nres - 1; r >= 0; r--)
				if (!b->res[r].dead) {
					unsigned k = ent_bucket(&ld, b->res[r].th, b->res[r].off);
					ld.next[r] = ld.head[k];
					ld.head[k] = (short)r;
				}
		}
	}
	for (n = 0; n < 8; n++) {
		long sz;
		char *blk;
		CONTACT *c;
		memset(&e, 0, sizeof(e));
		if (!ld.esize || !fr_bytes(&lf, (char *)&e, ld.esize))
			break;
		sz = e.blen + (e.blen & 1);
		if (e.blen < (long)sizeof(CONTACT) || e.blen > 262144L)
			break;
		for (r = ld.head[ent_bucket(&ld, e.th, e.off)]; r >= 0; r = ld.next[r]) {
			RESOURCE *rs = &b->res[r];
			if (rs->len == e.tlen && rs->hh == e.hh && !rs->done &&
			    (ld.trust ? rs->off == e.off : rs->th == e.th))
				break;
		}
		if (r < 0 || !(blk = malloc(e.blen))) {
			ld.stale++;
			if (!fr_bytes(&lf, 0, sz))
				break;
			continue;
		}
		if (!fr_bytes(&lf, blk, e.blen) || (sz > e.blen && !fr_bytes(&lf, 0, 1))) {
			free(blk);
			break;
		}
		c = vcard_unpack(blk, e.blen);
		if (!c) {
			free(blk);
			ld.stale++;
			continue;
		}
		if (ld.trust)
			b->res[r].th = e.th;
		c->book = (short)ld.bi;
		c->res = (short)r;
		vcard_sort_key(c, opt.sort_first);
		b->res[r].done = 1;	/* when it doesn't fit, not parsed again to fail again */
		if (!people_add(c)) {
			free(c);
			b->lost++;
		} else {
			ld.used++;
		}
	}
	if (n == 8)
		return;
	free(ld.next);
	ld.next = 0;
	fr_close(&lf);
	ld_to(LB_PARSE);
}

/* then the rest, parsed: one card */
static void ld_parse(void)
{
	BOOK *b = &books[ld.bi];
	RESOURCE *rs;
	if (!ld.begun) {
		char path[200];
		ld.begun = 1;
		cache_path(b, "DAT", path, sizeof(path));
		fr_open(&lf, path);
		ld.r = 0;
		ld.raw = 0;
		ld.max = 0;
		return;			/* a step of its own */
	}
	for (; ld.r < b->nres; ld.r++) {
		rs = &b->res[ld.r];
		if (rs->dead || rs->done || rs->len >= 262144L)
			continue;
		if (ld.full) {
			/* memory is full: the rest counted as left out, not
			   parsed at every start only to be let go again */
			b->lost++;
			continue;
		}
		break;
	}
	if (ld.r < b->nres) {
		rs = &b->res[ld.r];
		if (rs->len + 1 > ld.max) {
			free(ld.raw);
			ld.max = rs->len + 1 > 4096 ? rs->len + 1 : 4096;
			ld.raw = malloc(ld.max);
			if (!ld.raw) {
				ld.max = 0;
				b->lost++;
				ld.r++;
				return;
			}
		}
		/* forward to it, past the copies that aren't read */
		if (rs->off >= lf.off && fr_bytes(&lf, 0, rs->off - lf.off) && fr_bytes(&lf, ld.raw, rs->len)) {
			ld.raw[rs->len] = 0;
			if (ld.trust) {
				/* not in the index: hashed now */
				TH t = { 0, 0 };
				th_add(&t, ld.raw, rs->len);
				rs->th = th_end(&t);
			}
			switch (add_contact(b, ld.r, ld.raw, rs->len)) {
			case 1: ld.parsed++; break;
			case -1: ld.full = 1; break;
			}
			ld.r++;
			return;
		}
	}
	free(ld.raw);
	ld.raw = 0;
	fr_close(&lf);
	/* the parsed cache written again when cards were parsed, when it
	   holds many that are gone, or was made from another copy of the
	   file (or is 1.39's) */
	if (b->ver == CACHE_VER && !parse_old && ld.used + ld.parsed > 0 &&
	    (ld.parsed > 0 || (ld.stale > 16 && ld.stale > ld.used / 4) || !ld.trust)) {
		/* the file as it was when the book was begun */
		ld.wgen[ld.bi] = ld.gen;
		ld.wsize[ld.bi] = ld.gen ? 0 : ld.datsize;
		ld.wstamp[ld.bi] = ld.gen ? 0 : ld.datstamp;
		if (ld.all) {
			/* after the sort: in order, so the next start needn't sort */
			ld.wneed |= 1L << ld.bi;
			ld_to(LB_END);
		} else {
			ld_to(LB_WRITE);
		}
	} else {
		ld_to(LB_END);
	}
}

/* the next book whose parsed cache is to be written, after the sort */
static void ld_next_write(void)
{
	int i;
	for (i = 0; i < nbooks; i++)
		if (ld.wneed & (1L << i)) {
			ld.wneed &= ~(1L << i);
			ld.bi = i;
			ld_to(LB_WRITE);
			return;
		}
	ld_to(LB_GROUPS);
}

/* the parsed cache written again: up to 4 contacts */
static void ld_write(void)
{
	BOOK *b = &books[ld.bi];
	char path[200];
	int n = 0;
	cache_path(b, "IDX", path, sizeof(path));
	if (!ld.begun) {
		IDXHEAD hd;
		ld.begun = 1;
		ld.wi = 0;
		ld.wok = 1;
		ld.wrote = 1;
		ld.t[LB_WRITE] = 0;
		out_file(&ld.wo, path, 1);
		memset(&hd, 0, sizeof(hd));
		memcpy(hd.magic, IDX_MAGIC, 8);
		hd.packid = VC_PACK_ID;
		str_copy(hd.version, parse_id, sizeof(hd.version));
		hd.gen = ld.wgen[ld.bi];
		hd.datsize = ld.wsize[ld.bi];
		hd.datstamp = ld.wstamp[ld.bi];
		ld.wok = out_write(&ld.wo, &hd, sizeof(hd));
	}
	for (; ld.wi < npeople && ld.wok && n < 4; ld.wi++) {
		CONTACT *c = people[ld.wi];
		if (c->book == ld.bi && c->res >= 0 && c->res < b->nres) {
			ld.wok = idx_put(&ld.wo, c, &b->res[c->res]);
			n++;
		}
	}
	if (ld.wok && ld.wi < npeople)
		return;
	ld.wok = ld.wok && out_flush(&ld.wo);	/* the file in one go */
	out_done(&ld.wo);
	b->idxcur = ld.wok ? 1 : -1;
	if (!ld.wok)
		pf_remove(path);
	if (ld.all) {
		char m[100], a[16];
		ld_clock();
		snprintf(m, sizeof(m), "%.40s: the index written %s", b->name, secs_of(ld.t[LB_WRITE], a));
		conn_log("Contacts", " -- ", m, (long)strlen(m));
		ld_next_write();
	} else {
		ld_to(LB_END);
	}
}

static void ld_end(void)
{
	BOOK *b = &books[ld.bi];
	if (!ld.quiet) {
		/* in the log: where the start-up's time goes */
		char m[340], a[16], r[16], i[16], p[16], w[16];
		unsigned long all = ld.t[LB_START] + ld.t[LB_REC] + ld.t[LB_IDX] + ld.t[LB_PARSE] + ld.t[LB_WRITE];
		int k = snprintf(m, sizeof(m), "%.40s: %d cards in %s: the records %s%s, %d from the index %s, %d parsed %s%s%s",
				 b->name, ld.used + ld.parsed, secs_of(all, a), secs_of(ld.t[LB_START] + ld.t[LB_REC], r),
				 ld.trust ? " (not hashed)" : "", ld.used, secs_of(ld.t[LB_IDX], i), ld.parsed,
				 secs_of(ld.t[LB_PARSE], p),
				 ld.wrote ? ", the index written " : (ld.wneed & (1L << ld.bi)) ? ", the index written after the sort" : "",
				 ld.wrote ? secs_of(ld.t[LB_WRITE], w) : "");
		if (b->lost && k > 0 && k < (int)sizeof(m))
			k += snprintf(m + k, sizeof(m) - k, "; %d left out, no memory for them", b->lost);
		if (pf_ms() - ld.t0 > all + 1000UL && k > 0 && k < (int)sizeof(m))
			snprintf(m + k, sizeof(m) - k, "; in steps over %s", secs_of(pf_ms() - ld.t0, a));
		conn_log("Contacts", " -- ", m, (long)strlen(m));
	}
	ld.inend = 1;			/* a load from here (cache_rewrite's) is its own */
	if (b->ver && b->ver != CACHE_VER && !parse_old) {
		status("Converting %s...", b->name);
		if (!cache_rewrite(b, -1, 0, 0, 0, 0, 0)) {
			/* the disk is full: read as it is, written anew another time */
			parse_old = 1;
			load_one(b);
			parse_old = 0;
		}
	}
	ld.inend = 0;
	ld_next_book();
}

static void ld_step(void)
{
	switch (ld.phase) {
	case LB_START: ld_book(ld.bi); break;
	case LB_REC: ld_records(); break;
	case LB_TRIM: ld_trim(); break;
	case LB_IDX: ld_index(); break;
	case LB_PARSE: ld_parse(); break;
	case LB_WRITE: ld_write(); break;
	case LB_END: ld_end(); break;
	case LB_SORT:
		if (!ld.begun) {
			ld.begun = 1;
			people_serial++;
			sort_begin();
		}
		if (!sort_step()) {
			count_books();
			ld_next_write();
		}
		break;
	case LB_GROUPS:
		groups_collect();
		ld_to(LB_NAMES);
		break;
	case LB_NAMES: {
		/* the names stamp (names_stamp), 64 contacts a step */
		char m[60], a[16];
		int n;
		if (!ld.begun) {
			ld.begun = 1;
			ld.r = 0;
			ld.names = 0x2C1B3C6DUL + opt.last_first;
		}
		for (n = 0; n < 64 && ld.r < npeople; n++, ld.r++)
			ld.names = names_of(ld.names, people[ld.r]);
		if (ld.r < npeople)
			break;
		people_names_stamp = ld.names & 0xFFFFFFFFUL;
		ld_to(LB_IDLE);
		snprintf(m, sizeof(m), "the list sorted: %s",
			 secs_of(ld.t[LB_SORT] + ld.t[LB_GROUPS] + ld.t[LB_NAMES], a));
		conn_log("Contacts", " -- ", m, (long)strlen(m));
		conn_log_flush();
		status_clear();
		break;
	}
	default:
		ld_to(LB_IDLE);
	}
}

/* the steps until there are none left, or more() says the time is up */
static int ld_run(int (*more)(void))
{
	ld.at = pf_ms();
	/* big reads, in steps too (6.5): 16 KB on a 68000, 32 KB on a 68030,
	   each about a step's time on the disk */
	while (ld.phase != LB_IDLE && (!more || more()))
		ld_step();
	fr_park(&lf);
	ld_clock();
	return ld.phase != LB_IDLE;
}

void books_load_begin(void)
{
	books_load_cancel();
	ld.all = 1;
	ld.last = nbooks - 1;
	ld.bi = 0;
	if (nbooks > 0) {
		status("Reading your contacts...");
		ld.phase = LB_START;
	} else {
		ld.phase = LB_SORT;
	}
}

int books_load_run(int (*more)(void))
{
	return ld.phase != LB_IDLE && ld_run(more);
}

int books_loading(void)
{
	return ld.phase != LB_IDLE;
}

/* what was read so far stays, to be let go (store_release) or read again */
void books_load_cancel(void)
{
	fr_close(&lf);
	if (ld.phase != LB_IDLE) {
		out_done(&ld.wo);
		free(ld.next);
		free(ld.raw);
		free(ld.trim);
	}
	memset(&ld, 0, sizeof(ld));
	sort_cancel();
}

static void load_one(BOOK *b)
{
	LD keep;
	if (ld.phase != LB_IDLE && !ld.inend)
		ld_run(0);		/* a load in steps finished first */
	keep = ld;
	memset(&ld, 0, sizeof(ld));
	ld.bi = ld.last = (int)(b - books);
	ld.phase = LB_START;
	ld_run(0);
	ld = keep;
}

void book_load(BOOK *b)
{
	load_one(b);
	people_changed(1);
}

void book_load_all(void)
{
	books_load_begin();
	ld_run(0);
}

/* A card into the files: its PHOTO line to the end of the photo file
   (at poff), a stand-in in its place. *at and *n: where the card starts
   in what was written to h, and its length; *plen: the photo's */
static int rec_write(OUT *h, OUT *ph, long poff, const char *href, const char *etag, const char *raw, long len,
		     int *partial, long *at, long *n, long *plen, unsigned long *th)
{
	TH t = { 0, 0 };
	long s = 0, e = 0;
	char mark[40], head[700];
	int ml = 0, hl;
	*plen = 0;
	if (vcard_photo_span(raw, len, &s, &e)) {
		if (vcard_photo_empty(raw + s, e - s)) {
			/* a card without its photo is never written back as it is */
			ml = snprintf(mark, sizeof(mark), "%s:?\r\n", VC_PHOTO_MARK);
			*partial = 1;
		} else {
			ml = snprintf(mark, sizeof(mark), "%s:%08lX\r\n", VC_PHOTO_MARK, vcard_photo_key(raw + s, e - s));
			*plen = e - s;
		}
	}
	*n = ml ? s + ml + (len - e) : len;
	hl = snprintf(head, sizeof(head), "R %s\t%s\t%ld\t%c\t%ld\t%ld\n", etag ? etag : "", href, *n,
		      *partial ? 'P' : 'W', *plen ? poff : -1L, *plen);
	*at = hl;
	if (*plen && (!ph || !out_write(ph, raw + s, *plen)))
		return 0;
	if (!out_write(h, head, hl))
		return 0;
	if (ml) {
		if (!out_write(h, raw, s) || !out_write(h, mark, ml) || !out_write(h, raw + e, len - e))
			return 0;
		th_add(&t, raw, s);
		th_add(&t, mark, ml);
		th_add(&t, raw + e, len - e);
	} else {
		if (!out_write(h, raw, len))
			return 0;
		th_add(&t, raw, len);
	}
	if (th)
		*th = th_end(&t);
	return out_write(h, "\n", 1);
}

/* writing new cache files: header, then resources one by one */
typedef struct {
	BOOK *b;
	int h, ph;		/* the new files */
	int old, oldp;		/* the old ones, for copying */
	long plen;		/* the new photo file so far */
	char tmp[200], ptmp[200];
	int failed;
} CW;

static int cw_open(CW *w, BOOK *b)
{
	char path[200];
	memset(w, 0, sizeof(*w));
	w->b = b;
	cache_path(b, "TMP", w->tmp, sizeof(w->tmp));
	cache_path(b, "PHT", w->ptmp, sizeof(w->ptmp));
	w->h = pf_open(w->tmp, PF_WRITE);
	w->ph = w->h >= 0 ? pf_open(w->ptmp, PF_WRITE) : -1;
	if (w->ph < 0) {
		if (w->h >= 0) {
			pf_close(w->h);
			pf_remove(w->tmp);
		}
		snprintf(ab_err, sizeof(ab_err), "can't write the address book's cache (disk full or locked?)");
		return 0;
	}
	cache_path(b, "DAT", path, sizeof(path));
	w->old = pf_open(path, PF_READ);
	cache_path(b, "PHO", path, sizeof(path));
	w->oldp = pf_open(path, PF_READ);
	{
		/* a new number for this copy of the file (the parsed cache says
		   which it was made from) */
		unsigned char g[4];
		char head[24];
		unsigned long gen;
		pf_entropy(g, 4);
		gen = ((unsigned long)g[0] << 24 | (unsigned long)g[1] << 16 | (unsigned)g[2] << 8 | g[3]) & 0xFFFFFFFFUL;
		snprintf(head, sizeof(head), "CARDCACHE 2 %08lX\n", gen ? gen : 1UL);
		pf_write(w->h, head, (long)strlen(head));
	}
	return 1;
}

static void cw_add(CW *w, const char *href, const char *etag, const char *raw, long len, int partial)
{
	long at, n, plen;
	OUT d, p;
	out_handle(&d, w->h);
	out_handle(&p, w->ph);
	if (!rec_write(&d, &p, w->plen, href, etag, raw, len, &partial, &at, &n, &plen, 0))
		w->failed = 1;
	w->plen += plen;
}

static int read_at(int h, long off, char *buf, long n)
{
	return h >= 0 && pf_seek(h, off) == off && pf_read(h, buf, n) == n;
}

static void cw_copy(CW *w, const RESOURCE *r)
{
	char *raw, *pho = 0, head[700];
	int hl;
	if (w->old < 0 || !(raw = malloc(r->len + 1)))
		return;
	if (!read_at(w->old, r->off, raw, r->len)) {
		w->failed = 1;
	} else if (w->b->ver != CACHE_VER) {
		cw_add(w, res_href(w->b, r), r->etag, raw, r->len, 0);	/* the old kind: its photo comes out */
	} else if (r->plen > 0 && (!(pho = malloc(r->plen)) || !read_at(w->oldp, r->poff, pho, r->plen))) {
		/* the photo can't be read: the card stays, as one to fetch again */
		hl = snprintf(head, sizeof(head), "R \t%s\t%ld\tP\t-1\t0\n", res_href(w->b, r), r->len);
		if (pf_write(w->h, head, hl) != hl || pf_write(w->h, raw, r->len) != r->len || pf_write(w->h, "\n", 1) != 1)
			w->failed = 1;
	} else {
		/* as it is: the card with its stand-in, the photo moved along */
		hl = snprintf(head, sizeof(head), "R %s\t%s\t%ld\t%c\t%ld\t%ld\n", r->etag, res_href(w->b, r), r->len,
			      r->partial ? 'P' : 'W', r->plen > 0 ? w->plen : -1L, r->plen > 0 ? r->plen : 0L);
		if ((r->plen > 0 && pf_write(w->ph, pho, r->plen) != r->plen) || pf_write(w->h, head, hl) != hl ||
		    pf_write(w->h, raw, r->len) != r->len || pf_write(w->h, "\n", 1) != 1)
			w->failed = 1;
		if (r->plen > 0)
			w->plen += r->plen;
	}
	free(pho);
	free(raw);
}

static void cw_close_old(CW *w)
{
	if (w->old >= 0)
		pf_close(w->old);
	if (w->oldp >= 0)
		pf_close(w->oldp);
	pf_close(w->h);
	pf_close(w->ph);
}

static void cw_abandon(CW *w)
{
	cw_close_old(w);
	pf_remove(w->tmp);
	pf_remove(w->ptmp);
}

static int cw_close(CW *w)
{
	char path[200];
	cw_close_old(w);
	if (w->failed) {
		pf_remove(w->tmp);
		pf_remove(w->ptmp);
		snprintf(ab_err, sizeof(ab_err), "couldn't write the address book's cache (disk full?)");
		return 0;
	}
	cache_path(w->b, "PHO", path, sizeof(path));
	pf_remove(path);
	if (w->plen)
		pf_rename(w->ptmp, path);
	else
		pf_remove(w->ptmp);
	cache_path(w->b, "DAT", path, sizeof(path));
	pf_remove(path);
	pf_rename(w->tmp, path);
	return 1;
}

/* the whole cache written again, without the superseded copies */
static int cache_rewrite(BOOK *b, int drop, const char *href, const char *etag, const char *raw, long len,
			 int partial)
{
	CW w;
	int i;
	if (!cw_open(&w, b))
		return 0;
	for (i = 0; i < b->nres; i++)
		if (i != drop && !b->res[i].dead && !(href && !strcmp(res_href(b, &b->res[i]), href)))
			cw_copy(&w, &b->res[i]);
	if (href)
		cw_add(&w, href, etag, raw, len, partial);
	if (!cw_close(&w))
		return 0;
	book_load(b);
	return 1;
}

/* adding to the end of the files: a changed contact, or the whole copies
   of ones fetched in part. Only those are read again. What is added
   gathers in memory and is written at aw_close, one open a file (6.5);
   no file is open in between, so a session may span a network wait or
   steps */
typedef struct {
	BOOK *b;
	OUT d, p, i;		/* the cards, the photos, the parsed cache */
	int idx, want;		/* the parsed cache is added to; it was to be */
	long size, psize;
	int ok;
} AW;

static int aw_open(AW *w, BOOK *b)
{
	char path[200];
	memset(w, 0, sizeof(*w));
	w->b = b;
	cache_path(b, "DAT", path, sizeof(path));
	pf_info(path, &w->size);
	/* written anew when more than half of it is superseded copies */
	if (w->size <= 0 || b->ver != CACHE_VER || (b->ndead >= 32 && 2 * b->ndead > b->nres))
		return 0;
	out_file(&w->d, path, 0);
	w->d.base = w->size;
	cache_path(b, "PHO", path, sizeof(path));
	pf_info(path, &w->psize);
	if (w->psize < 0)
		w->psize = 0;
	out_file(&w->p, path, 0);
	w->p.base = w->psize;
	/* the parsed cache is added to only when it is there, and this kind */
	cache_path(b, "IDX", path, sizeof(path));
	w->idx = w->want = idx_current(b);
	out_file(&w->i, path, 0);
	w->ok = 1;
	return 1;
}

static void aw_drop(AW *w, int r)
{
	char head[700];
	int n = snprintf(head, sizeof(head), "X %s\n", res_href(w->b, &w->b->res[r]));
	if (w->ok && !out_write(&w->d, head, n))
		w->ok = 0;
	w->size += n;
	kill_res(w->b, r);
}

/* pre: the card parsed already (it is taken), or 0 */
static int aw_add_pre(AW *w, const char *href, const char *etag, const char *raw, long len, int partial,
		      CONTACT *pre)
{
	long at, n, plen;
	int old, r;
	unsigned long th;
	if (!w->ok) {
		free(pre);
		return 0;
	}
	old = live_res(w->b, href);
	if (old >= 0)
		kill_res(w->b, old);
	if (!rec_write(&w->d, &w->p, w->psize, href, etag, raw, len, &partial, &at, &n, &plen, &th)) {
		w->ok = 0;
		free(pre);
		return 0;
	}
	r = res_add(w->b, etag, href, w->size + at, n, plen ? w->psize : -1L, plen, partial);
	w->size += at + n + 1;
	w->psize += plen;
	if (r >= 0) {
		int before = npeople;
		w->b->res[r].th = th;
		if (pre)
			add_parsed(w->b, r, pre);
		else
			add_contact(w->b, r, raw, len);
		pre = 0;
		/* and its parsed self at the end of the parsed cache */
		if (w->idx && npeople > before && !idx_put(&w->i, people[npeople - 1], &w->b->res[r]))
			w->idx = 0;	/* removed at aw_close */
	}
	free(pre);			/* not used: no room in the list */
	return 1;
}

static int aw_add(AW *w, const char *href, const char *etag, const char *raw, long len, int partial)
{
	return aw_add_pre(w, href, etag, raw, len, partial, 0);
}

/* the session's bytes written, one open a file. 0: the disk refused the
   end of a file: the caller writes it all again */
static int aw_close(AW *w)
{
	int ok = w->ok;
	/* the photos first: a card is never there without its photo */
	ok = out_flush(&w->p) && ok;
	ok = out_flush(&w->d) && ok;
	if (w->want && (!w->idx || !out_flush(&w->i))) {
		/* the parsed cache couldn't be added to: made again next time */
		pf_remove(w->i.path);
		w->b->idxcur = -1;
	}
	out_done(&w->p);
	out_done(&w->d);
	out_done(&w->i);
	return ok;
}

static AW fj;			/* the job of fetches (book_fetch_job), in memory */
static int fj_bi = -1;

/* One contact changed (or gone): the new copy is added to the end of the
   cache file, and only that contact is read again. When superseded
   copies pile up the file is written anew. */
static int cache_change(BOOK *b, int drop, const char *href, const char *etag, const char *raw, long len,
			int partial)
{
	AW w;
	if (!aw_open(&w, b))
		return cache_rewrite(b, drop, href, etag, raw, len, partial);
	/* the old copy goes */
	if (drop >= 0 && !b->res[drop].dead && (!href || strcmp(res_href(b, &b->res[drop]), href)))
		aw_drop(&w, drop);
	if (href)
		aw_add(&w, href, etag, raw, len, partial);
	if (!aw_close(&w)) {
		load_one(b);
		return cache_rewrite(b, -1, 0, 0, 0, 0, 0);
	}
	people_changed(1);
	return 1;
}

static RESOURCE *res_of(const CONTACT *c)
{
	if (!c || c->book < 0 || c->book >= nbooks || c->res < 0 || c->res >= books[c->book].nres)
		return 0;
	return &books[c->book].res[c->res];
}

int contact_partial(const CONTACT *c)
{
	RESOURCE *r = res_of(c);
	return r && r->partial;
}

const char *contact_href(const CONTACT *c)
{
	RESOURCE *r = res_of(c);
	return r ? res_href(&books[c->book], r) : "";
}

char *contact_raw(const CONTACT *c, long *len)
{
	char path[200], *raw, *m, *me, *out;
	int h;
	RESOURCE *r = res_of(c);
	BOOK *b;
	long ml = (long)strlen(VC_PHOTO_MARK), n;
	if (!r)
		return 0;
	b = &books[c->book];
	raw = malloc(r->len + 1);
	if (!raw)
		return 0;
	/* a card the job fetched is still in its memory */
	if (!(fj_bi == c->book && out_has(&fj.d, r->off, raw, r->len))) {
		cache_path(b, "DAT", path, sizeof(path));
		h = pf_open(path, PF_READ);
		if (!read_at(h, r->off, raw, r->len)) {
			free(raw);
			raw = 0;
		}
		if (h >= 0)
			pf_close(h);
		if (!raw)
			return 0;
	}
	raw[r->len] = 0;
	n = r->len;
	/* the stand-in: the photo back in its place, or the line out */
	for (m = raw; (m = strstr(m, VC_PHOTO_MARK)) != 0; m += ml)
		if ((m == raw || m[-1] == '\n') && m[ml] == ':')
			break;
	if (m) {
		me = strchr(m, '\n');
		me = me ? me + 1 : raw + n;
		out = malloc(n - (me - m) + r->plen + 1);
		if (!out) {
			free(raw);
			return 0;
		}
		memcpy(out, raw, m - raw);
		n = m - raw;
		if (r->plen > 0 && fj_bi == c->book && out_has(&fj.p, r->poff, out + n, r->plen)) {
			n += r->plen;
		} else if (r->plen > 0) {
			cache_path(b, "PHO", path, sizeof(path));
			h = pf_open(path, PF_READ);
			if (read_at(h, r->poff, out + n, r->plen))
				n += r->plen;
			if (h >= 0)
				pf_close(h);
		}
		memcpy(out + n, me, r->len - (me - raw));
		n += r->len - (me - raw);
		out[n] = 0;
		free(raw);
		raw = out;
	}
	if (len)
		*len = n;
	return raw;
}

/* ---------------- syncing ---------------- */

/* etags compare without quotes and without a weak W/ */
static int same_etag(const char *a, const char *b)
{
	int la, lb;
	if (!strncmp(a, "W/", 2))
		a += 2;
	if (!strncmp(b, "W/", 2))
		b += 2;
	if (*a == '"')
		a++;
	if (*b == '"')
		b++;
	la = (int)strlen(a);
	lb = (int)strlen(b);
	if (la && a[la - 1] == '"')
		la--;
	if (lb && b[lb - 1] == '"')
		lb--;
	return la == lb && !strncmp(a, b, la) && la > 0;
}

/* a card that came, kept in memory until it is written (quiet_sync) */
typedef struct {
	char *href, *etag, *data;	/* one block */
	long len;
	int partial;
	CONTACT *c;			/* parsed before it is written */
} QCARD;

typedef struct {
	CW *w;
	AW *aw;
	BOOK *b;
	int n, of;
	QCARD *q;		/* neither file: into this list */
	int nq, maxq;
} MG;

static int mget_cb(void *ud, const char *href, const char *etag, const char *data, long len, int partial)
{
	MG *m = ud;
	if (++m->n % 10 == 0 && m->of > 1)
		status("%s: %d of %d...", m->b->name, m->n, m->of);
	if (m->aw)
		return aw_add(m->aw, href, etag, data, len, partial);
	if (m->w) {
		cw_add(m->w, href, etag, data, len, partial);
		return !m->w->failed;
	}
	{
		long hl = (long)strlen(href), el = (long)strlen(etag ? etag : "");
		QCARD *c;
		if (m->nq >= m->maxq) {
			int mx = m->maxq ? m->maxq * 2 : 8;
			QCARD *nq = realloc(m->q, mx * sizeof(QCARD));
			if (!nq)
				return 0;
			m->q = nq;
			m->maxq = mx;
		}
		c = &m->q[m->nq];
		if (!(c->href = malloc(hl + el + len + 3)))
			return 0;
		memcpy(c->href, href, hl + 1);
		c->etag = c->href + hl + 1;
		memcpy(c->etag, etag ? etag : "", el + 1);
		c->data = c->etag + el + 1;
		memcpy(c->data, data, len);
		c->data[len] = 0;
		c->len = len;
		c->partial = partial;
		c->c = 0;
		m->nq++;
		if (http_yield)
			http_yield();	/* (an answer of many cards is read at once) */
		return 1;
	}
}

/* the server's list, compared with the cache as it comes: only the
   hrefs to fetch are kept. New ones (and ones we have in part) are
   fetched without their photos; changed ones we have whole are fetched
   whole, so a changed photo is seen */
typedef struct {
	BOOK *b;
	unsigned char *keep;
	char **part, **whole;
	int npart, nwhole, maxpart, maxwhole;
	int nomem;
} LCMP;

static int add_href(char ***list, int *n, int *max, const char *href)
{
	if (*n >= *max) {
		int m = *max ? *max * 2 : 64;
		char **nl = realloc(*list, m * sizeof(char *));
		if (!nl)
			return 0;
		*list = nl;
		*max = m;
	}
	if (!((*list)[*n] = strdup(href)))
		return 0;
	(*n)++;
	return 1;
}

static int list_cmp(void *ud, const char *href, const char *etag)
{
	LCMP *l = ud;
	int found = live_res(l->b, href), ok;
	if (found >= 0 && same_etag(l->b->res[found].etag, etag)) {
		l->keep[found] = 1;
		return 1;
	}
	if (found >= 0 && !l->b->res[found].partial)
		ok = add_href(&l->whole, &l->nwhole, &l->maxwhole, href);
	else
		ok = add_href(&l->part, &l->npart, &l->maxpart, href);
	if (!ok)
		l->nomem = 1;
	return ok;
}

static void lcmp_free(LCMP *l)
{
	int i;
	for (i = 0; i < l->npart; i++)
		free(l->part[i]);
	for (i = 0; i < l->nwhole; i++)
		free(l->whole[i]);
	free(l->part);
	free(l->whole);
	free(l->keep);
}

int sync_book(BOOK *b, int force)
{
	DAVAUTH au;
	char ctag[100];
	int j, nkept, r = 0, skipped;
	CW w;
	MG m;
	LCMP l;
	ACCOUNT *a = &accts[b->acct];
	if (a->kind == AK_LOCAL)
		return 0;
	ab_err[0] = 0;
	au = auth_of(b->acct);
	status("%s: checking...", b->name);
	if (!carddav_ctag(b->url, &au, ctag, sizeof(ctag), ab_err, sizeof(ab_err))) {
		r = -1;
		goto out;
	}
	if (!force && ctag[0] && !strcmp(ctag, b->ctag) && b->synced && b->ver)
		goto out;
	status("%s: what has changed...", b->name);
	memset(&l, 0, sizeof(l));
	l.b = b;
	l.keep = calloc(b->nres + 1, 1);
	if (!l.keep || !carddav_list(b->url, &au, list_cmp, &l, ab_err, sizeof(ab_err))) {
		if (!l.keep || l.nomem)
			str_copy(ab_err, "not enough memory to compare the contacts with the server's", sizeof(ab_err));
		lcmp_free(&l);
		r = -1;
		goto out;
	}
	if (!cw_open(&w, b)) {
		lcmp_free(&l);
		r = -1;
		goto out;
	}
	for (j = 0, nkept = 0; j < b->nres; j++)
		if (l.keep[j]) {
			cw_copy(&w, &b->res[j]);
			nkept++;
		}
	if (!l.npart && !l.nwhole && nkept == b->nres - b->ndead && b->ver == CACHE_VER) {
		/* the ctag moved (our own changes, say) but nothing is new */
		cw_abandon(&w);
		lcmp_free(&l);
		str_copy(b->ctag, ctag, sizeof(b->ctag));
		b->synced = now_local();
		goto out;
	}
	memset(&m, 0, sizeof(m));
	m.w = &w;
	m.b = b;
	m.of = l.npart + l.nwhole;
	if (m.of)
		status("%s: fetching %d contact%s...", b->name, m.of, m.of == 1 ? "" : "s");
	if (l.npart && !carddav_multiget(b->url, &au, l.part, l.npart, 1, mget_cb, &m, ab_err, sizeof(ab_err)))
		w.failed = 2;
	skipped = l.npart ? carddav_skipped : 0;
	if (l.nwhole && !w.failed &&
	    !carddav_multiget(b->url, &au, l.whole, l.nwhole, 0, mget_cb, &m, ab_err, sizeof(ab_err)))
		w.failed = 2;
	skipped += l.nwhole ? carddav_skipped : 0;
	lcmp_free(&l);
	if (w.failed == 2) {
		cw_abandon(&w);
		r = -1;
		goto out;
	}
	if (!cw_close(&w)) {
		r = -1;
		goto out;
	}
	/* a card too big to fetch: the ctag not taken, so it is tried again */
	str_copy(b->ctag, skipped ? "" : ctag, sizeof(b->ctag));
	b->synced = now_local();
	status("%s: reading...", b->name);
	book_load(b);
	r = 1;
out:
	if (r < 0)
		str_copy(a->err, ab_err, sizeof(a->err));
	return r;
}

static int aw_open_anew(AW *w, BOOK *b)
{
	if (aw_open(w, b))
		return 1;
	/* written anew first (superseded copies, or the old kind) */
	if (cache_rewrite(b, -1, 0, 0, 0, 0, 0) && aw_open(w, b))
		return 1;
	if (!ab_err[0])
		str_copy(ab_err, "can't write the address book's cache", sizeof(ab_err));
	return 0;
}

int book_fetch_job(int bi)
{
	book_fetch_job_end();
	if (bi < 0 || bi >= nbooks || !aw_open_anew(&fj, &books[bi]))
		return 0;
	fj_bi = bi;
	return 1;
}

int book_fetch_job_end(void)
{
	BOOK *b;
	if (fj_bi < 0)
		return 1;
	b = &books[fj_bi];
	fj_bi = -1;
	if (!aw_close(&fj)) {
		load_one(b);
		cache_rewrite(b, -1, 0, 0, 0, 0, 0);
		people_changed(1);
		str_copy(ab_err, "couldn't write the address book's cache (disk full?)", sizeof(ab_err));
		return 0;
	}
	return 1;
}

int book_fetch_whole(BOOK *b, char **hrefs, int n)
{
	AW w, *wp = &w;
	MG m;
	DAVAUTH au;
	int ok;
	ACCOUNT *a = &accts[b->acct];
	ab_err[0] = 0;
	if (a->kind != AK_CARDDAV)
		return 0;
	if (opt.offline) {
		str_copy(ab_err, "EContact is working offline", sizeof(ab_err));
		return -1;
	}
	if (fj_bi == (int)(b - books)) {
		wp = &fj;		/* in the job: written at its end */
	} else if (!aw_open(&w, b)) {
		/* written anew first (superseded copies, or the old kind) */
		if (!cache_rewrite(b, -1, 0, 0, 0, 0, 0) || !aw_open(&w, b)) {
			if (!ab_err[0])
				str_copy(ab_err, "can't write the address book's cache", sizeof(ab_err));
			return -1;
		}
	}
	au = auth_of(b->acct);
	memset(&m, 0, sizeof(m));
	m.aw = wp;
	m.b = b;
	ok = carddav_multiget(b->url, &au, hrefs, n, 0, mget_cb, &m, ab_err, sizeof(ab_err));
	if (wp == &fj) {
		people_changed(0);
		return ok || m.n ? m.n : -1;
	}
	if (!aw_close(&w)) {
		load_one(b);
		cache_rewrite(b, -1, 0, 0, 0, 0, 0);
		people_changed(1);
		str_copy(ab_err, "couldn't write the address book's cache (disk full?)", sizeof(ab_err));
		return -1;
	}
	people_changed(0);		/* the same people, the same emails */
	return ok || m.n ? m.n : -1;
}

CONTACT *contact_by_href(int bi, const char *href)
{
	int r = live_res(&books[bi], href), i;
	if (r < 0)
		return 0;
	for (i = 0; i < npeople; i++)
		if (people[i]->book == bi && people[i]->res == r)
			return people[i];
	return 0;
}

CONTACT *contact_complete(const CONTACT *c)
{
	char href[400];
	int bi;
	RESOURCE *r = res_of(c);
	if (!r) {
		str_copy(ab_err, "that contact is gone", sizeof(ab_err));
		return 0;
	}
	bi = c->book;
	if (!r->partial)
		return (CONTACT *)c;
	str_copy(href, res_href(&books[c->book], r), sizeof(href));
	{
		char *one = href;
		if (book_fetch_whole(&books[bi], &one, 1) <= 0) {
			if (!ab_err[0])
				str_copy(ab_err, "the server didn't send the contact", sizeof(ab_err));
			return 0;
		}
	}
	return contact_by_href(bi, href);
}

int sync_account(int a, int force)
{
	int i, changed = 0, failed = 0;
	char err[200] = "";
	if (accts[a].kind == AK_LOCAL)
		return 0;
	accts[a].err[0] = 0;
	{
		int have = 0;
		for (i = 0; i < nbooks; i++)
			have += books[i].acct == a;
		/* the first time, and on a forced refresh, the list of books too */
		if ((!have || force) && !acct_discover(a))
			return -1;
	}
	for (i = 0; i < nbooks; i++) {
		int r;
		if (books[i].acct != a)
			continue;
		r = sync_book(&books[i], force);
		if (r > 0)
			changed++;
		if (r < 0) {
			failed++;
			if (!err[0])
				str_copy(err, ab_err, sizeof(err));
		}
	}
	if (failed) {
		str_copy(ab_err, err, sizeof(ab_err));
		str_copy(accts[a].err, err, sizeof(accts[a].err));
	}
	store_save_settings();
	status_clear();
	return failed && !changed ? -1 : changed;
}

int sync_all(int force)
{
	int a, changed = 0, failed = 0;
	char err[200] = "";
	for (a = 0; a < naccts; a++) {
		int r = sync_account(a, force);
		if (r > 0)
			changed += r;
		if (r < 0) {
			failed++;
			if (!err[0])
				snprintf(err, sizeof(err), "%s: %s", accts[a].name, ab_err);
		}
	}
	if (failed)
		str_copy(ab_err, err, sizeof(ab_err));
	http_close_all();
	conn_log_flush();
	return failed ? -1 : changed;
}

/* ---- bringing the books up to date in the background ----
 * The accessory, its window closed (ui/main.c, common/ui/bgwork.c): one
 * exchange with a server a step, its answer kept in memory (a few cards
 * at a time, within carddav_budget), and the files opened only while it
 * is written, with no event call between: an accessory's files belong
 * to the program in front, which may end while the network is waited
 * for. Only what changed is added to the end of the cache and read; a
 * book whose cache must be written anew, or the list of books, wait for
 * a sync with the window open. */
enum { QS_IDLE, QS_BOOK, QS_LIST, QS_FETCH, QS_PARSE, QS_WRITE, QS_END };
static int qs_last;

static struct {
	int st, bi, changed, failed, dropped, skipped;
	char ctag[100];
	LCMP l;
	int pi, wi;			/* the next of l.part, l.whole to fetch */
	MG m;				/* the cards that came, gathered until written */
	int qi, n, partial;		/* the next to parse or write; how many asked */
	long bytes;			/* the cards gathered so far */
	AW aw;				/* writing them: in memory until aw_close */
	int writing;
} qs;

static void qs_cards_free(void)
{
	int i;
	if (qs.writing)
		out_done(&qs.aw.d), out_done(&qs.aw.p), out_done(&qs.aw.i);	/* dropped */
	qs.writing = 0;
	qs.bytes = 0;
	for (i = 0; i < qs.m.nq; i++) {
		free(qs.m.q[i].c);
		free(qs.m.q[i].href);
	}
	free(qs.m.q);
	memset(&qs.m, 0, sizeof(qs.m));
}

static void qs_book_done(void)
{
	qs_cards_free();
	lcmp_free(&qs.l);
	memset(&qs.l, 0, sizeof(qs.l));
	qs.bi++;
	qs.st = QS_BOOK;
}

int quiet_sync_begin(void)
{
	quiet_sync_cancel();
	if (opt.offline || !naccts)
		return 0;
	qs.st = QS_BOOK;
	return 1;
}

void quiet_sync_cancel(void)
{
	if (qs.st != QS_IDLE) {
		lcmp_free(&qs.l);
		qs_cards_free();
	}
	memset(&qs, 0, sizeof(qs));
}

int quiet_syncing(void)
{
	return qs.st != QS_IDLE;
}

/* the cards that came, into the cache's end; first the ones the server
   no longer has */
static int qs_gone(BOOK *b, int i)
{
	const char *h;
	int j;
	if (b->res[i].dead || qs.l.keep[i])
		return 0;
	h = res_href(b, &b->res[i]);
	for (j = 0; j < qs.l.npart; j++)
		if (!strcmp(qs.l.part[j], h))
			return 0;
	for (j = 0; j < qs.l.nwhole; j++)
		if (!strcmp(qs.l.whole[j], h))
			return 0;
	return 1;
}

/* the cards gathered, into the cache's end: a few a step into memory
   (aw), then the files written in one go (6.5); first the ones the
   server no longer has. 1 ok, with *done when all is written */
static int qs_write(BOOK *b, int *done)
{
	int i, k, gone = 0;
	*done = 0;
	if (!qs.writing) {
		if (!qs.dropped)
			for (i = 0; i < b->nres && !gone; i++)
				gone = qs_gone(b, i);
		if (!gone && qs.qi >= qs.m.nq) {
			qs.dropped = 1;
			*done = 1;
			return 1;	/* nothing to write */
		}
		if (!aw_open(&qs.aw, b))
			return 0;
		qs.writing = 1;
		if (!qs.dropped) {
			qs.dropped = 1;
			for (i = 0; i < b->nres; i++)
				if (qs_gone(b, i))
					aw_drop(&qs.aw, i);
			return 1;
		}
	}
	for (k = 0; k < 4 && qs.qi < qs.m.nq; k++, qs.qi++) {
		QCARD *q = &qs.m.q[qs.qi];
		aw_add_pre(&qs.aw, q->href, q->etag, q->data, q->len, q->partial, q->c);
		q->c = 0;		/* taken */
	}
	if (qs.qi < qs.m.nq)
		return 1;
	qs.writing = 0;
	*done = 1;
	if (!aw_close(&qs.aw)) {
		load_one(b);		/* as the files are; written anew with the window open */
		return 0;
	}
	return 1;
}

/* one step: 1 while there is more */
int quiet_sync_step(void)
{
	BOOK *b = qs.bi < nbooks ? &books[qs.bi] : 0;
	DAVAUTH au;
	switch (qs.st) {
	case QS_BOOK:
		/* the next book, if its ctag moved */
		if (!b) {
			qs.st = QS_END;
			qs.dropped = qs.skipped = 0;	/* (QS_END's steps) */
			return 1;
		}
		if (accts[b->acct].kind != AK_CARDDAV || !b->ver) {
			qs.bi++;
			return 1;
		}
		au = auth_of(b->acct);
		ab_err[0] = 0;
		if (!carddav_ctag(b->url, &au, qs.ctag, sizeof(qs.ctag), ab_err, sizeof(ab_err))) {
			qs.failed++;
			qs.bi++;
			return 1;
		}
		if (qs.ctag[0] && !strcmp(qs.ctag, b->ctag) && b->synced) {
			qs.bi++;
			return 1;
		}
		/* changes are added to the end; when the file is to be written
		   anew, that waits for the window */
		if (b->ver != CACHE_VER || (b->ndead >= 32 && 2 * b->ndead > b->nres)) {
			qs.bi++;
			return 1;
		}
		qs.st = QS_LIST;
		return 1;
	case QS_LIST:
		au = auth_of(b->acct);
		memset(&qs.l, 0, sizeof(qs.l));
		qs.l.b = b;
		qs.l.keep = calloc(b->nres + 1, 1);
		if (!qs.l.keep || !carddav_list(b->url, &au, list_cmp, &qs.l, ab_err, sizeof(ab_err))) {
			qs.failed++;
			qs_book_done();
			return 1;
		}
		qs.pi = qs.wi = 0;
		qs.dropped = 0;
		qs.skipped = 0;
		if (!qs.l.npart && !qs.l.nwhole) {
			/* nothing new: only the ones gone */
			qs.n = 0;
			qs.qi = 0;
			qs.st = QS_WRITE;
			return 1;
		}
		qs.st = QS_FETCH;
		return 1;
	case QS_FETCH:
		/* a few at a time, the ones we have in part first: the
		   answer is kept in memory, no file open while it comes;
		   the cards gather until the job's memory is full */
		if (qs.pi < qs.l.npart) {
			qs.n = qs.l.npart - qs.pi < 8 ? qs.l.npart - qs.pi : 8;
			qs.partial = 1;
		} else {
			qs.n = qs.l.nwhole - qs.wi < 4 ? qs.l.nwhole - qs.wi : 4;
			qs.partial = 0;
		}
		qs.m.b = b;
		au = auth_of(b->acct);
		if (qs.n > 0) {
			if (!carddav_multiget(b->url, &au, qs.partial ? qs.l.part + qs.pi : qs.l.whole + qs.wi, qs.n,
					      qs.partial, mget_cb, &qs.m, ab_err, sizeof(ab_err))) {
				qs.failed++;
				qs_book_done();
				return 1;
			}
			qs.skipped += carddav_skipped;
		}
		qs.st = QS_PARSE;
		return 1;
	case QS_PARSE:
		/* one card a step, as at start-up */
		if (qs.qi < qs.m.nq) {
			QCARD *q = &qs.m.q[qs.qi++];
			q->c = vcard_parse(q->data, q->len);
			qs.bytes += q->len + 64;
			return 1;
		}
		if (qs.partial)
			qs.pi += qs.n;
		else
			qs.wi += qs.n;
		qs.n = 0;
		/* more to fetch, and room for them: fetched first */
		if ((qs.pi < qs.l.npart || qs.wi < qs.l.nwhole) && qs.bytes < ab_job_bytes) {
			qs.st = QS_FETCH;
			return 1;
		}
		qs.qi = 0;
		qs.st = QS_WRITE;
		return 1;
	case QS_WRITE: {
		int done;
		if (!qs_write(b, &done)) {
			qs.failed++;
			qs_book_done();
			return 1;
		}
		if (!done)
			return 1;
		qs_cards_free();
		qs.qi = 0;
		qs.st = QS_FETCH;
		if (qs.pi < qs.l.npart || qs.wi < qs.l.nwhole)
			return 1;
		}
		/* a card too big to fetch: the ctag not taken, so it is tried again */
		str_copy(b->ctag, qs.skipped ? "" : qs.ctag, sizeof(b->ctag));
		b->synced = now_local();
		qs.changed++;
		qs_book_done();
		return 1;
	case QS_END:
		/* in two steps: the connection, the log. ECONTACT.INF (the
		   new ctags) is written when the window opens (store_unsaved):
		   writing it takes an ST over 100 ms, and a ctag not kept only
		   costs the next refresh a look at the list */
		if (!qs.dropped) {
			qs.dropped = 1;
			http_close_all();
			return 1;
		}
		if (qs.changed)
			store_unsaved = 1;
		conn_log_flush();
		{
			int changed = qs.changed;
			quiet_sync_cancel();
			qs_last = changed;
			if (changed) {
				/* sorted again, the names stamp again: in steps too */
				books_load_cancel();
				ld.all = 1;
				ld.phase = LB_SORT;
			}
		}
		return 0;
	}
	return 0;
}

/* stopped where it is (the window opens): what came is kept, and sorted
   at once */
void quiet_sync_stop(void)
{
	int changed;
	if (qs.st == QS_IDLE)
		return;
	changed = qs.changed || qs.dropped || qs.pi || qs.wi;
	if (qs.writing) {
		/* cards half added: the rest added and the files written, so
		   the list and the files agree */
		BOOK *b = &books[qs.bi];
		int done = 0;
		while (qs.writing && qs_write(b, &done) && !done)
			;
		qs.writing = 0;		/* (closed, or failed and read again) */
	}
	quiet_sync_cancel();
	qs_last = changed;
	http_close_all();
	if (changed) {
		store_save_settings();
		people_changed(1);
	}
}

int store_unsaved;

/* the step next, for AES_PROFILE's notes */
const char *quiet_sync_what(void)
{
	static const char *const n[] = { "sync", "sync-ctag", "sync-list", "sync-fetch", "sync-parse", "sync-write",
					  "sync-end" };
	if (qs.st == QS_END)
		return !qs.dropped ? "sync-end-close" : "sync-end-log";
	return n[qs.st];
}

/* the books the last one changed */
int quiet_sync_changed(void)
{
	return qs_last;
}

/* ---------------- changing contacts ---------------- */

/* The whole card, to change: one fetched in part is fetched whole first
   (not kept: the change is stored over it), so nothing the server has is
   lost. etag: the one to change it under. NULL with ab_err */
static char *contact_text(const CONTACT *c, long *len, char *etag, int esize)
{
	RESOURCE *r = res_of(c);
	BOOK *b;
	char *raw;
	if (!r) {
		str_copy(ab_err, "can't read the contact from the cache", sizeof(ab_err));
		return 0;
	}
	b = &books[c->book];
	str_copy(etag, r->etag, esize);
	if (r->partial && accts[b->acct].kind == AK_CARDDAV) {
		DAVAUTH au = auth_of(b->acct);
		SBUF fresh;
		char e2[100];
		if (opt.offline) {
			str_copy(ab_err, "EContact is working offline: switch it off in the menu to save changes", sizeof(ab_err));
			return 0;
		}
		status("Fetching the whole card...");
		sb_init(&fresh);
		if (!carddav_get(b->url, res_href(b, r), &au, &fresh, e2, sizeof(e2), ab_err, sizeof(ab_err)) || !fresh.len) {
			sb_free(&fresh);
			if (!ab_err[0])
				str_copy(ab_err, "the server didn't send the contact", sizeof(ab_err));
			return 0;
		}
		if (e2[0])
			str_copy(etag, e2, esize);
		*len = fresh.len;
		return fresh.s;
	}
	raw = contact_raw(c, len);
	if (!raw)
		str_copy(ab_err, "can't read the contact from the cache", sizeof(ab_err));
	return raw;
}

/* a vCard to the server (not for On My Atari) and into the cache. res:
   the resource it replaces, -1 for a new one at href; if_match: its etag
   (NULL: the cache's) */
static int store_card(BOOK *b, int res, const char *href, const char *if_match, const char *text, long len)
{
	ACCOUNT *a = &accts[b->acct];
	char etag[100] = "";
	SBUF fresh;
	int ok;
	sb_init(&fresh);
	if (!book_writable(b)) {
		str_copy(ab_err, "this address book can't be changed", sizeof(ab_err));
		return 0;
	}
	if (a->kind == AK_CARDDAV) {
		DAVAUTH au = auth_of(b->acct);
		const char *old_etag = if_match ? if_match : res >= 0 ? b->res[res].etag : 0;
		if (opt.offline) {
			str_copy(ab_err, "EContact is working offline: switch it off in the menu to save changes", sizeof(ab_err));
			return 0;
		}
		status("Saving to %s...", b->name);
		if (!carddav_put(b->url, href, &au, text, len, old_etag, etag, sizeof(etag), ab_err, sizeof(ab_err))) {
			status_clear();
			return 0;
		}
		/* without an etag the server may have changed the card on
		   saving: fetch it as it is now, for the next change to match */
		if (!etag[0]) {
			char e2[100];
			if (carddav_get(b->url, href, &au, &fresh, e2, sizeof(e2), ab_err, sizeof(ab_err)) && fresh.len) {
				text = fresh.s;
				len = fresh.len;
				str_copy(etag, e2, sizeof(etag));
			}
		}
		status_clear();
	}
	ok = cache_change(b, res, href, etag, text, len, 0);
	sb_free(&fresh);
	return ok;
}

static void new_href(const char *uid, char *out, int size)
{
	/* the UID, kept to letters and digits, as the Mac's Contacts does */
	int n = 0;
	for (; *uid && n < size - 5; uid++)
		if (isalnum((unsigned char)*uid) || *uid == '-')
			out[n++] = *uid;
	if (!n)
		out[n++] = 'x';
	strcpy(out + n, ".vcf");
}

CONTACT *contact_save(const CEDIT *e, CONTACT *old, int bi)
{
	SBUF out;
	char *raw = 0, uid[64], href[80], etag[100] = "";
	long len = 0;
	BOOK *b;
	int res = -1, ok;
	if (old) {
		bi = old->book;
		raw = contact_text(old, &len, etag, sizeof(etag));
		if (!raw)
			return 0;
		str_copy(uid, old->uid ? old->uid : "", sizeof(uid));
		res = old->res;
	} else {
		vcard_new_uid(uid, sizeof(uid));
	}
	if (bi < 0 || bi >= nbooks) {
		free(raw);
		str_copy(ab_err, "there's no address book to keep it in", sizeof(ab_err));
		return 0;
	}
	b = &books[bi];
	sb_init(&out);
	if (!vcard_write(e, uid, raw, len, &out)) {
		free(raw);
		sb_free(&out);
		str_copy(ab_err, vcard_nomem ? "not enough memory for the card: nothing was changed" :
			 "a contact needs a name, a company, an email or a phone", sizeof(ab_err));
		return 0;
	}
	free(raw);
	{
		/* the href is copied: storing frees the old resource */
		char *h = res >= 0 ? strdup(res_href(b, &b->res[res])) : 0;
		if (res < 0)
			new_href(uid, href, sizeof(href));
		else if (h && strlen(h) < sizeof(href))
			str_copy(href, h, sizeof(href));
		ok = (res < 0 || h) && store_card(b, res, h ? h : href, etag[0] ? etag : 0, out.s, out.len);
		if (h && strlen(h) >= sizeof(href))
			href[0] = 0;	/* too long to find it by: by UID only */
		free(h);
	}
	sb_free(&out);
	if (!ok)
		return 0;
	if (!uid[0] && href[0]) {
		/* an old card without a UID got one: find it by href */
		int i;
		for (i = 0; i < npeople; i++)
			if (people[i]->book == bi && people[i]->res >= 0 && !strcmp(res_href(b, &b->res[people[i]->res]), href))
				return people[i];
		return 0;
	}
	return contact_by_uid(uid, bi);
}

int contact_delete(CONTACT *c)
{
	BOOK *b;
	ACCOUNT *a;
	int res, i;
	char uid[80];
	if (!c || c->book < 0 || c->book >= nbooks)
		return 0;
	b = &books[c->book];
	a = &accts[b->acct];
	res = c->res;
	if (!book_writable(b)) {
		str_copy(ab_err, "this address book can't be changed", sizeof(ab_err));
		return 0;
	}
	str_copy(uid, c->uid ? c->uid : "", sizeof(uid));
	/* out of its groups first, while it is still there to compare with */
	if (uid[0] && !c->group) {
		int bi = c->book;
		for (i = 0; i < ngroups; i++) {
			/* each change reads the list again: the contact is found anew */
			CONTACT *cur = contact_by_uid(uid, bi);
			if (cur && groups[i].card && groups[i].book == bi && group_has(&groups[i], cur))
				group_remove(&groups[i], cur);
		}
		c = contact_by_uid(uid, bi);
		if (!c)
			return 0;
	}
	if (c)
		res = c->res;
	if (a->kind == AK_CARDDAV) {
		DAVAUTH au = auth_of(b->acct);
		if (opt.offline) {
			str_copy(ab_err, "EContact is working offline: switch it off in the menu to delete", sizeof(ab_err));
			return 0;
		}
		status("Deleting from %s...", b->name);
		if (!carddav_delete(b->url, res_href(b, &b->res[res]), &au, b->res[res].etag, ab_err, sizeof(ab_err))) {
			status_clear();
			return 0;
		}
		status_clear();
	}
	return cache_change(b, res, 0, 0, 0, 0, 0);
}

static int contains(const char *h, const char *n)
{
	return h && *h && str_istr(h, n) != 0;
}

int contact_matches(const CONTACT *c, const char *t)
{
	int i;
	char name[160];
	if (!*t)
		return 1;
	vcard_display_name(c, 0, name, sizeof(name));
	if (contains(name, t) || contains(c->fn, t) || contains(c->nick, t) || contains(c->org, t) ||
	    contains(c->dept, t) || contains(c->title, t) || contains(c->note, t) || contains(c->family, t))
		return 1;
	for (i = 0; i < c->nemail; i++)
		if (contains(c->email[i].value, t))
			return 1;
	for (i = 0; i < c->ntel; i++) {
		/* phone numbers match without their spaces and dashes */
		char a[48], b[48];
		int k, n = 0, m = 0;
		for (k = 0; c->tel[i].value[k] && n < 47; k++)
			if (isdigit((unsigned char)c->tel[i].value[k]) || c->tel[i].value[k] == '+')
				a[n++] = c->tel[i].value[k];
		a[n] = 0;
		for (k = 0; t[k] && m < 47; k++)
			if (isdigit((unsigned char)t[k]) || t[k] == '+')
				b[m++] = t[k];
		b[m] = 0;
		if (m >= 3 && strstr(a, b))
			return 1;
		if (contains(c->tel[i].value, t))
			return 1;
	}
	for (i = 0; i < c->nadr; i++)
		if (contains(c->adr[i].value, t))
			return 1;
	return 0;
}

/* ---------------- groups ---------------- */

static int cmp_groups(const void *a, const void *b)
{
	const GROUP *x = a, *y = b;
	if (x->book != y->book)
		return x->book - y->book;
	return strcasecmp(x->name, y->name);
}

static int add_group(const char *name, int book, CONTACT *card)
{
	int i;
	GROUP *n;
	char buf[64], *nm;
	str_copy(buf, name, sizeof(buf));
	nm = str_trim(buf);
	if (!nm[0])
		return -1;
	for (i = 0; i < ngroups; i++)
		if (groups[i].book == book && !strcasecmp(groups[i].name, nm)) {
			if (card && !groups[i].card)
				groups[i].card = card;
			return i;
		}
	n = realloc(groups, (ngroups + 1) * sizeof(GROUP));
	if (!n)
		return -1;
	groups = n;
	memset(&groups[ngroups], 0, sizeof(GROUP));
	str_copy(groups[ngroups].name, nm, sizeof(groups[ngroups].name));
	groups[ngroups].book = (short)book;
	groups[ngroups].card = card;
	return ngroups++;
}

void groups_collect(void)
{
	int i;
	free(groups);
	groups = 0;
	ngroups = 0;
	for (i = 0; i < npeople; i++)
		if (people[i]->group) {
			char n[64];
			vcard_display_name(people[i], 0, n, sizeof(n));
			add_group(n, people[i]->book, people[i]);
		}
	for (i = 0; i < npeople; i++) {
		const char *p = people[i]->cats;
		while (p && *p) {
			const char *c = strchr(p, ',');
			char n[64];
			int k = c ? (int)(c - p) : (int)strlen(p);
			if (k > 63)
				k = 63;
			memcpy(n, p, k);
			n[k] = 0;
			add_group(n, people[i]->book, 0);
			p = c ? c + 1 : 0;
		}
	}
	if (ngroups > 1)
		qsort(groups, ngroups, sizeof(GROUP), cmp_groups);
}

static int has_cat(const char *cats, const char *name)
{
	const char *p = cats;
	int n = (int)strlen(name);
	while (p && *p) {
		const char *c = strchr(p, ',');
		int k = c ? (int)(c - p) : (int)strlen(p);
		while (k && *p == ' ')
			p++, k--;
		while (k && p[k - 1] == ' ')
			k--;
		if (k == n && !strncasecmp(p, name, n))
			return 1;
		p = c ? c + 1 : 0;
	}
	return 0;
}

int group_has(const GROUP *g, const CONTACT *c)
{
	int i;
	if (c->group || c->book != g->book)
		return 0;
	if (g->card) {
		if (!c->uid)
			return 0;
		for (i = 0; i < g->card->nmember; i++)
			if (!strcasecmp(g->card->member[i], c->uid))
				return 1;
		return 0;
	}
	return has_cat(c->cats, g->name);
}

int group_count(const GROUP *g)
{
	int i, n = 0;
	for (i = 0; i < npeople; i++)
		n += group_has(g, people[i]);
	return n;
}

/* a group card written again with these members */
static int write_group(GROUP *g, const char *name, const char **mem, int n)
{
	SBUF out;
	char *raw, etag[100];
	long len = 0;
	BOOK *b = &books[g->book];
	int ok;
	char href[520];
	raw = contact_text(g->card, &len, etag, sizeof(etag));
	if (!raw)
		return 0;
	sb_init(&out);
	/* copied: res_href's buffer is used again while storing */
	str_copy(href, res_href(b, &b->res[g->card->res]), sizeof(href));
	ok = vcard_write_group(name, g->card->uid, mem, n, raw, len, &out);
	if (!ok && vcard_nomem)
		str_copy(ab_err, "not enough memory for the group: nothing was changed", sizeof(ab_err));
	ok = ok && store_card(b, g->card->res, href, etag, out.s, out.len);
	sb_free(&out);
	free(raw);
	return ok;
}

/* a contact's categories written again */
static int write_cats(CONTACT *c, const char *cats)
{
	SBUF out;
	char *raw, etag[100];
	long len = 0;
	BOOK *b = &books[c->book];
	int ok;
	char href[520];
	raw = contact_text(c, &len, etag, sizeof(etag));
	if (!raw)
		return 0;
	sb_init(&out);
	str_copy(href, res_href(b, &b->res[c->res]), sizeof(href));
	ok = vcard_set_categories(raw, len, cats, &out);
	if (!ok && vcard_nomem)
		str_copy(ab_err, "not enough memory for the card: nothing was changed", sizeof(ab_err));
	ok = ok && store_card(b, c->res, href, etag, out.s, out.len);
	sb_free(&out);
	free(raw);
	return ok;
}

/* group pointers move when the list is collected again: by name */
static GROUP *group_again(const char *name, int book)
{
	int i;
	for (i = 0; i < ngroups; i++)
		if (groups[i].book == book && !strcasecmp(groups[i].name, name))
			return &groups[i];
	return 0;
}

int group_add(GROUP *g, CONTACT *c)
{
	if (!c || c->group)
		return 0;
	if (c->book != g->book) {
		str_copy(ab_err, "a group can only hold contacts from its own account", sizeof(ab_err));
		return 0;
	}
	if (group_has(g, c))
		return 1;
	if (g->card) {
		const char **mem;
		int n = g->card->nmember, ok;
		char name[64];
		if (!c->uid) {
			str_copy(ab_err, "this contact has no UID to put in a group", sizeof(ab_err));
			return 0;
		}
		mem = malloc((n + 1) * sizeof(char *));
		if (!mem)
			return 0;
		memcpy(mem, g->card->member, n * sizeof(char *));
		mem[n] = c->uid;
		str_copy(name, g->name, sizeof(name));
		ok = write_group(g, name, mem, n + 1);
		free(mem);
		return ok;
	} else {
		char cats[400];
		snprintf(cats, sizeof(cats), "%s%s%s", c->cats ? c->cats : "", c->cats && *c->cats ? "," : "", g->name);
		return write_cats(c, cats);
	}
}

/* the categories without one */
static void cats_without(const char *cats, const char *name, char *out, int size)
{
	const char *p = cats;
	int o = 0;
	out[0] = 0;
	while (p && *p) {
		const char *c = strchr(p, ',');
		char buf[100], *one;
		int k = c ? (int)(c - p) : (int)strlen(p);
		if (k > 99)
			k = 99;
		memcpy(buf, p, k);
		buf[k] = 0;
		one = str_trim(buf);
		if (one[0] && strcasecmp(one, name))
			o += snprintf(out + o, size > o ? size - o : 0, "%s%s", o ? "," : "", one);
		p = c ? c + 1 : 0;
	}
}

int group_remove(GROUP *g, CONTACT *c)
{
	if (!c || !group_has(g, c))
		return 1;
	if (g->card) {
		const char **mem;
		int n = 0, i, ok;
		char name[64];
		mem = malloc((g->card->nmember + 1) * sizeof(char *));
		if (!mem)
			return 0;
		for (i = 0; i < g->card->nmember; i++)
			if (strcasecmp(g->card->member[i], c->uid))
				mem[n++] = g->card->member[i];
		str_copy(name, g->name, sizeof(name));
		ok = write_group(g, name, mem, n);
		free(mem);
		return ok;
	} else {
		char cats[400];
		cats_without(c->cats, g->name, cats, sizeof(cats));
		return write_cats(c, cats);
	}
}

int group_new(int bi, const char *name)
{
	SBUF out;
	char uid[64], href[80];
	int ok;
	GROUP *g;
	if (bi < 0 || bi >= nbooks)
		return -1;
	if ((g = group_again(name, bi)) != 0)
		return (int)(g - groups);
	vcard_new_uid(uid, sizeof(uid));
	new_href(uid, href, sizeof(href));
	sb_init(&out);
	ok = vcard_write_group(name, uid, 0, 0, 0, 0, &out) && store_card(&books[bi], -1, href, 0, out.s, out.len);
	sb_free(&out);
	if (!ok)
		return -1;
	g = group_again(name, bi);
	return g ? (int)(g - groups) : -1;
}

int group_rename(GROUP *g, const char *name)
{
	char old[64];
	int book = g->book, i;
	str_copy(old, g->name, sizeof(old));
	if (g->card)
		return write_group(g, name, g->card->member, g->card->nmember);
	/* a category: every member's card */
	for (i = 0; i < npeople; i++) {
		CONTACT *c = people[i];
		char cats[400];
		if (c->book != book || !has_cat(c->cats, old))
			continue;
		cats_without(c->cats, old, cats, sizeof(cats) - 66);
		snprintf(cats + strlen(cats), sizeof(cats) - strlen(cats), "%s%s", cats[0] ? "," : "", name);
		if (!write_cats(c, cats))
			return 0;
		i = -1;		/* the list was read again: from the top */
	}
	return 1;
}

int group_delete(GROUP *g)
{
	char name[64];
	int book = g->book, i;
	if (g->card)
		return contact_delete(g->card);
	str_copy(name, g->name, sizeof(name));
	for (i = 0; i < npeople; i++) {
		CONTACT *c = people[i];
		char cats[400];
		if (c->book != book || !has_cat(c->cats, name))
			continue;
		cats_without(c->cats, name, cats, sizeof(cats));
		if (!write_cats(c, cats))
			return 0;
		i = -1;
	}
	return 1;
}

/* ---------------- files ---------------- */

typedef struct {
	int book, n, failed;
} IMP;

int contacts_import(int bi, const char *path, int *count)
{
	char *text, *p, *end;
	long len;
	int n = 0;
	*count = 0;
	if (bi < 0 || bi >= nbooks) {
		str_copy(ab_err, "there's no address book to import into", sizeof(ab_err));
		return 0;
	}
	text = pf_load(path, &len);
	if (!text) {
		str_copy(ab_err, "can't read the file", sizeof(ab_err));
		return 0;
	}
	p = text;
	end = text + len;
	while (p < end) {
		char *b = (char *)str_istr(p, "BEGIN:VCARD"), *e;
		CONTACT *c;
		CEDIT ed;
		SBUF out;
		char href[80], uid[64];
		int ok;
		if (!b)
			break;
		e = (char *)str_istr(b, "END:VCARD");
		e = e ? e + 9 : end;
		p = e;
		c = vcard_parse(b, e - b);
		if (!c)
			continue;
		/* written anew in vCard 3.0 (iCloud refuses 2.1), with a UID */
		vcard_to_edit(c, &ed);
		str_copy(uid, c->uid ? c->uid : "", sizeof(uid));
		if (!uid[0] || contact_by_uid(uid, bi))
			vcard_new_uid(uid, sizeof(uid));
		free(c);
		sb_init(&out);
		{
			/* the old text without its UID line: the new one goes in */
			char save = *e;
			*e = 0;
			ok = vcard_write(&ed, uid, str_istr(b, "VERSION:2.1") ? 0 : b, e - b, &out);
			*e = save;
		}
		if (ok) {
			new_href(uid, href, sizeof(href));
			status("Importing %d...", n + 1);
			ok = store_card(&books[bi], -1, href, 0, out.s, out.len);
			if (!ok) {
				sb_free(&out);
				break;
			}
			n++;
		}
		sb_free(&out);
	}
	free(text);
	*count = n;
	status_clear();
	return 1;
}

int contacts_export(CONTACT **list, int n, const char *path)
{
	int h = pf_open(path, PF_WRITE), i, ok = 1;
	if (h < 0) {
		str_copy(ab_err, "can't write the file", sizeof(ab_err));
		return 0;
	}
	for (i = 0; i < n && ok; i++) {
		long len;
		char *raw, etag[100];
		/* whole: what was fetched in part is fetched again, when it can be */
		raw = contact_partial(list[i]) && !opt.offline ? contact_text(list[i], &len, etag, sizeof(etag)) : 0;
		if (!raw)
			raw = contact_raw(list[i], &len);
		if (!raw)
			continue;
		if (n > 1)
			status("Exporting %d of %d...", i + 1, n);
		ok = pf_write(h, raw, len) == len;
		if (ok && len && raw[len - 1] != '\n')
			ok = pf_write(h, "\r\n", 2) == 2;
		free(raw);
	}
	pf_close(h);
	status_clear();
	if (!ok)
		str_copy(ab_err, "couldn't write the file (disk full?)", sizeof(ab_err));
	return ok;
}

/* ---------------- the people, for EMail and ECal ---------------- */

/* Their names and addresses in one number, which changes when one of
   them does (and not for a phone number or a note): EMail and ECal are
   told then (PEOPLE's changed:, ui/answer.c). Shifts and adds: a 68000
   multiplies in software */
unsigned long people_names_stamp;

static unsigned long names_mix(unsigned long h, const char *s)
{
	if (s)
		while (*s)
			h = ((h << 5) | (h >> 27)) + (unsigned char)*s++ + 0x9E3779B9UL;
	return ((h << 5) | (h >> 27)) + 1;
}

static unsigned long names_of(unsigned long h, const CONTACT *c)
{
	int j;
	h = names_mix(h, c->given);
	h = names_mix(h, c->middle);
	h = names_mix(h, c->family);
	h = names_mix(h, c->fn);
	h = names_mix(h, c->org);
	h += c->company * 3 + c->group * 5;
	for (j = 0; j < c->nemail; j++)
		h = names_mix(h, c->email[j].value);
	return h;
}

static void names_stamp(void)
{
	unsigned long h = 0x2C1B3C6DUL + opt.last_first;
	int i;
	for (i = 0; i < npeople; i++)
		h = names_of(h, people[i]);
	people_names_stamp = h & 0xFFFFFFFFUL;
}

/* the name EMail or ECal shows for an address: 1 found */
int people_name_for(const char *addr, char *name, int size)
{
	int i, j;
	int a = tolower((unsigned char)*addr);
	name[0] = 0;
	if (!*addr)
		return 0;
	for (i = 0; i < npeople; i++) {
		const CONTACT *c = people[i];
		if (c->group)
			continue;
		for (j = 0; j < c->nemail; j++) {
			const char *v = c->email[j].value;
			if (tolower((unsigned char)*v) == a && !strcasecmp(v, addr)) {
				vcard_display_name(c, opt.last_first, name, size);
				return 1;
			}
		}
	}
	return 0;
}

/* the step next, for AES_PROFILE's notes */
const char *books_load_what(void)
{
	static const char *const n[] = { "idle", "load-start", "load-records", "load-trim", "load-index", "load-parse",
					  "load-end", "load-sort", "load-write", "load-groups", "load-names" };
	return n[ld.phase];
}
