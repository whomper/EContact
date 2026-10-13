/*
 * abook.c - the address books: settings, caches on disk, syncing with
 * CardDAV servers, saving changes, groups, and CONTACTS.TXT for EMail
 * and ECal. Built like ECal's calstore.c.
 *
 * Each address book has a cache file in CACHE\ next to the settings:
 *   BOOKnnnn.DAT  every contact's vCard as the server sent it, with its
 *                 href and etag
 * so the contacts open at once, offline too, and a contact can be
 * written back without losing what this program doesn't understand.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include "abook.h"
#include "carddav.h"
#include "photo.h"
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
}

static void set_opt(const char *k, const char *v)
{
#define S(name) if (!strcasecmp(k, #name)) { str_copy(opt.name, v, sizeof(opt.name)); return; }
#define I(name) if (!strcasecmp(k, #name)) { opt.name = (short)atoi(v); return; }
	S(gate) S(folder) S(me)
	I(falcon) I(dsp) I(log) I(font_id) I(font_pt) I(hebfont) I(win_x) I(win_y) I(win_w) I(win_h)
	I(sidebar) I(sort_first) I(last_first) I(refresh) I(sync_open) I(photos) I(offline) I(hebrew) I(heapkb) I(default_book)
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
	{
		char pd[220];
		path_join(pd, sizeof(pd), cache_dir, "PHOTOS");
		photo_dir(pd);
	}
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
		if (!strcasecmp(line, "[options]")) {
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
static void note(SBUF *b, const char *text)
{
	if (*text)
		sb_printf(b, "; %s\r\n", text);
}

static void kv(SBUF *b, const char *text, const char *key, const char *value)
{
	note(b, text);
	sb_printf(b, "%s=%s\r\n\r\n", key, value);
}

static void kn(SBUF *b, const char *text, const char *key, long value)
{
	char v[16];
	snprintf(v, sizeof(v), "%ld", value);
	kv(b, text, key, v);
}

void store_save_settings(void)
{
	SBUF b;
	int i, j;
	char enc[200];
	sb_init(&b);
	sb_adds(&b, "; EContact settings, written by ECONTACT.ACC / ECONTACT.PRG. Lines starting with ; are notes.\r\n"
		    "; Most of these are set in the menu > Settings; see docs/GUIDE.md.\r\n"
		    "; 1 means on, 0 means off. Keys may be in any case.\r\n\r\n[OPTIONS]\r\n");
#define S(key, name, text) note(&b, text); sb_printf(&b, key "=%s\r\n\r\n", opt.name);
#define I(key, name, text) note(&b, text); sb_printf(&b, key "=%d\r\n\r\n", opt.name);
	/* first: the accessory, EMail and ECal read it from the start of the file */
	sb_printf(&b, "; EContact's folder, with its address books, keys and certificates, and\r\n"
		    "; CONTACTS.TXT for EMail and ECal; ECONTACT.PRG writes where it was started from\r\nFOLDER=%s\r\n\r\n",
		  opt.workdir);
	I("HEAPKB", heapkb, "memory the accessory takes at boot, in KB; 0: 1.5 MB on a Falcon/TT, 320 KB on an ST")
	I("FALCON", falcon, "Falcon mode: TLS on this Atari (Falcon, TT); 0: through the Pi gateway")
	I("DSP", dsp, "check certificates on the Falcon's DSP56001")
	S("GATE", gate, "the Raspberry Pi gateway's address (Settings > Connection); ECal's gateway serves both")
	sb_printf(&b, "; the gateway's port\r\nGATEPORT=%u\r\n\r\n", opt.gateport);
	I("OFFLINE", offline, "work offline: no connections at all (the menu > Work offline)")
	I("LOG", log, "write the conversation with the servers to ECONTACT.LOG (passwords left out)")
	I("REFRESH", refresh, "minutes between refreshes of all address books; 0: only by hand")
	I("SYNC_OPEN", sync_open, "refresh when the window opens")
	I("PHOTOS", photos, "show contacts' photos (kept small in CACHE\\PHOTOS)")
	I("SORT_FIRST", sort_first, "sort by first name; 0: by last name")
	I("LAST_FIRST", last_first, "show names as Last, First; 0: First Last")
	I("DEFAULT_BOOK", default_book, "new contacts go into this address book (its ID below); -1: the first")
	S("ME", me, "the UID of your own card (right-click a contact > Make This My Card)")
	I("HEBFONT", hebfont, "where the font has the Hebrew letters: 0 Atari, 1 ISO-8859-8, 2 DOS 862")
	I("HEBREW", hebrew, "start with the Hebrew keyboard (F10 switches)")
	I("SIDEBAR", sidebar, "show the groups on the left")
	I("FONT_ID", font_id, "the font, as a GDOS id; 1: the system font")
	I("FONT_PT", font_pt, "the font's size in points; 0: the system font's")
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
	if (npeople > 1)
		qsort(people, npeople, sizeof(CONTACT *), cmp_people);
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

