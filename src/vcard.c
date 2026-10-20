/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vcard.c - reading and writing vCards.
 *
 * Reading: the lines are unfolded, each split into group, name,
 * parameters and value, the value decoded (quoted-printable in 2.1,
 * any charset) into Atari text and unescaped. Apple keeps a custom label
 * in a second line of the same group ("item1.TEL:..." and
 * "item1.X-ABLabel:_$!<Mobile>!$_"), so all the lines are read first and
 * the labels matched to them after.
 *
 * Writing: the properties the editor knows are written from the CEDIT,
 * every other line of the old card is copied as it was.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "vcard.h"
#include "charset.h"
#include "plat.h"
#include "util.h"

int vcard_nomem;

const char *const vc_tel_labels[] = { "mobile", "iPhone", "home", "work", "main", "home fax", "work fax",
				      "pager", "other", 0 };
const char *const vc_email_labels[] = { "home", "work", "school", "iCloud", "other", 0 };
const char *const vc_adr_labels[] = { "home", "work", "school", "other", 0 };
const char *const vc_url_labels[] = { "homepage", "home", "work", "school", "other", 0 };

/* ---------------- lines ---------------- */

/* the properties the parser looks at, known once by their name: a
   68000 compares a name with each one in turn slowly (P_OTHER: the rest) */
enum { P_OTHER, P_BEGIN, P_END, P_PHOTO, P_LOGO, P_KEY, P_SOUND, P_N, P_FN, P_UID, P_NICKNAME, P_ORG,
       P_TITLE, P_NOTE, P_CATEGORIES, P_BDAY, P_EMAIL, P_TEL, P_ADR, P_URL, P_SHOWAS, P_KIND, P_MEMBER,
       P_MARK, P_ABLABEL };

typedef struct {
	char group[24];
	char name[40];		/* upper case */
	unsigned char prop;	/* P_..., from the name */
	char types[80];		/* TYPE values, lower case, comma-separated */
	char *value;		/* Atari text, still escaped (in VLINES.pool) */
	char *raw;		/* the whole unfolded line as it was, for copying (in pool; NULL if not kept) */
	long voff, roff;	/* their places in pool while it grows */
	int omit_year;		/* X-APPLE-OMIT-YEAR */
	int binary;		/* base64: a photo or a key */
} VLINE;

/* The lines and their text in two blocks, not two mallocs a line: on
   the Atari each malloc and free walks the list of free pieces, and a
   few thousand small ones left between the contacts make reading a big
   address book slower with every card */
typedef struct {
	VLINE *l;
	int n, max;
	SBUF pool;
} VLINES;

static void lines_free(VLINES *v)
{
	free(v->l);
	sb_free(&v->pool);
	v->l = 0;
	v->n = v->max = 0;
}

/* the pool has stopped growing: the lines can point into it */
static void lines_point(VLINES *v)
{
	int i;
	for (i = 0; i < v->n; i++) {
		v->l[i].value = v->pool.s + v->l[i].voff;
		v->l[i].raw = v->l[i].roff >= 0 ? v->pool.s + v->l[i].roff : 0;
	}
}

/* vcard_parse's, kept from card to card (unless a card made them big) */
static void lines_reset(VLINES *v)
{
	if (v->max > 64 || v->pool.cap > 8192)
		lines_free(v);
	v->n = 0;
	sb_reset(&v->pool);
}

/* one physical line: [p, *eol), next at *next */
static const char *phys_line(const char *p, const char *end, const char **eol)
{
	const char *e = p;
	while (e < end && *e != '\n' && *e != '\r')
		e++;
	*eol = e;
	if (e < end && *e == '\r')
		e++;
	if (e < end && *e == '\n')
		e++;
	return e;
}

static void lower_into(char *d, const char *s, long n, int size)
{
	int i = 0;
	while (n-- > 0 && i < size - 1) {
		char c = *s++;
		d[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
	}
	d[i] = 0;
}

/* one TYPE word added to the list; *k: the list's length so far */
static void add_type(VLINE *l, int *k, const char *s, long n)
{
	while (n > 0 && (*s == ' ' || *s == '"'))
		s++, n--;
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '"'))
		n--;
	if (n <= 0 || *k + n + 2 >= (long)sizeof(l->types))
		return;
	if (*k)
		l->types[(*k)++] = ',';
	lower_into(l->types + *k, s, n, (int)sizeof(l->types) - *k);
	*k += (int)n;
}

/* the parameter's name is w (lower case), as lower_into and strcmp
   found it: the first 23 letters, up to a NUL in them */
static int key_is(const char *s, long n, const char *w)
{
	long i;
	if (n > 23)
		n = 23;
	for (i = 0; w[i]; i++) {
		char c;
		if (i >= n)
			return 0;
		c = s[i];
		if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != w[i])
			return 0;
	}
	return i == n || !s[i];
}

/* the parameters "TYPE=home,cell;CHARSET=utf-8;ENCODING=QUOTED-PRINTABLE"
   (2.1: bare words "HOME;CELL") */
static void parse_params(VLINE *l, const char *p, const char *end, int *qp, int *cs)
{
	int k = 0;		/* l->types' length */
	while (p < end) {
		const char *s = p, *eq = 0;
		int quoted = 0;
		while (p < end && (quoted || *p != ';')) {
			if (*p == '"')
				quoted = !quoted;
			if (*p == '=' && !eq && !quoted)
				eq = p;
			p++;
		}
		if (!eq) {
			add_type(l, &k, s, p - s);
		} else {
			const char *v = eq + 1;
			long kn = eq - s;
			if (key_is(s, kn, "type")) {
				/* a list: TYPE=home,cell */
				const char *q = v;
				while (q < p) {
					const char *c = q;
					while (c < p && *c != ',')
						c++;
					add_type(l, &k, q, c - q);
					q = c < p ? c + 1 : c;
				}
			} else if (key_is(s, kn, "encoding")) {
				if (p - v >= 1 && (v[0] == 'q' || v[0] == 'Q'))
					*qp = 1;
				else if (p - v >= 1 && (v[0] == 'b' || v[0] == 'B'))
					l->binary = 1;
			} else if (key_is(s, kn, "charset")) {
				char n[32];
				lower_into(n, v, p - v, sizeof(n));
				*cs = cs_id(n);
			} else if (key_is(s, kn, "x-apple-omit-year")) {
				l->omit_year = atoi(v);
			}
		}
		if (p < end)
			p++;
	}
}

static int prop_of(const char *n, int len)
{
	static const struct {
		const char *s;
		unsigned char len, prop;
	} t[] = {
		{ "N", 1, P_N }, { "FN", 2, P_FN }, { "UID", 3, P_UID }, { "TEL", 3, P_TEL }, { "ADR", 3, P_ADR },
		{ "URL", 3, P_URL }, { "ORG", 3, P_ORG }, { "KEY", 3, P_KEY }, { "END", 3, P_END },
		{ "BDAY", 4, P_BDAY }, { "NOTE", 4, P_NOTE }, { "LOGO", 4, P_LOGO }, { "KIND", 4, P_KIND },
		{ "EMAIL", 5, P_EMAIL }, { "TITLE", 5, P_TITLE }, { "PHOTO", 5, P_PHOTO }, { "SOUND", 5, P_SOUND },
		{ "BEGIN", 5, P_BEGIN }, { "MEMBER", 6, P_MEMBER }, { "NICKNAME", 8, P_NICKNAME },
		{ "X-ABLABEL", 9, P_ABLABEL }, { "CATEGORIES", 10, P_CATEGORIES }, { "X-ABSHOWAS", 10, P_SHOWAS },
		{ VC_PHOTO_MARK, sizeof(VC_PHOTO_MARK) - 1, P_MARK },
		{ "X-ADDRESSBOOKSERVER-KIND", 24, P_KIND }, { "X-ADDRESSBOOKSERVER-MEMBER", 26, P_MEMBER },
	};
	int i;
	for (i = 0; i < (int)(sizeof(t) / sizeof(t[0])); i++)
		if (t[i].len == len && t[i].s[0] == n[0] && !memcmp(t[i].s, n, len))
			return t[i].prop;
	return P_OTHER;
}

/* a value cs_to_atari wouldn't change (no 8-bit byte, no control
   character: they would be left out) into the pool as it is, NUL after
   it, in one pass over it: no malloc and no second copy. 0 (the pool as
   it was) when it isn't one, or there is no room */
static int add_plain(SBUF *pool, const char *s, long n)
{
	const unsigned char *p = (const unsigned char *)s, *e = p + n;
	char *d;
	if (!sb_room(pool, n + 1))
		return 0;
	d = pool->s + pool->len;
	while (p < e) {
		unsigned char c = *p++;
		if (c - 32u > 95u && c != 9 && c != 10 && c != 13)
			return 0;
		*d++ = (char)c;
	}
	*d = 0;
	pool->len += n + 1;
	pool->s[pool->len] = 0;
	return 1;
}