/* sorted again, groups and counts again, and EMail's and ECal's list */
static void people_changed(int export)
{
	people_sort();
	count_books();
	groups_collect();
	if (export)
		people_export();
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
	for (i = 0; i < b->nres; i++) {
		free(b->res[i].href);
		free(b->res[i].etag);
	}
	free(b->res);
	b->res = 0;
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

/* a buffered reader that knows where it is */
typedef struct {
	int h;
	char buf[16384];	/* big reads: each GEMDOS call costs, on a floppy most */
	int pos, len;
	long off;		/* file offset of buf[pos] */
} FR;

static int fr_fill(FR *f)
{
	if (f->pos < f->len)
		return 1;
	f->len = (int)pf_read(f->h, f->buf, sizeof(f->buf));
	f->pos = 0;
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

static int live_res(BOOK *b, const char *href)
{
	int i;
	for (i = 0; i < b->nres; i++)
		if (!b->res[i].dead && !strcmp(b->res[i].href, href))
			return i;
	return -1;
}

static void kill_res(BOOK *b, int r)
{
	b->res[r].dead = 1;
	b->ndead++;
	people_drop((int)(b - books), r);
}

/* a contact read from the cache into the list */
static void add_contact(BOOK *b, int r, const char *raw, long len)
{
	CONTACT *c = vcard_parse(raw, len);
	if (!c)
		return;
	c->book = (short)(b - books);
	c->res = (short)r;
	if (opt.sort_first)
		vcard_sort_key(c, 1);
	if (!people_add(c))
		free(c);
}

static int res_add(BOOK *b, const char *etag, const char *href, long off, long len)
{
	RESOURCE *r;
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
	r->etag = strdup(etag ? etag : "");
	r->href = strdup(href);
	r->off = off;
	r->len = len;
	r->dead = 0;
	return b->nres++;
}

static void load_one(BOOK *b)
{
	char path[200], line[700];
	static FR f;		/* 16 KB: not on the accessory's stack */
	people_drop((int)(b - books), -1);
	book_free(b);
	cache_path(b, "DAT", path, sizeof(path));
	memset(&f, 0, sizeof(f));
	f.h = pf_open(path, PF_READ);
	if (f.h < 0)
		return;
	if (!fr_line(&f, line, sizeof(line)) || strncmp(line, "CARDCACHE 1", 11)) {
		pf_close(f.h);
		return;
	}
	while (fr_line(&f, line, sizeof(line))) {
		char *tab1, *tab2, *raw;
		long len;
		int r;
		if (line[0] == 'X' && line[1] == ' ') {
			/* a contact deleted since the file was written */
			int old = live_res(b, line + 2);
			if (old >= 0)
				kill_res(b, old);
			continue;
		}
		if (line[0] != 'R' || line[1] != ' ')
			break;
		tab1 = strchr(line + 2, '\t');
		tab2 = tab1 ? strchr(tab1 + 1, '\t') : 0;
		if (!tab2)
			break;
		*tab1 = *tab2 = 0;
		len = atol(tab2 + 1);
		{
			/* a newer copy of a contact, appended when it was changed */
			int old = live_res(b, tab1 + 1);
			if (old >= 0)
				kill_res(b, old);
		}
		r = res_add(b, line + 2, tab1 + 1, f.off, len);
		if (r < 0)
			break;
		raw = len < 262144L ? malloc(len + 1) : 0;
		if (raw) {
			if (!fr_bytes(&f, raw, len)) {
				free(raw);
				break;
			}
			raw[len] = 0;
			add_contact(b, r, raw, len);
			free(raw);
		} else if (!fr_bytes(&f, 0, len)) {
			break;
		}
		fr_bytes(&f, 0, 1);		/* the newline after it */
	}
	pf_close(f.h);
}

void book_load(BOOK *b)
{
	load_one(b);
	people_changed(1);
}

void book_load_all(void)
{
	int i;
	for (i = 0; i < nbooks; i++) {
		status("Reading %s...", books[i].name);
		load_one(&books[i]);
	}
	people_changed(1);
	status_clear();
}

/* writing a new cache file: header, then resources one by one */
typedef struct {
	BOOK *b;
	int h, old;		/* new file, old one (for copying) */
	char tmp[200];
	int failed;
} CW;

static int cw_open(CW *w, BOOK *b)
{
	char path[200];
	memset(w, 0, sizeof(*w));
	w->b = b;
	cache_path(b, "TMP", w->tmp, sizeof(w->tmp));
	w->h = pf_open(w->tmp, PF_WRITE);
	if (w->h < 0) {
		snprintf(ab_err, sizeof(ab_err), "can't write the address book's cache (disk full or locked?)");
		return 0;
	}
	cache_path(b, "DAT", path, sizeof(path));
	w->old = pf_open(path, PF_READ);
	pf_write(w->h, "CARDCACHE 1\n", 12);
	return 1;
}

static void cw_add(CW *w, const char *href, const char *etag, const char *raw, long len)
{
	char head[700];
	int n = snprintf(head, sizeof(head), "R %s\t%s\t%ld\n", etag ? etag : "", href, len);
	if (pf_write(w->h, head, n) != n || pf_write(w->h, raw, len) != len || pf_write(w->h, "\n", 1) != 1)
		w->failed = 1;
}

static void cw_copy(CW *w, const RESOURCE *r)
{
	char *raw;
	if (w->old < 0 || !(raw = malloc(r->len + 1)))
		return;
	if (pf_seek(w->old, r->off) == r->off && pf_read(w->old, raw, r->len) == r->len)
		cw_add(w, r->href, r->etag, raw, r->len);
	else
		w->failed = 1;
	free(raw);
}

static void cw_abandon(CW *w)
{
	if (w->old >= 0)
		pf_close(w->old);
	pf_close(w->h);
	pf_remove(w->tmp);
}

static int cw_close(CW *w)
{
	char path[200];
	if (w->old >= 0)
		pf_close(w->old);
	pf_close(w->h);
	if (w->failed) {
		pf_remove(w->tmp);
		snprintf(ab_err, sizeof(ab_err), "couldn't write the address book's cache (disk full?)");
		return 0;
	}
	cache_path(w->b, "DAT", path, sizeof(path));
	pf_remove(path);
	pf_rename(w->tmp, path);
	return 1;
}

/* the whole cache written again, without the superseded copies */
static int cache_rewrite(BOOK *b, int drop, const char *href, const char *etag, const char *raw, long len)
{
	CW w;
	int i;
	if (!cw_open(&w, b))
		return 0;
	for (i = 0; i < b->nres; i++)
		if (i != drop && !b->res[i].dead && !(href && !strcmp(b->res[i].href, href)))
			cw_copy(&w, &b->res[i]);
	if (href)
		cw_add(&w, href, etag, raw, len);
	if (!cw_close(&w))
		return 0;
	book_load(b);
	return 1;
}

/* One contact changed (or gone): the new copy is added to the end of the
   cache file, and only that contact is read again. When superseded
   copies pile up the file is written anew. */
static int cache_change(BOOK *b, int drop, const char *href, const char *etag, const char *raw, long len)
{
	char path[200], head[700];
	long size;
	int h, n, ok = 1;
	cache_path(b, "DAT", path, sizeof(path));
	size = pf_size(path);
	if (size <= 0 || b->ndead >= 32)
		return cache_rewrite(b, drop, href, etag, raw, len);
	h = pf_open(path, PF_APPEND);
	if (h < 0)
		return cache_rewrite(b, drop, href, etag, raw, len);
	/* the old copy goes */
	if (drop >= 0 && !b->res[drop].dead && (!href || strcmp(b->res[drop].href, href))) {
		n = snprintf(head, sizeof(head), "X %s\n", b->res[drop].href);
		ok = pf_write(h, head, n) == n;
		size += n;
		kill_res(b, drop);
	}
	if (ok && href) {
		int old = live_res(b, href), r;
		if (old >= 0)
			kill_res(b, old);
		n = snprintf(head, sizeof(head), "R %s\t%s\t%ld\n", etag ? etag : "", href, len);
		ok = pf_write(h, head, n) == n && pf_write(h, raw, len) == len && pf_write(h, "\n", 1) == 1;
		r = res_add(b, etag, href, size + n, len);
		if (r >= 0)
			add_contact(b, r, raw, len);
	}
	pf_close(h);
	if (!ok) {
		/* the disk refused the end of the file: write it all again */
		load_one(b);
		return cache_rewrite(b, -1, 0, 0, 0, 0);
	}
	people_changed(1);
	return 1;
}

char *contact_raw(const CONTACT *c, long *len)
{
	char path[200], *raw;
	int h;
	RESOURCE *r;
	BOOK *b;
	if (!c || c->book < 0 || c->book >= nbooks)
		return 0;
	b = &books[c->book];
	if (c->res < 0 || c->res >= b->nres)
		return 0;
	r = &b->res[c->res];
	cache_path(b, "DAT", path, sizeof(path));
	h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	raw = malloc(r->len + 1);
	if (raw && (pf_seek(h, r->off) != r->off || pf_read(h, raw, r->len) != r->len)) {
		free(raw);
		raw = 0;
	}
	pf_close(h);
	if (raw) {
		raw[r->len] = 0;
		if (len)
			*len = r->len;
	}
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

static int mget_cb(void *ud, const char *href, const char *etag, const char *data, long len)
{
	CW *w = ud;
	cw_add(w, href, etag, data, len);
	return !w->failed;
}

int sync_book(BOOK *b, int force)
{
	DAVAUTH au;
	DAVITEM *items;
	char ctag[100], **need;
	unsigned char *keep;
	int n, i, j, nneed = 0, nkept, r = 0;
	CW w;
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
	if (!force && ctag[0] && !strcmp(ctag, b->ctag) && b->synced)
		goto out;
	status("%s: what has changed...", b->name);
	if (!carddav_list(b->url, &au, &items, &n, ab_err, sizeof(ab_err))) {
		r = -1;
		goto out;
	}
	keep = calloc(b->nres + 1, 1);
	need = malloc((n + 1) * sizeof(char *));
	if (!keep || !need) {
		free(keep);
		free(need);
		carddav_free_items(items, n);
		str_copy(ab_err, "out of memory", sizeof(ab_err));
		r = -1;
		goto out;
	}
	for (i = 0; i < n; i++) {
		int found = -1;
		for (j = 0; j < b->nres; j++)
			if (!b->res[j].dead && !strcmp(b->res[j].href, items[i].href)) {
				found = j;
				break;
			}
		if (found >= 0 && same_etag(b->res[found].etag, items[i].etag))
			keep[found] = 1;
		else
			need[nneed++] = items[i].href;
	}
	if (!cw_open(&w, b)) {
		free(keep);
		free(need);
		carddav_free_items(items, n);
		r = -1;
		goto out;
	}
	for (j = 0, nkept = 0; j < b->nres; j++)
		if (keep[j]) {
			cw_copy(&w, &b->res[j]);
			nkept++;
		}
	if (!nneed && nkept == b->nres - b->ndead) {
		/* the ctag moved (our own changes, say) but nothing is new */
		cw_abandon(&w);
		free(keep);
		free(need);
		carddav_free_items(items, n);
		str_copy(b->ctag, ctag, sizeof(b->ctag));
		b->synced = now_local();
		goto out;
	}
	if (nneed) {
		status("%s: fetching %d contact%s...", b->name, nneed, nneed == 1 ? "" : "s");
		if (!carddav_multiget(b->url, &au, need, nneed, mget_cb, &w, ab_err, sizeof(ab_err)))
			w.failed = 2;
	}
	free(keep);
	free(need);
	carddav_free_items(items, n);
	if (w.failed == 2) {
		cw_abandon(&w);
		r = -1;
		goto out;
	}
	if (!cw_close(&w)) {
		r = -1;
		goto out;
	}
	str_copy(b->ctag, ctag, sizeof(b->ctag));
	b->synced = now_local();
	status("%s: reading...", b->name);
	book_load(b);
	r = 1;
out:
	if (r < 0)
		str_copy(a->err, ab_err, sizeof(a->err));
	return r;
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

/* ---------------- changing contacts ---------------- */

/* a vCard to the server (not for On My Atari) and into the cache. res:
   the resource it replaces, -1 for a new one at href */
static int store_card(BOOK *b, int res, const char *href, const char *text, long len)
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
		const char *old_etag = res >= 0 ? b->res[res].etag : 0;
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
	ok = cache_change(b, res, href, etag, text, len);
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
	char *raw = 0, uid[64], href[80];
	long len = 0;
	BOOK *b;
	int res = -1, ok;
	if (old) {
		bi = old->book;
		raw = contact_raw(old, &len);
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
		str_copy(ab_err, "a contact needs a name, a company, an email or a phone", sizeof(ab_err));
		return 0;
	}
	free(raw);
	{
		/* the href is copied: storing frees the old resource */
		char *h = res >= 0 ? strdup(b->res[res].href) : 0;
		if (res < 0)
			new_href(uid, href, sizeof(href));
		else if (h && strlen(h) < sizeof(href))
			str_copy(href, h, sizeof(href));
		ok = (res < 0 || h) && store_card(b, res, h ? h : href, out.s, out.len);
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
			if (people[i]->book == bi && people[i]->res >= 0 && !strcmp(b->res[people[i]->res].href, href))
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
		if (!carddav_delete(b->url, b->res[res].href, &au, b->res[res].etag, ab_err, sizeof(ab_err))) {
			status_clear();
			return 0;
		}
		status_clear();
	}
	return cache_change(b, res, 0, 0, 0, 0);
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
	char *raw;
	long len = 0;
	BOOK *b = &books[g->book];
	int ok;
	raw = contact_raw(g->card, &len);
	sb_init(&out);
	ok = vcard_write_group(name, g->card->uid, mem, n, raw, len, &out) &&
	     store_card(b, g->card->res, b->res[g->card->res].href, out.s, out.len);
	sb_free(&out);
	free(raw);
	return ok;
}

/* a contact's categories written again */
static int write_cats(CONTACT *c, const char *cats)
{
	SBUF out;
	char *raw;
	long len = 0;
	BOOK *b = &books[c->book];
	int ok;
	char href[400];
	raw = contact_raw(c, &len);
	if (!raw) {
		str_copy(ab_err, "can't read the contact from the cache", sizeof(ab_err));
		return 0;
	}
	sb_init(&out);
	str_copy(href, b->res[c->res].href, sizeof(href));
	ok = vcard_set_categories(raw, len, cats, &out) && store_card(b, c->res, href, out.s, out.len);
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
	ok = vcard_write_group(name, uid, 0, 0, 0, 0, &out) && store_card(&books[bi], -1, href, out.s, out.len);
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
			ok = store_card(&books[bi], -1, href, out.s, out.len);
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
		char *raw = contact_raw(list[i], &len);
		if (!raw)
			continue;
		ok = pf_write(h, raw, len) == len;
		if (ok && len && raw[len - 1] != '\n')
			ok = pf_write(h, "\r\n", 2) == 2;
		free(raw);
	}
	pf_close(h);
	if (!ok)
		str_copy(ab_err, "couldn't write the file (disk full?)", sizeof(ab_err));
	return ok;
}

/* ---------------- CONTACTS.TXT ---------------- */

void people_export(void)
{
	SBUF b;
	char path[200], name[160];
	int i, j, k;
	if (!opt.workdir[0])
		return;
	sb_init(&b);
	sb_adds(&b, "; EContact's people, for EMail and ECal. Written by EContact whenever the\r\n"
		    "; contacts change: don't edit it here, change the contacts in EContact.\r\n"
		    "; P<tab>name<tab>email<tab>label    one line for each email address\r\n"
		    "; G<tab>group<tab>Name <email>, Name <email>...\r\n");
	for (i = 0; i < npeople; i++) {
		CONTACT *c = people[i];
		if (c->group || !c->nemail)
			continue;
		vcard_display_name(c, 0, name, sizeof(name));
		for (j = 0; j < c->nemail; j++)
			sb_printf(&b, "P\t%s\t%s\t%s\r\n", name, c->email[j].value, c->email[j].label);
	}
	for (k = 0; k < ngroups; k++) {
		int first = 1;
		for (i = 0; i < npeople; i++) {
			CONTACT *c = people[i];
			if (!c->nemail || !group_has(&groups[k], c))
				continue;
			if (first)
				sb_printf(&b, "G\t%s\t", groups[k].name);
			vcard_display_name(c, 0, name, sizeof(name));
			sb_printf(&b, "%s%s <%s>", first ? "" : ", ", name, c->email[0].value);
			first = 0;
		}
		if (!first)
			sb_adds(&b, "\r\n");
	}
	path_join(path, sizeof(path), opt.workdir, "CONTACTS.TXT");
	pf_save(path, b.s ? b.s : "", b.len);
	sb_free(&b);
}