/* the next line's place in v, made ready: the line is made where it is
   kept, not copied there (a 68000 takes a millisecond over a copy of
   one). NULL when there's no memory for it */
static VLINE *line_slot(VLINES *v)
{
	VLINE *l;
	if (v->n >= v->max) {
		VLINE *nl;
		int max = v->max ? v->max * 2 : 32;
		nl = realloc(v->l, max * sizeof(VLINE));
		if (!nl) {
			vcard_nomem = 1;
			return 0;
		}
		v->l = nl;
		v->max = max;
	}
	l = &v->l[v->n];
	l->group[0] = 0;
	l->types[0] = 0;
	l->omit_year = 0;
	l->binary = 0;
	return l;
}

/* group.NAME;params: into l, from the line [ls, le); where its ':' is,
   or NULL when it has none (a NUL in the line ends it, as it did a
   copy's) */
static const char *line_head(VLINE *l, const char *ls, const char *le, int *qp, int *cs)
{
	const char *colon, *q, *nm;
	int quoted = 0;
	for (colon = ls; colon < le && *colon && (quoted || *colon != ':'); colon++)
		if (*colon == '"')
			quoted = !quoted;
	if (colon >= le || !*colon)
		return 0;
	for (q = ls; q < colon && *q != ';' && *q != '.'; q++)
		;
	nm = ls;
	if (q < colon && *q == '.') {
		lower_into(l->group, ls, q - ls, sizeof(l->group));
		nm = q + 1;
		for (q = nm; q < colon && *q != ';'; q++)
			;
	}
	{
		int i = 0;
		const char *s;
		for (s = nm; s < q && i < (int)sizeof(l->name) - 1; s++) {
			char c = *s;
			l->name[i++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
		}
		l->name[i] = 0;
		l->prop = (unsigned char)prop_of(l->name, i);
	}
	if (q < colon)
		parse_params(l, q + 1, colon, qp, cs);
	return colon;
}

/* every content line of one vCard, from BEGIN to END; returns where
   the next one starts, or NULL when there is none */
/* keep_raw: each line as it was too, for writing it back (vcard_write) */
static const char *read_lines(const char *text, const char *end, VLINES *v, int keep_raw)
{
	/* the unfolded line, kept from card to card (unless a photo made it
	   big): on the Atari a malloc and a free for each card cost */
	static SBUF line;
	const char *p = text, *eol, *ret = 0;
	int began = 0;
	while (p < end) {
		const char *next = phys_line(p, end, &eol), *colon = 0, *ls, *le;
		VLINE *l = 0;
		int qp = 0, cs = CS_UTF8, folded;
		/* a line as it is in the text, [ls, le): copied only when it is
		   folded (or quoted-printable goes on, below) */
		ls = p;
		le = eol;
		p = next;
		folded = p < end && (*p == ' ' || *p == '\t');
		if (folded && !keep_raw) {
			/* a photo (or a key, a sound) folded over many lines: what
			   it is, from its first, and the rest passed over, not
			   unfolded (tens of kilobytes; its value isn't kept) */
			if (!(l = line_slot(v)))
				break;
			colon = line_head(l, ls, le, &qp, &cs);
			if (colon && !qp && l->prop != P_BEGIN && l->prop != P_END &&
			    (l->binary || l->prop == P_PHOTO || l->prop == P_LOGO || l->prop == P_KEY ||
			     l->prop == P_SOUND)) {
				while (p < end && (*p == ' ' || *p == '\t'))
					p = phys_line(p, end, &eol);
				folded = 0;
			} else {
				colon = 0;
				qp = 0;
				cs = CS_UTF8;
			}
		}
		if (folded) {
			/* folded: the next line starts with a space or a tab */
			int room;
			sb_reset(&line);
			room = sb_add(&line, ls, le - ls);
			while (p < end && (*p == ' ' || *p == '\t')) {
				next = phys_line(p, end, &eol);
				room = sb_add(&line, p + 1, eol - p - 1) && room;
				p = next;
			}
			if (!room) {
				vcard_nomem = 1;	/* not even the line: the card isn't made */
				break;
			}
			ls = line.s;
			le = line.s + line.len;
		}
		if (!colon) {
			if (le == ls)
				continue;
			if (!(l = line_slot(v)))
				break;
			if (!(colon = line_head(l, ls, le, &qp, &cs)))
				continue;
		}
		if (l->prop == P_BEGIN) {
			if (le - colon > 5 && !strncasecmp(colon + 1, "VCARD", 5)) {
				began = 1;
				v->n = 0;
				sb_reset(&v->pool);
			}
			continue;
		}
		if (!began)
			continue;
		if (l->prop == P_END) {
			ret = p;
			break;
		}
		/* quoted-printable (2.1): a line ending in '=' goes on in the next */
		if (qp && le[-1] == '=' && p < end) {
			int quoted = 0;
			if (ls != line.s) {
				sb_reset(&line);
				if (!sb_add(&line, ls, le - ls)) {
					vcard_nomem = 1;
					break;
				}
			}
			while (line.len && line.s[line.len - 1] == '=' && p < end) {
				next = phys_line(p, end, &eol);
				line.len--;
				if (!sb_add(&line, p, eol - p))
					vcard_nomem = 1;
				p = next;
			}
			ls = line.s;
			le = line.s + line.len;
			for (colon = ls; *colon && (quoted || *colon != ':'); colon++)
				if (*colon == '"')
					quoted = !quoted;
		}
		l->roff = -1;
		if (keep_raw) {
			l->roff = v->pool.len;
			if (!sb_add(&v->pool, ls, le - ls) || !sb_addc(&v->pool, 0))
				l->roff = -2;
		}
		if (l->binary || l->prop == P_PHOTO || l->prop == P_LOGO || l->prop == P_KEY || l->prop == P_SOUND) {
			l->binary = 1;
			l->voff = v->pool.len;
			if (!sb_addc(&v->pool, 0))
				l->voff = -2;
		} else {
			const char *val = colon + 1;
			long vn = le - val;
			char *dec = 0, *at;
			long dn;
			l->voff = v->pool.len;
			if (!qp && cs == CS_UTF8 && add_plain(&v->pool, val, vn)) {
				/* most lines */
			} else {
				if (qp && (dec = qp_decode(val, vn, 0, &dn)) != 0) {
					val = dec;
					vn = dn;
				}
				at = cs_to_atari(val, vn, cs, &dn);
				free(dec);
				if (!at || !sb_add(&v->pool, at, dn + 1))
					l->voff = -2;
				free(at);
			}
		}
		if (l->roff == -2 || l->voff == -2) {
			vcard_nomem = 1;
			continue;
		}
		v->n++;
	}
	lines_point(v);
	if (line.cap > 8192)
		sb_free(&line);
	return ret ? ret : began ? end : 0;
}

/* unescape \n \, \; \\ in place; with split, stop at an unescaped split
   character and return what follows it (else NULL) */
static char *unescape(char *s, char split)
{
	char *d = s, *rest = 0;
	while (*s) {
		if (*s == '\\' && s[1]) {
			s++;
			*d++ = (*s == 'n' || *s == 'N') ? '\n' : *s;
			s++;
			continue;
		}
		if (split && *s == split) {
			rest = s + 1;
			break;
		}
		*d++ = *s++;
	}
	*d = 0;
	return rest;
}

/* the TYPE words the labels go by, as bits: the list looked at once a
   line, not once for each word (a 68000 takes its time over a string) */
enum { T_IPHONE = 1, T_CELL = 2, T_MOBILE = 4, T_FAX = 8, T_HOME = 16, T_WORK = 32, T_PAGER = 64,
       T_MAIN = 128, T_OTHER = 256, T_SCHOOL = 512, T_MOBILEME = 1024 };

static unsigned type_bits(const VLINE *l)
{
	static const struct {
		const char *s;
		unsigned char n;
		unsigned short bit;
	} w[] = {
		{ "home", 4, T_HOME }, { "work", 4, T_WORK }, { "cell", 4, T_CELL }, { "fax", 3, T_FAX },
		{ "iphone", 6, T_IPHONE }, { "mobile", 6, T_MOBILE }, { "pager", 5, T_PAGER }, { "main", 4, T_MAIN },
		{ "other", 5, T_OTHER }, { "school", 6, T_SCHOOL }, { "x-mobileme", 10, T_MOBILEME },
	};
	const char *p = l->types;
	unsigned bits = 0;
	while (*p) {
		const char *e = p;
		int n, i;
		while (*e && *e != ',')
			e++;
		n = (int)(e - p);
		for (i = 0; i < (int)(sizeof(w) / sizeof(w[0])); i++)
			if (w[i].n == n && w[i].s[0] == p[0] && !memcmp(p, w[i].s, n)) {
				bits |= w[i].bit;
				break;
			}
		if (!*e)
			break;
		p = e + 1;
	}
	return bits;
}

/* Apple's "_$!<HomeFAX>!$_" -> "home fax"; a label the user typed stays */
static void ab_label(const char *s, char *out, int size)
{
	static const struct { const char *ab, *mac; } map[] = {
		{ "Mobile", "mobile" }, { "iPhone", "iPhone" }, { "Home", "home" }, { "Work", "work" },
		{ "Main", "main" }, { "HomeFAX", "home fax" }, { "WorkFAX", "work fax" },
		{ "OtherFAX", "other fax" }, { "Pager", "pager" }, { "Other", "other" },
		{ "HomePage", "homepage" }, { "School", "school" }, { "AppleWatch", "Apple Watch" },
	};
	int i;
	if (!strncmp(s, "_$!<", 4)) {
		const char *e = strstr(s, ">!$_");
		char w[32];
		int n = e ? (int)(e - s - 4) : (int)strlen(s + 4);
		if (n > (int)sizeof(w) - 1)
			n = sizeof(w) - 1;
		memcpy(w, s + 4, n);
		w[n] = 0;
		for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
			if (!strcasecmp(w, map[i].ab)) {
				str_copy(out, map[i].mac, size);
				return;
			}
		str_copy(out, w, size);
		if (out[0] >= 'A' && out[0] <= 'Z')
			out[0] += 32;
		return;
	}
	str_copy(out, s, size);
}

static const char *tel_label(unsigned t)
{
	if (t & T_IPHONE)
		return "iPhone";
	if (t & (T_CELL | T_MOBILE))
		return "mobile";
	if (t & T_FAX)
		return t & T_HOME ? "home fax" : t & T_WORK ? "work fax" : "other fax";
	if (t & T_PAGER)
		return "pager";
	if (t & T_MAIN)
		return "main";
	if (t & T_HOME)
		return "home";
	if (t & T_WORK)
		return "work";
	if (t & T_OTHER)
		return "other";
	return "phone";
}

static const char *place_label(unsigned t, const char *dflt)
{
	if (t & T_HOME)
		return "home";
	if (t & T_WORK)
		return "work";
	if (t & T_SCHOOL)
		return "school";
	if (t & T_OTHER)
		return "other";
	return dflt;
}

/* ---------------- building the CONTACT ---------------- */

typedef struct {
	long lab, val;
} ITOFF;

typedef struct {
	SBUF pool;
	long f[14];		/* the string fields, as offsets (-1: none) */
	ITOFF *it[4];		/* email, tel, adr, url */
	int nit[4], cap[4];
	long *mem;
	int nmem, capmem;
	int failed;
} BLD;

/* The usual labels are kept once, here, not in every contact: an
   offset of LAB_SHARED - k in ITOFF.lab is shared_labels[k] */
static const char *const shared_labels[] = { "mobile", "iPhone", "home", "work", "main", "home fax",
	"work fax", "pager", "other", "school", "iCloud", "homepage", 0 };
#define LAB_SHARED (-2L)

enum { F_UID, F_FN, F_FAMILY, F_GIVEN, F_MIDDLE, F_PREFIX, F_SUFFIX, F_NICK, F_ORG, F_DEPT, F_TITLE,
       F_BDAY, F_NOTE, F_CATS };
enum { I_EMAIL, I_TEL, I_ADR, I_URL };

static long bstr(BLD *b, const char *s)
{
	long off = b->pool.len, n;
	char *d;
	if (!s)
		return -1;
	n = (long)strlen(s) + 1;
	if (!sb_room(&b->pool, n)) {
		b->failed = 1;
		return off;
	}
	/* (copied here: memcpy goes a byte at a time between odd and even
	   addresses, slower than this) */
	d = b->pool.s + off;
	while ((*d++ = *s++))
		;
	b->pool.len += n;
	b->pool.s[b->pool.len] = 0;
	return off;
}

static void bfield(BLD *b, int f, const char *s)
{
	if (b->f[f] < 0 && s && *s)
		b->f[f] = bstr(b, s);
}

static void bitem(BLD *b, int k, const char *label, const char *value)
{
	ITOFF *n;
	int j;
	if (!value || !*value || b->nit[k] >= 255)
		return;
	if (b->nit[k] >= b->cap[k]) {
		int cap = b->cap[k] ? b->cap[k] * 2 : 8;
		n = realloc(b->it[k], cap * sizeof(ITOFF));
		if (!n) {
			b->failed = 1;
			return;
		}
		b->it[k] = n;
		b->cap[k] = cap;
	}
	n = &b->it[k][b->nit[k]];
	n->lab = -1;
	for (j = 0; label && shared_labels[j]; j++)
		if (!strcmp(label, shared_labels[j])) {
			n->lab = LAB_SHARED - j;
			break;
		}
	if (n->lab == -1)
		n->lab = bstr(b, label);
	n->val = bstr(b, value);
	b->nit[k]++;
}

static void bmember(BLD *b, const char *uid)
{
	long *n;
	if (!*uid || b->nmem >= 4000)
		return;
	if (b->nmem >= b->capmem) {
		int cap = b->capmem ? b->capmem * 2 : 16;
		n = realloc(b->mem, cap * sizeof(long));
		if (!n) {
			b->failed = 1;
			return;
		}
		b->mem = n;
		b->capmem = cap;
	}
	b->mem[b->nmem++] = bstr(b, uid);
}

/* a member's address "urn:uuid:ABC" -> "ABC" */
static const char *member_uid(const char *v)
{
	if (!strncasecmp(v, "urn:uuid:", 9))
		return v + 9;
	return v;
}

static CONTACT *assemble(BLD *b, unsigned char company, unsigned char group, unsigned char photo)
{
	long size, items = 0, off;
	int k, i;
	CONTACT *c;
	char *pool;
	VCITEM *it;
	const char **mem;
	const char **fields;
	for (k = 0; k < 4; k++)
		items += b->nit[k];
	size = sizeof(CONTACT) + items * sizeof(VCITEM) + b->nmem * sizeof(char *) + b->pool.len + 1;
	c = malloc(size);
	if (!c)
		return 0;
	memset(c, 0, sizeof(*c));
	c->bsize = size;
	it = (VCITEM *)(c + 1);
	mem = (const char **)(it + items);
	pool = (char *)(mem + b->nmem);
	if (b->pool.len)
		memcpy(pool, b->pool.s, b->pool.len);
	pool[b->pool.len] = 0;
	fields = &c->uid;
	for (i = 0; i < 14; i++)
		fields[i] = b->f[i] >= 0 ? pool + b->f[i] : 0;
	off = 0;
	for (k = 0; k < 4; k++) {
		VCITEM *start = it + off;
		for (i = 0; i < b->nit[k]; i++) {
			long lab = b->it[k][i].lab;
			start[i].label = lab >= 0 ? pool + lab : lab <= LAB_SHARED ? shared_labels[LAB_SHARED - lab] : "";
			start[i].value = pool + b->it[k][i].val;
		}
		switch (k) {
		case I_EMAIL: c->email = start; c->nemail = (unsigned char)b->nit[k]; break;
		case I_TEL: c->tel = start; c->ntel = (unsigned char)b->nit[k]; break;
		case I_ADR: c->adr = start; c->nadr = (unsigned char)b->nit[k]; break;
		case I_URL: c->url = start; c->nurl = (unsigned char)b->nit[k]; break;
		}
		off += b->nit[k];
	}
	for (i = 0; i < b->nmem; i++)
		mem[i] = pool + b->mem[i];
	c->member = mem;
	c->nmember = (unsigned short)b->nmem;
	c->company = company;
	c->group = group;
	c->photo = photo;
	c->book = c->res = -1;
	vcard_sort_key(c, 0);
	return c;
}

/* the ADR value "pobox;ext;street;city;region;zip;country" -> the parts
   on lines (street, city, region, postcode, country) */
static void adr_lines(char *v, char *out, int size)
{
	char *part[7];
	int i, n = 0;
	char *p = v;
	for (i = 0; i < 7; i++) {
		part[i] = p;
		p = p ? unescape(p, ';') : 0;
	}
	out[0] = 0;
	/* street (with the PO box and extended address before it), at most
	   199 letters; the parts are lines: a street of two lines is joined
	   by ", " (no strncat or snprintf: a 68000 takes milliseconds) */
	{
		int k = 0;
		for (i = 0; i < 3; i++)
			if (part[i] && *part[i]) {
				const char *q = part[i];
				if (k && k < 199) {
					if (n >= size - 3)
						break;
					out[n++] = ',';
					out[n++] = ' ';
					k++;
				}
				for (; *q && k < 199; q++, k++) {
					if (n >= size - 3)
						break;
					if (*q == '\n') {
						out[n++] = ',';
						out[n++] = ' ';
					} else {
						out[n++] = *q;
					}
				}
			}
		/* then the city, region, postcode and country, a line each */
		for (i = 3; i < 7; i++) {
			const char *q = part[i] ? part[i] : "";
			if (n < size - 1)
				out[n] = '\n';
			n++;
			for (; *q; q++, n++)
				if (n < size - 1)
					out[n] = *q;
		}
		out[n < size - 1 ? n : size - 1] = 0;
	}
}

/* the builder's room kept from card to card, as the lines' are: what
   is left in memory of a card is its CONTACT, and nothing between them */
static void bld_reset(BLD *b)
{
	int k;
	if (b->pool.cap > 8192)
		sb_free(&b->pool);
	sb_reset(&b->pool);
	for (k = 0; k < 4; k++) {
		if (b->cap[k] > 64) {
			free(b->it[k]);
			b->it[k] = 0;
			b->cap[k] = 0;
		}
		b->nit[k] = 0;
	}
	if (b->capmem > 256) {
		free(b->mem);
		b->mem = 0;
		b->capmem = 0;
	}
	b->nmem = 0;
	b->failed = 0;
	for (k = 0; k < 14; k++)
		b->f[k] = -1;
}

CONTACT *vcard_parse(const char *text, long len)
{
	static VLINES v;
	static BLD b;
	CONTACT *c = 0;
	int i, k;
	unsigned char company = 0, group = 0, photo = 0;
	unsigned long photo_key = 0, marked = 0;
	lines_reset(&v);
	bld_reset(&b);
	vcard_nomem = 0;
	if (!read_lines(text, text + len, &v, 0) && !v.n)
		return 0;
	for (i = 0; i < v.n; i++) {
		VLINE *l = &v.l[i];
		char label[40];
		label[0] = 0;
		/* Apple's label for this group */
		if (l->group[0])
			for (k = 0; k < v.n; k++)
				if (k != i && v.l[k].prop == P_ABLABEL && !strcmp(v.l[k].group, l->group)) {
					char *t = strdup(v.l[k].value);
					if (t) {
						unescape(t, 0);
						ab_label(t, label, sizeof(label));
						free(t);
					}
				}
		if (l->prop == P_N) {
			char *p = l->value, *part[5];
			int j;
			for (j = 0; j < 5; j++) {
				part[j] = p;
				p = p ? unescape(p, ';') : 0;
			}
			bfield(&b, F_FAMILY, part[0]);
			bfield(&b, F_GIVEN, part[1]);
			bfield(&b, F_MIDDLE, part[2]);
			bfield(&b, F_PREFIX, part[3]);
			bfield(&b, F_SUFFIX, part[4]);
		} else if (l->prop == P_FN) {
			unescape(l->value, 0);
			bfield(&b, F_FN, l->value);
		} else if (l->prop == P_UID) {
			unescape(l->value, 0);
			bfield(&b, F_UID, member_uid(l->value));
		} else if (l->prop == P_NICKNAME) {
			unescape(l->value, 0);
			bfield(&b, F_NICK, l->value);
		} else if (l->prop == P_ORG) {
			char *dept = unescape(l->value, ';');
			bfield(&b, F_ORG, l->value);
			if (dept) {
				unescape(dept, ';');
				bfield(&b, F_DEPT, dept);
			}
		} else if (l->prop == P_TITLE) {
			unescape(l->value, 0);
			bfield(&b, F_TITLE, l->value);
		} else if (l->prop == P_NOTE) {
			unescape(l->value, 0);
			bfield(&b, F_NOTE, l->value);
		} else if (l->prop == P_CATEGORIES) {
			unescape(l->value, 0);
			bfield(&b, F_CATS, l->value);
		} else if (l->prop == P_BDAY) {
			char d[16];
			int y = 0, m = 0, dd = 0;
			unescape(l->value, 0);
			if (vcard_bday_split(l->value, &y, &m, &dd)) {
				char *q = d;
				if (l->omit_year && y == l->omit_year)
					y = 0;
				/* "1980-05-17" or "--05-17", without snprintf */
				if (y) {
					*q++ = (char)('0' + y / 1000 % 10);
					*q++ = (char)('0' + y / 100 % 10);
					*q++ = (char)('0' + y / 10 % 10);
					*q++ = (char)('0' + y % 10);
				} else {
					*q++ = '-';
				}
				*q++ = '-';
				*q++ = (char)('0' + m / 10);
				*q++ = (char)('0' + m % 10);
				*q++ = '-';
				*q++ = (char)('0' + dd / 10);
				*q++ = (char)('0' + dd % 10);
				*q = 0;
				bfield(&b, F_BDAY, d);
			}
		} else if (l->prop == P_EMAIL) {
			unsigned t = label[0] ? 0 : type_bits(l);
			unescape(l->value, 0);
			bitem(&b, I_EMAIL, label[0] ? label : place_label(t, t & T_MOBILEME ? "iCloud" : "other"),
			      str_trim(l->value));
		} else if (l->prop == P_TEL) {
			unescape(l->value, 0);
			if (!strncasecmp(l->value, "tel:", 4))
				memmove(l->value, l->value + 4, strlen(l->value + 4) + 1);
			bitem(&b, I_TEL, label[0] ? label : tel_label(type_bits(l)), str_trim(l->value));
		} else if (l->prop == P_ADR) {
			char lines[400];
			adr_lines(l->value, lines, sizeof(lines));
			/* an address with nothing in it at all is left out */
			for (k = 0; lines[k] && (lines[k] == '\n' || lines[k] == ' '); k++)
				;
			if (lines[k])
				bitem(&b, I_ADR, label[0] ? label : place_label(type_bits(l), "other"), lines);
		} else if (l->prop == P_URL) {
			unescape(l->value, 0);
			bitem(&b, I_URL, label[0] ? label : place_label(type_bits(l), "homepage"), str_trim(l->value));
		} else if (l->prop == P_SHOWAS) {
			if (!strncasecmp(l->value, "COMPANY", 7))
				company = 1;
		} else if (l->prop == P_KIND) {
			if (!strncasecmp(l->value, "group", 5))
				group = 1;
			else if (!strncasecmp(l->value, "org", 3))
				company = 1;
		} else if (l->prop == P_MEMBER) {
			unescape(l->value, 0);
			bmember(&b, member_uid(str_trim(l->value)));
		} else if (l->prop == P_PHOTO) {
			photo = 1;		/* its fingerprint below, from the text as it came */
		} else if (l->prop == P_MARK) {
			/* the cache's stand-in for a photo kept elsewhere (abook.c) */
			photo = 1;
			marked = l->value[0] == '?' ? VC_KEY_UNKNOWN : strtoul(l->value, 0, 16);
			if (!marked)
				marked = VC_KEY_UNKNOWN;
		}
	}
	if (marked)
		photo_key = marked;
	else if (photo) {
		long s, e;
		photo_key = vcard_photo_span(text, len, &s, &e) && !vcard_photo_empty(text + s, e - s) ?
			    vcard_photo_key(text + s, e - s) : VC_KEY_UNKNOWN;
	}
	if (b.failed)
		vcard_nomem = 1;
	if (!vcard_nomem) {
		c = assemble(&b, company, group, photo);
		if (c)
			c->photo_key = photo_key;
		else
			vcard_nomem = 1;
	}
	return c;
}

/* ---------------- packed ---------------- */

/* where a CONTACT's pointers are: its 14 strings, the four item lists
   and the members; then each item's label and value, and each member */
#define PK_NULL   (-1L)
#define PK_EMPTY  (-2L)			/* "" outside the block */
#define PK_SHARED (-16L)		/* shared_labels[PK_SHARED - v] */

static long pk_off(const char *base, long size, const void *p)
{
	const char *q = p;
	int k;
	if (!q)
		return PK_NULL;
	if (q >= base && q < base + size)
		return (long)(q - base);
	for (k = 0; shared_labels[k]; k++)
		if (q == shared_labels[k])
			return PK_SHARED - k;
	if (!*q)
		return PK_EMPTY;
	return -1000;			/* somewhere else: can't be packed */
}

static int pk_ptr(const char *base, long size, const void **slot)
{
	long v = pk_off(base, size, *slot);
	if (v == -1000)
		return 0;
	*(long *)slot = v;
	return 1;
}

#define N_SHARED ((long)(sizeof(shared_labels) / sizeof(shared_labels[0])) - 1)

static int un_ptr(char *base, long size, const void **slot, long minoff)
{
	long v = *(long *)slot;
	if (v >= minoff && v < size) {
		*slot = base + v;		/* the usual case first */
		return 1;
	}
	if (v == PK_NULL) {
		*slot = 0;
		return 1;
	}
	if (v == PK_EMPTY) {
		*slot = "";
		return 1;
	}
	if (v <= PK_SHARED && PK_SHARED - v < N_SHARED) {
		*slot = shared_labels[PK_SHARED - v];
		return 1;
	}
	if (v < minoff || v >= size)
		return 0;
	*slot = base + v;
	return 1;
}

static long n_items(const CONTACT *c)
{
	return (long)c->nemail + c->ntel + c->nadr + c->nurl;
}

int vcard_pack(const CONTACT *c, char *out)
{
	CONTACT *p = (CONTACT *)out;
	const char *base = (const char *)c;
	long size = c->bsize, i, items = n_items(c);
	const void **f;
	VCITEM *it;
	memcpy(out, c, size);
	/* the items and members first: their pointers are read from the copy */
	it = (VCITEM *)(out + ((const char *)(c + 1) - base));
	for (i = 0; i < items; i++)
		if (!pk_ptr(base, size, (const void **)&it[i].label) || !pk_ptr(base, size, (const void **)&it[i].value))
			return 0;
	for (i = 0; i < c->nmember; i++)
		if (!pk_ptr(base, size, (const void **)(out + ((const char *)&c->member[i] - base))))
			return 0;
	f = (const void **)&p->uid;
	for (i = 0; i < 14; i++)
		if (!pk_ptr(base, size, &f[i]))
			return 0;
	return pk_ptr(base, size, (const void **)&p->email) && pk_ptr(base, size, (const void **)&p->tel) &&
	       pk_ptr(base, size, (const void **)&p->adr) && pk_ptr(base, size, (const void **)&p->url) &&
	       pk_ptr(base, size, (const void **)&p->member);
}

CONTACT *vcard_unpack(char *blk, long len)
{
	CONTACT *c = (CONTACT *)blk;
	const void **f;
	long i, items, min = (long)sizeof(CONTACT);
	if (len < (long)sizeof(CONTACT) || c->bsize != len)
		return 0;
	f = (const void **)&c->uid;
	for (i = 0; i < 14; i++)
		if (!un_ptr(blk, len, &f[i], min))
			return 0;
	if (!un_ptr(blk, len + 1, (const void **)&c->email, min) || !un_ptr(blk, len + 1, (const void **)&c->tel, min) ||
	    !un_ptr(blk, len + 1, (const void **)&c->adr, min) || !un_ptr(blk, len + 1, (const void **)&c->url, min) ||
	    !un_ptr(blk, len + 1, (const void **)&c->member, min))
		return 0;
	items = n_items(c);
	/* the lists where assemble() puts them, and inside the block */
	if ((char *)c->email != (char *)(c + 1) || (char *)(c->email + items) > blk + len ||
	    (char *)(c->member + c->nmember) > blk + len)
		return 0;
	for (i = 0; i < items; i++) {
		VCITEM *it = &c->email[i];
		if (!un_ptr(blk, len, (const void **)&it->label, min) || !un_ptr(blk, len, (const void **)&it->value, min) ||
		    !it->value)
			return 0;
	}
	for (i = 0; i < c->nmember; i++)
		if (!un_ptr(blk, len, (const void **)&c->member[i], min) || !c->member[i])
			return 0;
	/* the strings end inside the block */
	if (blk[len - 1])
		return 0;
	c->mark = 0;
	return c;
}

int vcard_parse_all(const char *text, long len, int (*cb)(CONTACT *c, void *ud), void *ud)
{
	const char *p = text, *end = text + len;
	int n = 0;
	while (p < end) {
		const char *b = str_istr(p, "BEGIN:VCARD"), *e;
		CONTACT *c;
		if (!b || b >= end)
			break;
		e = str_istr(b, "END:VCARD");
		if (!e || e >= end)
			e = end;
		else
			e += 9;
		c = vcard_parse(b, e - b);
		p = e;
		if (!c)
			continue;
		n++;
		if (!cb(c, ud))
			break;
	}
	return n;
}

/* ---------------- names ---------------- */

static void cat_word(char *out, int size, const char *w)
{
	int n = (int)strlen(out);
	if (!w || !*w)
		return;
	if (n && n < size - 1)
		out[n++] = ' ';
	str_copy(out + n, w, size - n);
}

void vcard_display_name(const CONTACT *c, int last_first, char *out, int size)
{
	out[0] = 0;
	if (c->group || (c->company && c->org && *c->org)) {
		str_copy(out, c->group ? (c->fn ? c->fn : c->family ? c->family : "Group") : c->org, size);
		return;
	}
	if ((c->given && *c->given) || (c->family && *c->family)) {
		if (last_first && c->family && *c->family && c->given && *c->given) {
			snprintf(out, size, "%s, %s", c->family, c->given);
			if (c->middle && *c->middle)
				cat_word(out, size, c->middle);
		} else {
			cat_word(out, size, c->given);
			cat_word(out, size, c->middle);
			cat_word(out, size, c->family);
		}
		return;
	}
	if (c->fn && *c->fn)
		str_copy(out, c->fn, size);
	else if (c->org && *c->org)
		str_copy(out, c->org, size);
	else if (c->nemail)
		str_copy(out, c->email[0].value, size);
	else if (c->ntel)
		str_copy(out, c->tel[0].value, size);
	else
		str_copy(out, "No Name", size);
}

static char upper(unsigned char ch)
{
	/* Latin letters with accents (Atari's codes) sort with their letter */
	static const char *const acc[] = {
		"C\x80\x87", "U\x81\x96\x97\x9a\xa3", "E\x82\x88\x89\x8a\x90", "A\x83\x84\x85\x86\x8e\x8f\xa0\xb0\xb6\xb7",
		"I\x8b\x8c\x8d\xa1", "O\x93\x94\x95\x99\xa2\xb1\xb2\xb3\xb8", "N\xa4\xa5", "Y\x98",
	};
	int i;
	if (ch >= 'a' && ch <= 'z')
		return (char)(ch - 32);
	if (ch >= 0x80 && ch < 0xc0)
		for (i = 0; i < (int)(sizeof(acc) / sizeof(acc[0])); i++)
			if (strchr(acc[i] + 1, ch))
				return acc[i][0];
	return (char)ch;
}

void vcard_sort_key(CONTACT *c, int by_first)
{
	char t[64];
	const char *a, *b2;
	int i, n = 0;
	if (c->group || (c->company && c->org && *c->org)) {
		a = c->group ? c->fn : c->org;
		b2 = 0;
	} else if (by_first) {
		a = c->given && *c->given ? c->given : c->family;
		b2 = c->given && *c->given ? c->family : 0;
	} else {
		a = c->family && *c->family ? c->family : c->given;
		b2 = c->family && *c->family ? c->given : 0;
	}
	if (!a || !*a) {
		vcard_display_name(c, 0, t, sizeof(t));
		a = t;
		b2 = 0;
	}
	for (i = 0; a[i] && n < (int)sizeof(c->sortkey) - 1; i++)
		c->sortkey[n++] = upper((unsigned char)a[i]);
	if (b2 && *b2 && n < (int)sizeof(c->sortkey) - 2) {
		c->sortkey[n++] = 1;		/* before any letter: "SMITH" < "SMITHSON" */
		for (i = 0; b2[i] && n < (int)sizeof(c->sortkey) - 1; i++)
			c->sortkey[n++] = upper((unsigned char)b2[i]);
	}
	c->sortkey[n] = 0;
}

void vcard_initials(const CONTACT *c, char *out)
{
	int n = 0;
	const char *g = c->given, *f = c->family;
	if (c->group) {
		strcpy(out, "\xb9");		/* a group: no initials */
		return;
	}
	if (c->company && c->org && *c->org) {
		g = c->org;
		f = 0;
	}
	if (g && *g)
		out[n++] = upper((unsigned char)*g);
	if (f && *f)
		out[n++] = upper((unsigned char)*f);
	if (!n) {
		char t[64];
		vcard_display_name(c, 0, t, sizeof(t));
		out[n++] = upper((unsigned char)t[0]);
	}
	out[n] = 0;
}

/* ---------------- birthdays ---------------- */

int vcard_bday_split(const char *s, int *y, int *m, int *d)
{
	int v[3] = { 0, 0, 0 }, k = 0, digits = 0;
	const char *p = s;
	*y = *m = *d = 0;
	while (*p == ' ')
		p++;
	if (p[0] == '-' && p[1] == '-') {
		/* --0517 or --05-17 */
		p += 2;
		k = 1;
	}
	for (; *p && k < 3; p++) {
		if (*p >= '0' && *p <= '9') {
			int lim = k == 0 ? 4 : 2;
			if (digits == lim) {
				k++;
				digits = 0;
				if (k >= 3)
					break;
			}
			v[k] = v[k] * 10 + (*p - '0');
			digits++;
		} else if (*p == '-' && digits) {
			k++;
			digits = 0;
		} else if (*p == 'T' || *p == ' ') {
			break;
		}
	}
	if (v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31)
		return 0;
	*y = v[0];
	*m = v[1];
	*d = v[2];
	return 1;
}

int vcard_bday_format(const char *bday, char *out, int size)
{
	static const char *const mon[12] = { "January", "February", "March", "April", "May", "June", "July",
					    "August", "September", "October", "November", "December" };
	int y, m, d;
	if (!bday || !vcard_bday_split(bday, &y, &m, &d))
		return 0;
	if (y)
		snprintf(out, size, "%d %s %d", d, mon[m - 1], y);
	else
		snprintf(out, size, "%d %s", d, mon[m - 1]);
	return 1;
}

int vcard_bday_parse(const char *typed, char *out, int size)
{
	int v[3] = { 0, 0, 0 }, n = 0;
	const char *p = typed;
	while (*p && n < 3) {
		while (*p && (*p < '0' || *p > '9'))
			p++;
		if (!*p)
			break;
		while (*p >= '0' && *p <= '9')
			v[n] = v[n] * 10 + (*p++ - '0');
		n++;
	}
	if (n < 2)
		return 0;
	if (v[0] > 31) {
		/* 1980-05-17, as vCard writes it */
		int t = v[0];
		v[0] = v[2];
		v[2] = t;
	}
	if (v[1] < 1 || v[1] > 12 || v[0] < 1 || v[0] > 31)
		return 0;
	if (n == 3 && v[2] < 100)
		v[2] += v[2] > 30 ? 1900 : 2000;
	if (n == 3 && v[2])
		snprintf(out, size, "%04d-%02d-%02d", v[2], v[1], v[0]);
	else
		snprintf(out, size, "--%02d-%02d", v[1], v[0]);
	return 1;
}

/* ---------------- editing ---------------- */

void vcard_to_edit(const CONTACT *c, CEDIT *e)
{
	int i;
	memset(e, 0, sizeof(*e));
	if (!c)
		return;
#define CP(f) if (c->f) str_copy(e->f, c->f, sizeof(e->f));
	CP(prefix) CP(given) CP(middle) CP(family) CP(suffix) CP(nick) CP(org) CP(dept) CP(title) CP(note)
#undef CP
	if (!c->given && !c->family && c->fn && !c->company)
		str_copy(e->given, c->fn, sizeof(e->given));
	for (i = 0; i < c->nemail && i < CE_EMAIL; i++) {
		str_copy(e->email[i].label, c->email[i].label, sizeof(e->email[i].label));
		str_copy(e->email[i].value, c->email[i].value, sizeof(e->email[i].value));
	}
	for (i = 0; i < c->ntel && i < CE_TEL; i++) {
		str_copy(e->tel[i].label, c->tel[i].label, sizeof(e->tel[i].label));
		str_copy(e->tel[i].value, c->tel[i].value, sizeof(e->tel[i].value));
	}
	for (i = 0; i < c->nurl && i < CE_URL; i++) {
		str_copy(e->url[i].label, c->url[i].label, sizeof(e->url[i].label));
		str_copy(e->url[i].value, c->url[i].value, sizeof(e->url[i].value));
	}
	for (i = 0; i < c->nadr && i < CE_ADR; i++) {
		char *parts[5], tmp[400], *p = tmp;
		int k;
		str_copy(e->adr[i].label, c->adr[i].label, sizeof(e->adr[i].label));
		str_copy(tmp, c->adr[i].value, sizeof(tmp));
		for (k = 0; k < 5; k++) {
			parts[k] = p;
			if (p) {
				p = strchr(p, '\n');
				if (p)
					*p++ = 0;
			}
		}
		str_copy(e->adr[i].street, parts[0] ? parts[0] : "", sizeof(e->adr[i].street));
		str_copy(e->adr[i].city, parts[1] ? parts[1] : "", sizeof(e->adr[i].city));
		str_copy(e->adr[i].region, parts[2] ? parts[2] : "", sizeof(e->adr[i].region));
		str_copy(e->adr[i].zip, parts[3] ? parts[3] : "", sizeof(e->adr[i].zip));
		str_copy(e->adr[i].country, parts[4] ? parts[4] : "", sizeof(e->adr[i].country));
	}
	if (c->bday) {
		int y, m, d;
		if (vcard_bday_split(c->bday, &y, &m, &d)) {
			if (y)
				snprintf(e->bday, sizeof(e->bday), "%02d.%02d.%04d", d, m, y);
			else
				snprintf(e->bday, sizeof(e->bday), "%02d.%02d", d, m);
		}
	}
	e->company = c->company;
}

/* the vCard text of an Atari string: escaped, then UTF-8 */
static void put_text(SBUF *b, const char *s, int structured)
{
	SBUF t;
	char *u;
	long n;
	sb_init(&t);
	for (; s && *s; s++) {
		if (*s == '\\' || *s == ',' || *s == ';')
			sb_addc(&t, '\\');
		if (*s == '\n') {
			sb_adds(&t, "\\n");
			continue;
		}
		if (*s == '\r')
			continue;
		sb_addc(&t, *s);
	}
	(void)structured;
	u = atari_to_utf8(t.s ? t.s : "", t.len, &n);
	if (u) {
		sb_add(b, u, n);
		free(u);
	}
	sb_free(&t);
}

/* end a content line, folding it at 75 bytes (never inside a UTF-8 letter) */
static void put_line(SBUF *out, SBUF *line)
{
	long i = 0, col = 0;
	while (i < line->len) {
		long take = line->len - i, room = col ? 74 : 75;
		if (take > room) {
			take = room;
			while (take > 1 && ((unsigned char)line->s[i + take] & 0xC0) == 0x80)
				take--;
		}
		if (col)
			sb_addc(out, ' ');
		sb_add(out, line->s + i, take);
		sb_adds(out, "\r\n");
		i += take;
		col = 1;
	}
	sb_reset(line);
}

/* which old lines the editor rewrites */
static int edited_prop(const char *n)
{
	static const char *const ed[] = { "N", "FN", "NICKNAME", "ORG", "TITLE", "EMAIL", "TEL", "ADR", "URL",
					  "BDAY", "NOTE", "X-ABSHOWAS", "PRODID", "REV", "VERSION", 0 };
	int i;
	for (i = 0; ed[i]; i++)
		if (!strcmp(n, ed[i]))
			return 1;
	return 0;
}

/* the old lines to keep: not rewritten, and an X-ABLabel only while
   its group still has a line kept with it. The highest itemN in them
   comes back, so new labelled lines get numbers after it */
static int kept_lines(VLINES *v, int (*drop)(const VLINE *l, void *ud), void *ud, unsigned char *keep)
{
	int i, k, maxitem = 0;
	for (i = 0; i < v->n; i++)
		keep[i] = !drop(&v->l[i], ud);
	for (i = 0; i < v->n; i++) {
		if (!keep[i] || strcmp(v->l[i].name, "X-ABLABEL"))
			continue;
		keep[i] = 0;
		for (k = 0; k < v->n; k++)
			if (k != i && keep[k] && v->l[i].group[0] && !strcmp(v->l[k].group, v->l[i].group) &&
			    strcmp(v->l[k].name, "X-ABLABEL"))
				keep[i] = 1;
	}
	for (i = 0; i < v->n; i++)
		if (keep[i] && !strncmp(v->l[i].group, "item", 4) && atoi(v->l[i].group + 4) > maxitem)
			maxitem = atoi(v->l[i].group + 4);
	return maxitem;
}

static int drop_edited(const VLINE *l, void *ud)
{
	/* a UID given replaces the old one (an import that clashed) */
	return edited_prop(l->name) || (ud && !strcmp(l->name, "UID"));
}

static int empty(const char *s)
{
	while (*s == ' ')
		s++;
	return !*s;
}

/* a labelled line: TYPE= for the labels vCard has, Apple's itemN and
   X-ABLabel for the others */
static void put_labelled(SBUF *out, SBUF *line, const char *prop, const char *label, const char *const *std_types,
			 const char *const *std_labels, int *item, const char *value_utf8, long vn)
{
	int i, std = -1;
	char grp[16] = "";
	for (i = 0; std_labels[i]; i++)
		if (!strcasecmp(label, std_labels[i]))
			std = i;
	if (std < 0 || !std_types[std]) {
		snprintf(grp, sizeof(grp), "item%d.", ++*item);
	}
	sb_printf(line, "%s%s", grp, prop);
	if (std >= 0 && std_types[std] && *std_types[std])
		sb_printf(line, ";%s", std_types[std]);
	sb_addc(line, ':');
	sb_add(line, value_utf8, vn);
	put_line(out, line);
	if (grp[0]) {
		/* Apple's own words for its labels; the user's typed as they are */
		static const struct { const char *mac, *ab; } map[] = {
			{ "homepage", "_$!<HomePage>!$_" }, { "other fax", "_$!<OtherFAX>!$_" },
			{ "school", "_$!<School>!$_" }, { "iCloud", "iCloud" },
		};
		const char *ab = label[0] ? label : "other";
		for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
			if (!strcasecmp(label, map[i].mac))
				ab = map[i].ab;
		grp[strlen(grp) - 1] = 0;
		sb_printf(line, "%s.X-ABLabel:", grp);
		put_text(line, ab, 0);
		put_line(out, line);
	}
}

static void begin_card(SBUF *out, VLINES *v)
{
	int i;
	const char *ver = "3.0";
	for (i = 0; i < v->n; i++)
		if (!strcmp(v->l[i].name, "VERSION") && !strncmp(v->l[i].value, "4.0", 3))
			ver = "4.0";
	sb_printf(out, "BEGIN:VCARD\r\nVERSION:%s\r\nPRODID:-//EContact//Atari ST//EN\r\n", ver);
}

static void copy_kept(SBUF *out, SBUF *line, VLINES *v, const unsigned char *keep, int *has_uid)
{
	int i;
	for (i = 0; i < v->n; i++) {
		if (!keep[i])
			continue;
		if (!strcmp(v->l[i].name, "UID"))
			*has_uid = 1;
		sb_adds(line, v->l[i].raw);
		put_line(out, line);
	}
}

static void put_uid(SBUF *out, SBUF *line, const char *uid)
{
	sb_adds(line, "UID:");
	put_text(line, uid, 0);
	put_line(out, line);
}

/* ---------------- the photo line ---------------- */

int vcard_photo_span(const char *raw, long len, long *start, long *stop)
{
	const char *p = raw, *end = raw + len, *line = 0, *q;
	int v21 = 0;
	/* each line looked at once (as line_is(p, end, "PHOTO") did), its
	   first letter before strncasecmp: every card at start-up comes here
	   (abook.c parse_kept), and a 68000 takes some 10 us a letter a pass */
	while (p < end) {
		const char *nm = p;
		for (q = p; q < end; q++) {
			char c = *q;
			if (c == ':' || c == ';' || c == '\n' || c == '.')
				break;
		}
		if (q < end && *q == '.')
			nm = q + 1;
		if (end - nm > 5 && (nm[0] | 32) == 'p' && !strncasecmp(nm, "PHOTO", 5) && (nm[5] == ':' || nm[5] == ';')) {
			line = p;
			break;
		}
		if (end - p > 9 && (p[0] | 32) == 'v' && !strncasecmp(p, "VERSION:2", 9))
			v21 = 1;
		while (q < end && *q != '\n')
			q++;
		if (q >= end)
			break;
		p = q + 1;
	}
	if (!line)
		return 0;
	/* the logical line ends at a line that doesn't start with a space;
	   2.1's base64 goes on, unindented, to a line with a ':' in it */
	for (q = line; q < end; q++) {
		const char *n2;
		q = memchr(q, '\n', end - q);
		if (!q) {
			q = end;
			break;
		}
		if (q + 1 >= end || q[1] == ' ' || q[1] == '\t')
			continue;
		if (v21 && q[1] != '\r' && q[1] != '\n') {
			for (n2 = q + 1; n2 < end && *n2 != '\n' && *n2 != ':'; n2++)
				;
			if (n2 >= end || *n2 == '\n')
				continue;	/* no ':' in it: more of the base64 */
		}
		q++;
		break;
	}
	if (q > end)
		q = end;
	*start = line - raw;
	*stop = q - raw;
	return 1;
}

int vcard_photo_empty(const char *s, long n)
{
	const char *c = memchr(s, ':', n);
	if (!c)
		return 1;
	for (c++; c < s + n; c++)
		if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '\n')
			return 0;
	return 1;
}

/* Shifts and exclusive-ors only: a 68000 multiplies 32-bit numbers in
   software, and a photo is tens of thousands of letters. Line breaks
   and the spaces that fold lines don't count, so the same photo folded
   another way keeps its key. */
unsigned long vcard_photo_key(const char *s, long n)
{
	const unsigned char *p = (const unsigned char *)s, *e = p + n;
	unsigned long h = 0x2F6B3C1DUL, sum = 0, cnt = 0;
	while (p < e) {
		unsigned char ch = *p++;
		if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
			continue;
		h = ((h << 5) | (h >> 27)) ^ ch;
		sum += ch;
		cnt++;
	}
	h ^= ((sum & 0xFFFFUL) << 16) ^ (cnt & 0xFFFFUL);
	h &= 0xFFFFFFFFUL;
	if (h <= VC_KEY_UNKNOWN)
		h += 2;
	return h;
}

void vcard_new_uid(char *out, int size)
{
	unsigned char r[16];
	unsigned long t = pf_ms();
	int i;
	pf_entropy(r, 16);
	for (i = 0; i < 16; i++)
		r[i] ^= (unsigned char)(t >> ((i & 3) * 8)) ^ (unsigned char)(i * 37);
	snprintf(out, size, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		 r[0], r[1], r[2], r[3], r[4], r[5], (r[6] & 0x0f) | 0x40, r[7], (r[8] & 0x3f) | 0x80, r[9],
		 r[10], r[11], r[12], r[13], r[14], r[15]);
}

int vcard_write(const CEDIT *e, const char *uid, const char *old, long oldlen, SBUF *out)
{
	static const char *const tel_types[] = { "TYPE=CELL;TYPE=VOICE", "TYPE=IPHONE;TYPE=CELL;TYPE=VOICE",
		"TYPE=HOME;TYPE=VOICE", "TYPE=WORK;TYPE=VOICE", "TYPE=MAIN", "TYPE=HOME;TYPE=FAX",
		"TYPE=WORK;TYPE=FAX", "TYPE=PAGER", "TYPE=OTHER;TYPE=VOICE", "TYPE=VOICE", 0 };
	static const char *const tel_std[] = { "mobile", "iPhone", "home", "work", "main", "home fax", "work fax",
					       "pager", "other", "phone", 0 };
	static const char *const em_types[] = { "TYPE=INTERNET;TYPE=HOME", "TYPE=INTERNET;TYPE=WORK",
						"TYPE=INTERNET;TYPE=OTHER", "TYPE=INTERNET", 0 };
	static const char *const em_std[] = { "home", "work", "other", "email", 0 };
	static const char *const adr_types[] = { "TYPE=HOME", "TYPE=WORK", "TYPE=OTHER", 0 };
	static const char *const adr_std[] = { "home", "work", "other", 0 };
	static const char *const url_types[] = { "TYPE=HOME", "TYPE=WORK", "TYPE=OTHER", 0 };
	static const char *const url_std[] = { "home", "work", "other", 0 };
	VLINES v;
	SBUF line, val;
	unsigned char *keep = 0;
	int i, item = 0, has_uid = 0, any = 0;
	char fn[300], uidbuf[64];
	memset(&v, 0, sizeof(v));
	vcard_nomem = 0;
	if (old && oldlen)
		read_lines(old, old + oldlen, &v, 1);
	keep = vcard_nomem ? 0 : calloc(v.n + 1, 1);	/* a line lost: never write the rest */
	if (!keep) {
		lines_free(&v);
		return 0;
	}
	item = kept_lines(&v, drop_edited, (void *)(long)(uid && *uid), keep);
	sb_init(&line);
	sb_init(&val);
	/* the name the card is shown by */
	fn[0] = 0;
	if (e->company && !empty(e->org)) {
		str_copy(fn, e->org, sizeof(fn));
	} else {
		cat_word(fn, sizeof(fn), empty(e->prefix) ? 0 : e->prefix);
		cat_word(fn, sizeof(fn), empty(e->given) ? 0 : e->given);
		cat_word(fn, sizeof(fn), empty(e->middle) ? 0 : e->middle);
		cat_word(fn, sizeof(fn), empty(e->family) ? 0 : e->family);
		if (!empty(e->suffix)) {
			int n = (int)strlen(fn);
			snprintf(fn + n, sizeof(fn) - n, "%s%s", n ? ", " : "", e->suffix);
		}
		if (!fn[0] && !empty(e->org))
			str_copy(fn, e->org, sizeof(fn));
	}
	for (i = 0; !fn[0] && i < CE_EMAIL; i++)
		if (!empty(e->email[i].value))
			str_copy(fn, e->email[i].value, sizeof(fn));
	for (i = 0; !fn[0] && i < CE_TEL; i++)
		if (!empty(e->tel[i].value))
			str_copy(fn, e->tel[i].value, sizeof(fn));
	if (!fn[0]) {
		free(keep);
		lines_free(&v);
		return 0;
	}
	begin_card(out, &v);
	sb_adds(&line, "N:");
	put_text(&line, e->family, 1);
	sb_addc(&line, ';');
	put_text(&line, e->given, 1);
	sb_addc(&line, ';');
	put_text(&line, e->middle, 1);
	sb_addc(&line, ';');
	put_text(&line, e->prefix, 1);
	sb_addc(&line, ';');
	put_text(&line, e->suffix, 1);
	put_line(out, &line);
	sb_adds(&line, "FN:");
	put_text(&line, fn, 0);
	put_line(out, &line);
	if (!empty(e->nick)) {
		sb_adds(&line, "NICKNAME:");
		put_text(&line, e->nick, 0);
		put_line(out, &line);
	}
	if (!empty(e->org) || !empty(e->dept)) {
		sb_adds(&line, "ORG:");
		put_text(&line, e->org, 1);
		if (!empty(e->dept)) {
			sb_addc(&line, ';');
			put_text(&line, e->dept, 1);
		}
		put_line(out, &line);
	}
	if (!empty(e->title)) {
		sb_adds(&line, "TITLE:");
		put_text(&line, e->title, 0);
		put_line(out, &line);
	}
	for (i = 0; i < CE_EMAIL; i++) {
		if (empty(e->email[i].value))
			continue;
		sb_reset(&val);
		put_text(&val, e->email[i].value, 0);
		put_labelled(out, &line, "EMAIL", e->email[i].label[0] ? e->email[i].label : "other", em_types, em_std,
			     &item, val.s, val.len);
		any = 1;
	}
	for (i = 0; i < CE_TEL; i++) {
		if (empty(e->tel[i].value))
			continue;
		sb_reset(&val);
		put_text(&val, e->tel[i].value, 0);
		put_labelled(out, &line, "TEL", e->tel[i].label[0] ? e->tel[i].label : "mobile", tel_types, tel_std,
			     &item, val.s, val.len);
		any = 1;
	}
	for (i = 0; i < CE_ADR; i++) {
		const CEADR *a = &e->adr[i];
		if (empty(a->street) && empty(a->city) && empty(a->region) && empty(a->zip) && empty(a->country))
			continue;
		sb_reset(&val);
		sb_adds(&val, ";;");
		put_text(&val, a->street, 1);
		sb_addc(&val, ';');
		put_text(&val, a->city, 1);
		sb_addc(&val, ';');
		put_text(&val, a->region, 1);
		sb_addc(&val, ';');
		put_text(&val, a->zip, 1);
		sb_addc(&val, ';');
		put_text(&val, a->country, 1);
		put_labelled(out, &line, "ADR", a->label[0] ? a->label : "home", adr_types, adr_std, &item, val.s, val.len);
	}
	for (i = 0; i < CE_URL; i++) {
		if (empty(e->url[i].value))
			continue;
		sb_reset(&val);
		put_text(&val, e->url[i].value, 0);
		put_labelled(out, &line, "URL", e->url[i].label[0] ? e->url[i].label : "homepage", url_types, url_std,
			     &item, val.s, val.len);
	}
	if (!empty(e->bday)) {
		char d[16];
		if (vcard_bday_parse(e->bday, d, sizeof(d))) {
			if (d[0] == '-')
				/* no year: Apple's way, which iCloud and iPhones show right */
				sb_printf(&line, "BDAY;X-APPLE-OMIT-YEAR=1604;VALUE=date:1604-%s", d + 2);
			else
				sb_printf(&line, "BDAY;VALUE=date:%s", d);
			put_line(out, &line);
		}
	}
	if (!empty(e->note)) {
		sb_adds(&line, "NOTE:");
		put_text(&line, e->note, 0);
		put_line(out, &line);
	}
	if (e->company) {
		sb_adds(&line, "X-ABShowAs:COMPANY");
		put_line(out, &line);
	}
	copy_kept(out, &line, &v, keep, &has_uid);
	if (!has_uid) {
		if (!uid || !*uid) {
			vcard_new_uid(uidbuf, sizeof(uidbuf));
			uid = uidbuf;
		}
		put_uid(out, &line, uid);
	}
	sb_adds(out, "END:VCARD\r\n");
	(void)any;
	sb_free(&line);
	sb_free(&val);
	free(keep);
	lines_free(&v);
	return 1;
}

static int drop_group_lines(const VLINE *l, void *ud)
{
	(void)ud;
	return !strcmp(l->name, "N") || !strcmp(l->name, "FN") || !strcmp(l->name, "X-ADDRESSBOOKSERVER-KIND") ||
	       !strcmp(l->name, "KIND") || !strcmp(l->name, "X-ADDRESSBOOKSERVER-MEMBER") ||
	       !strcmp(l->name, "MEMBER") || !strcmp(l->name, "VERSION") || !strcmp(l->name, "PRODID") ||
	       !strcmp(l->name, "REV");
}

int vcard_write_group(const char *name, const char *uid, const char **members, int n,
		      const char *old, long oldlen, SBUF *out)
{
	VLINES v;
	SBUF line;
	unsigned char *keep;
	int i, has_uid = 0, v4 = 0;
	char uidbuf[64];
	memset(&v, 0, sizeof(v));
	vcard_nomem = 0;
	if (old && oldlen)
		read_lines(old, old + oldlen, &v, 1);
	for (i = 0; i < v.n; i++)
		if (!strcmp(v.l[i].name, "VERSION") && !strncmp(v.l[i].value, "4.0", 3))
			v4 = 1;
	keep = vcard_nomem ? 0 : calloc(v.n + 1, 1);
	if (!keep) {
		lines_free(&v);
		return 0;
	}
	kept_lines(&v, drop_group_lines, 0, keep);
	sb_init(&line);
	begin_card(out, &v);
	sb_adds(&line, "N:");
	put_text(&line, name, 1);
	sb_adds(&line, ";;;;");
	put_line(out, &line);
	sb_adds(&line, "FN:");
	put_text(&line, name, 0);
	put_line(out, &line);
	sb_adds(&line, v4 ? "KIND:group" : "X-ADDRESSBOOKSERVER-KIND:group");
	put_line(out, &line);
	for (i = 0; i < n; i++) {
		sb_printf(&line, "%s:urn:uuid:", v4 ? "MEMBER" : "X-ADDRESSBOOKSERVER-MEMBER");
		sb_adds(&line, members[i]);
		put_line(out, &line);
	}
	copy_kept(out, &line, &v, keep, &has_uid);
	if (!has_uid) {
		if (!uid || !*uid) {
			vcard_new_uid(uidbuf, sizeof(uidbuf));
			uid = uidbuf;
		}
		put_uid(out, &line, uid);
	}
	sb_adds(out, "END:VCARD\r\n");
	sb_free(&line);
	free(keep);
	lines_free(&v);
	return 1;
}

static int drop_cats(const VLINE *l, void *ud)
{
	(void)ud;
	return !strcmp(l->name, "CATEGORIES") || !strcmp(l->name, "VERSION") || !strcmp(l->name, "PRODID") ||
	       !strcmp(l->name, "REV");
}

int vcard_set_categories(const char *old, long oldlen, const char *cats, SBUF *out)
{
	VLINES v;
	SBUF line;
	unsigned char *keep;
	int has_uid = 0;
	memset(&v, 0, sizeof(v));
	vcard_nomem = 0;
	read_lines(old, old + oldlen, &v, 1);
	if (!v.n || vcard_nomem || !(keep = calloc(v.n + 1, 1))) {
		lines_free(&v);
		return 0;
	}
	kept_lines(&v, drop_cats, 0, keep);
	sb_init(&line);
	begin_card(out, &v);
	copy_kept(out, &line, &v, keep, &has_uid);
	if (cats && *cats) {
		/* commas separate the categories: only the other characters are escaped */
		const char *p = cats;
		sb_adds(&line, "CATEGORIES:");
		while (*p) {
			const char *c = strchr(p, ',');
			char buf[100], *one;
			int n = c ? (int)(c - p) : (int)strlen(p);
			if (n > (int)sizeof(buf) - 1)
				n = sizeof(buf) - 1;
			memcpy(buf, p, n);
			buf[n] = 0;
			one = str_trim(buf);
			if (one[0]) {
				if (line.s[line.len - 1] != ':')
					sb_addc(&line, ',');
				put_text(&line, one, 0);
			}
			p = c ? c + 1 : p + strlen(p);
		}
		put_line(out, &line);
	}
	sb_adds(out, "END:VCARD\r\n");
	sb_free(&line);
	free(keep);
	lines_free(&v);
	return 1;
}
