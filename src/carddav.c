/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * carddav.c - CardDAV requests and their multistatus answers (after
 * ECal's caldav.c).
 *
 * Finding the address books: PROPFIND /.well-known/carddav for the
 * current-user-principal, the principal for its addressbook-home-set,
 * and the home, one level deep, for the collections whose resourcetype
 * says addressbook. iCloud answers at contacts.icloud.com and sends each
 * account on to its own server (p42-contacts.icloud.com); its one
 * address book is called "card".
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "carddav.h"
#include "http.h"
#include "xml.h"
#include "charset.h"
#include "conn.h"

#define NS "xmlns:d=\"DAV:\" xmlns:card=\"urn:ietf:params:xml:ns:carddav\" " \
	   "xmlns:cs=\"http://calendarserver.org/ns/\""

const char *carddav_status_text(int st)
{
	switch (st) {
	case 400: return "the server didn't understand the request";
	case 401: return "the server refused the user name or password (iCloud wants an app-specific password)";
	case 403: return "this address book can't be changed (read-only?)";
	case 404: return "not found on the server";
	case 405: return "the server doesn't allow this here";
	case 409: return "the server refused: a conflict";
	case 412: return "it was changed elsewhere in the meantime; refresh and try again";
	case 413: return "too big for the server";
	case 415: return "the server doesn't take this kind of data";
	case 500: return "the server had an internal error";
	case 502: case 504: return "the gateway couldn't reach the server";
	case 503: return "the server is busy; try again later";
	}
	return st >= 500 ? "server error" : st >= 400 ? "request refused" : "unexpected answer";
}

/* ---------------- multistatus ---------------- */

typedef struct {
	char href[400];
	char name[128];			/* UTF-8 */
	char ctag[100];
	char etag[100];
	char ctype[64];
	char principal[400];
	char home[400];
	int is_book, is_coll, priv_seen, writable;
	SBUF data;
} DAVRESP;

typedef void (*RESP_FN)(DAVRESP *r, void *ud);

static void trim_copy(char *d, const char *s, long n, int size)
{
	while (n && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t'))
		s++, n--;
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t'))
		n--;
	if (n > size - 1)
		n = size - 1;
	memcpy(d, s, n);
	d[n] = 0;
}

static void parse_multistatus(const char *xml, long n, RESP_FN fn, void *ud)
{
	XMLP x;
	char stack[16][48];
	int depth = 0, t, in_resp = 0;
	DAVRESP r;
	memset(&r, 0, sizeof(r));
	sb_init(&r.data);
	xml_init(&x, xml, n);
	while ((t = xml_next(&x)) != X_EOF) {
		const char *parent = depth > 0 ? stack[depth - 1] : "";
		const char *gp = depth > 1 ? stack[depth - 2] : "";
		if (t == X_START || t == X_EMPTY) {
			if (!strcmp(x.name, "response")) {
				in_resp = 1;
				sb_free(&r.data);
				memset(&r, 0, sizeof(r));
				sb_init(&r.data);
			}
			if (in_resp) {
				if (!strcmp(parent, "resourcetype")) {
					if (!strcmp(x.name, "addressbook"))
						r.is_book = 1;
					else if (!strcmp(x.name, "collection"))
						r.is_coll = 1;
				} else if (!strcmp(parent, "privilege")) {
					r.priv_seen = 1;
					if (!strcmp(x.name, "write") || !strcmp(x.name, "write-content") || !strcmp(x.name, "all"))
						r.writable = 1;
				}
			}
			if (t == X_START && depth < 16)
				str_copy(stack[depth++], x.name, sizeof(stack[0]));
			continue;
		}
		if (t == X_END) {
			if (depth > 0)
				depth--;
			if (!strcmp(x.name, "response") && in_resp) {
				in_resp = 0;
				fn(&r, ud);
			}
			continue;
		}
		/* text */
		if (!in_resp)
			continue;
		if (!strcmp(parent, "href")) {
			if (!strcmp(gp, "response"))
				trim_copy(r.href, x.text, x.textlen, sizeof(r.href));
			else if (!strcmp(gp, "current-user-principal"))
				trim_copy(r.principal, x.text, x.textlen, sizeof(r.principal));
			else if (!strcmp(gp, "addressbook-home-set"))
				trim_copy(r.home, x.text, x.textlen, sizeof(r.home));
		} else if (!strcmp(parent, "displayname")) {
			trim_copy(r.name, x.text, x.textlen, sizeof(r.name));
		} else if (!strcmp(parent, "getctag") || !strcmp(parent, "sync-token")) {
			if (!r.ctag[0] || !strcmp(parent, "getctag"))
				trim_copy(r.ctag, x.text, x.textlen, sizeof(r.ctag));
		} else if (!strcmp(parent, "getetag")) {
			trim_copy(r.etag, x.text, x.textlen, sizeof(r.etag));
		} else if (!strcmp(parent, "getcontenttype")) {
			trim_copy(r.ctype, x.text, x.textlen, sizeof(r.ctype));
		} else if (!strcmp(parent, "address-data")) {
			sb_add(&r.data, x.text, x.textlen);
		}
	}
	xml_done(&x);
	sb_free(&r.data);
}

long carddav_budget = 400000L;
int carddav_skipped;
static long dav_max = 1500000L;

static int dav(const char *method, const char *url, const DAVAUTH *a, int depth, const char *body,
	       HTTPRESP *rs, SBUF *out, char *err, int errlen)
{
	HTTPREQ rq;
	char hdr[40];
	memset(&rq, 0, sizeof(rq));
	rq.method = method;
	rq.url = url;
	rq.user = a ? a->user : 0;
	rq.pass = a ? a->pass : 0;
	if (body) {
		rq.body = body;
		rq.bodylen = (long)strlen(body);
		rq.ctype = "application/xml; charset=utf-8";
	}
	if (depth >= 0) {
		snprintf(hdr, sizeof(hdr), "Depth: %d\r\n", depth);
		rq.headers = hdr;
	}
	rq.maxbody = dav_max;
	if (!http_request(&rq, rs, out, err, errlen))
		return 0;
	if (rs->status != 207 && rs->status != 200) {
		snprintf(err, errlen, "%s (HTTP %d)", carddav_status_text(rs->status), rs->status);
		return 0;
	}
	return 1;
}

/* ---------------- discovery ---------------- */

typedef struct {
	char principal[400], home[400];
	int self_book;
} PRINC;

static void got_principal(DAVRESP *r, void *ud)
{
	PRINC *p = ud;
	if (r->principal[0] && !p->principal[0])
		str_copy(p->principal, r->principal, sizeof(p->principal));
	if (r->home[0] && !p->home[0])
		str_copy(p->home, r->home, sizeof(p->home));
	if (r->is_book)
		p->self_book = 1;
}

typedef struct {
	DAVBOOK *books;
	int n, max;
	const char *base;
} BOOKLIST;

static void name_to_atari(char *d, const char *utf8, int size)
{
	long n;
	char *a = cs_to_atari(utf8, (long)strlen(utf8), CS_UTF8, &n);
	str_copy(d, a ? a : utf8, size);
	free(a);
}

static void got_book(DAVRESP *r, void *ud)
{
	BOOKLIST *l = ud;
	DAVBOOK *b;
	if (!r->is_book)
		return;
	if (l->n >= l->max) {
		DAVBOOK *nb;
		l->max = l->max ? l->max * 2 : 4;
		nb = realloc(l->books, l->max * sizeof(DAVBOOK));
		if (!nb)
			return;
		l->books = nb;
	}
	b = &l->books[l->n++];
	memset(b, 0, sizeof(*b));
	url_resolve(l->base, r->href, b->url, sizeof(b->url));
	/* iCloud's one book is called "card": show it as Contacts */
	name_to_atari(b->name, r->name[0] && strcmp(r->name, "card") ? r->name : "Contacts", sizeof(b->name));
	str_copy(b->ctag, r->ctag, sizeof(b->ctag));
	b->readonly = r->priv_seen && !r->writable;
}

int carddav_discover(const char *base, const DAVAUTH *a, DAVBOOK **books, int *nbooks,
		     char *err, int errlen)
{
	static const char q_princ[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><d:current-user-principal/><card:addressbook-home-set/>"
		"<d:resourcetype/><d:displayname/><cs:getctag/>"
		"</d:prop></d:propfind>";
	static const char q_home[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><d:resourcetype/><d:displayname/><cs:getctag/>"
		"<d:current-user-privilege-set/></d:prop></d:propfind>";
	HTTPRESP rs;
	SBUF out;
	PRINC p;
	BOOKLIST l;
	URL u;
	char url[512], tried[512];
	int ok = 0;
	*books = 0;
	*nbooks = 0;
	if (!url_parse(base, &u)) {
		snprintf(err, errlen, "not a web address: %.60s", base);
		return 0;
	}
	sb_init(&out);
	memset(&p, 0, sizeof(p));
	memset(&l, 0, sizeof(l));
	/* 1. the principal: at the address given, or via .well-known */
	url_format(&u, url, sizeof(url));
	if (!strcmp(u.path, "/")) {
		char wk[512];
		url_resolve(url, "/.well-known/carddav", wk, sizeof(wk));
		if (dav("PROPFIND", wk, a, 0, q_princ, &rs, &out, err, errlen)) {
			str_copy(url, rs.final_url, sizeof(url));
			parse_multistatus(out.s, out.len, got_principal, &p);
		}
	}
	if (!p.principal[0] && !p.home[0] && !p.self_book) {
		if (!dav("PROPFIND", url, a, 0, q_princ, &rs, &out, err, errlen))
			goto done;
		str_copy(url, rs.final_url, sizeof(url));
		parse_multistatus(out.s, out.len, got_principal, &p);
	}
	if (p.self_book && !p.home[0]) {
		/* the address is an address book itself */
		l.base = url;
		if (!dav("PROPFIND", url, a, 0, q_home, &rs, &out, err, errlen))
			goto done;
		parse_multistatus(out.s, out.len, got_book, &l);
		if (l.n)
			str_copy(l.books[0].url, url, sizeof(l.books[0].url));
		ok = 1;
		goto have;
	}
	/* 2. the address book home, from the principal */
	if (!p.home[0] && p.principal[0]) {
		url_resolve(url, p.principal, tried, sizeof(tried));
		if (!dav("PROPFIND", tried, a, 0, q_princ, &rs, &out, err, errlen))
			goto done;
		str_copy(url, rs.final_url, sizeof(url));
		parse_multistatus(out.s, out.len, got_principal, &p);
	}
	if (!p.home[0]) {
		snprintf(err, errlen, "the server didn't say where the contacts are (no addressbook-home-set)");
		goto done;
	}
	/* 3. the address books in it */
	url_resolve(url, p.home, tried, sizeof(tried));
	if (!dav("PROPFIND", tried, a, 1, q_home, &rs, &out, err, errlen))
		goto done;
	l.base = rs.final_url;
	parse_multistatus(out.s, out.len, got_book, &l);
	ok = 1;
have:
	*books = l.books;
	*nbooks = l.n;
	if (!l.n) {
		snprintf(err, errlen, "no address books found in this account");
		ok = 0;
	}
done:
	if (!ok && l.books && !*books)
		free(l.books);
	sb_free(&out);
	return ok;
}

static void got_ctag(DAVRESP *r, void *ud)
{
	char *c = ud;
	if (r->ctag[0] && !c[0])
		str_copy(c, r->ctag, 100);
}

int carddav_ctag(const char *url, const DAVAUTH *a, char *ctag, int size, char *err, int errlen)
{
	static const char q[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><cs:getctag/><d:sync-token/></d:prop></d:propfind>";
	HTTPRESP rs;
	SBUF out;
	char c[100];
	int ok;
	sb_init(&out);
	c[0] = 0;
	ok = dav("PROPFIND", url, a, 0, q, &rs, &out, err, errlen);
	if (ok)
		parse_multistatus(out.s, out.len, got_ctag, c);
	str_copy(ctag, c, size);
	sb_free(&out);
	return ok;
}

/* ---------------- contacts ---------------- */

typedef struct {
	DAV_ITEM_CB cb;
	void *ud;
	int stop;
	SBUF pend;		/* streaming: what came since the last whole <response> */
} LISTING;

static void got_item(DAVRESP *r, void *ud)
{
	LISTING *l = ud;
	if (l->stop || !r->etag[0] || !r->href[0] || r->is_coll || r->is_book)
		return;			/* the collection itself */
	if (r->ctype[0] && !str_istr(r->ctype, "vcard") && !str_istr(r->ctype, "directory"))
		return;			/* something else kept in the book */
	if (!l->cb(l->ud, r->href, r->etag))
		l->stop = 1;
}

/* the end of the last whole </response> (any prefix) in s, 0 if none */
static long last_response_end(const char *s, long n)
{
	long i;
	for (i = n - 9; i >= 2; i--) {
		long k;
		if (s[i] != 'r' || memcmp(s + i, "response>", 9))
			continue;
		k = i;
		if (s[k - 1] == ':')
			for (k--; k > 0 && s[k - 1] != '/' && s[k - 1] != '<'; k--)
				;
		if (k >= 2 && s[k - 1] == '/' && s[k - 2] == '<')
			return i + 9;
	}
	return 0;
}

/* the answer as it arrives: each whole <response> read at once, the
   rest kept for the next piece. Memory: one piece, not the answer */
static int list_sink(void *ud, const char *d, long n)
{
	LISTING *l = ud;
	long end;
	if (!sb_add(&l->pend, d, n))
		return 0;
	end = last_response_end(l->pend.s, l->pend.len);
	if (end > 0) {
		parse_multistatus(l->pend.s, end, got_item, l);
		memmove(l->pend.s, l->pend.s + end, l->pend.len - end);
		l->pend.len -= end;
		l->pend.s[l->pend.len] = 0;
	}
	return !l->stop && l->pend.len < 65536L;
}

#define LIST_STREAM_BELOW 200000L	/* carddav_budget under which the list is streamed */

int carddav_list(const char *url, const DAVAUTH *a, DAV_ITEM_CB cb, void *ud, char *err, int errlen)
{
	static const char q[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><d:getetag/><d:getcontenttype/><d:resourcetype/></d:prop></d:propfind>";
	HTTPRESP rs;
	SBUF out;
	LISTING l;
	int ok;
	memset(&l, 0, sizeof(l));
	l.cb = cb;
	l.ud = ud;
	sb_init(&out);
	if (carddav_budget >= LIST_STREAM_BELOW) {
		/* memory enough: all of it at once, compressed on the way */
		ok = dav("PROPFIND", url, a, 1, q, &rs, &out, err, errlen);
		if (ok)
			parse_multistatus(out.s, out.len, got_item, &l);
	} else {
		HTTPREQ rq;
		char hdr[40];
		memset(&rq, 0, sizeof(rq));
		rq.method = "PROPFIND";
		rq.url = url;
		rq.user = a ? a->user : 0;
		rq.pass = a ? a->pass : 0;
		rq.body = q;
		rq.bodylen = (long)strlen(q);
		rq.ctype = "application/xml; charset=utf-8";
		snprintf(hdr, sizeof(hdr), "Depth: 1\r\n");
		rq.headers = hdr;
		rq.sink = list_sink;
		rq.ud = &l;
		sb_init(&l.pend);
		ok = http_request(&rq, &rs, &out, err, errlen);
		if (ok && rs.status != 207 && rs.status != 200) {
			snprintf(err, errlen, "%s (HTTP %d)", carddav_status_text(rs.status), rs.status);
			ok = 0;
		}
		if (ok && l.pend.len && !l.stop)
			parse_multistatus(l.pend.s, l.pend.len, got_item, &l);
		if (!ok && (l.stop || l.pend.len >= 65536L))
			str_copy(err, "not enough memory for the list of contacts", errlen);
		sb_free(&l.pend);
	}
	sb_free(&out);
	return ok && !l.stop;
}

typedef struct {
	DAV_CB cb;
	void *ud;
	int stop, got, asked_part;
} MGET;

/* What EContact shows, asked for alone (RFC 6352 8.4, partial
   retrieval): the PHOTO says that there is one but not what. A card
   with any other line came whole: the server ignored the request. */
static const char *const shown_props[] = {
	"VERSION", "UID", "FN", "N", "NICKNAME", "ORG", "TITLE", "NOTE", "CATEGORIES", "BDAY", "EMAIL", "TEL",
	"ADR", "URL", "X-ABLABEL", "X-ABSHOWAS", "X-ADDRESSBOOKSERVER-KIND", "KIND", "X-ADDRESSBOOKSERVER-MEMBER",
	"MEMBER", 0
};

static int only_shown(const char *d, long n)
{
	const char *p = d, *e = d + n;
	while (p < e) {
		const char *q = p, *nm = p;
		int k, len;
		while (q < e && *q != ':' && *q != ';' && *q != '\n' && *q != '.')
			q++;
		if (q < e && *q == '.') {
			nm = q + 1;
			for (q = nm; q < e && *q != ':' && *q != ';' && *q != '\n'; q++)
				;
		}
		len = (int)(q - nm);
		if (q < e && *q != '\n' && *p != ' ' && *p != '\t' && len > 0) {
			int known = (len == 5 && !strncasecmp(nm, "BEGIN", 5)) || (len == 3 && !strncasecmp(nm, "END", 3)) ||
				    (len == 5 && !strncasecmp(nm, "PHOTO", 5));
			for (k = 0; !known && shown_props[k]; k++)
				known = (int)strlen(shown_props[k]) == len && !strncasecmp(nm, shown_props[k], len);
			if (!known)
				return 0;
		}
		q = memchr(p, '\n', e - p);
		if (!q)
			break;
		p = q + 1;
	}
	return 1;
}

static void got_data(DAVRESP *r, void *ud)
{
	MGET *m = ud;
	if (m->stop || !r->data.len)
		return;
	m->got++;
	if (!m->cb(m->ud, r->href, r->etag, r->data.s, r->data.len, m->asked_part && only_shown(r->data.s, r->data.len)))
		m->stop = 1;
}

static void mget_query(SBUF *q, char **hrefs, int n, int partial)
{
	int k;
	sb_adds(q, "<?xml version=\"1.0\" encoding=\"utf-8\"?><card:addressbook-multiget " NS "><d:prop><d:getetag/>");
	if (partial) {
		sb_adds(q, "<card:address-data>");
		for (k = 0; shown_props[k]; k++)
			sb_printf(q, "<card:prop name=\"%s\"/>", shown_props[k]);
		sb_adds(q, "<card:prop name=\"PHOTO\" novalue=\"yes\"/></card:address-data>");
	} else {
		sb_adds(q, "<card:address-data/>");
	}
	sb_adds(q, "</d:prop>");
	for (k = 0; k < n; k++) {
		char esc[600];
		xml_escape(hrefs[k], esc, sizeof(esc));
		sb_printf(q, "<d:href>%s</d:href>", esc);
	}
	sb_adds(q, "</card:addressbook-multiget>");
}

/* As many at a time as fit in carddav_budget: the first answer says how
   big the cards are, and the next asks for as many as fit. An answer too
   big for memory is asked for again in halves; one card too big for it
   alone comes without its photo. A server that refuses partial
   retrieval gets the plain request. */
int carddav_multiget(const char *url, const DAVAUTH *a, char **hrefs, int n, int partial,
		     DAV_CB cb, void *ud, char *err, int errlen)
{
	int i = 0, ok = 1, per, one_part = 0;
	SBUF q, out;
	long saved = dav_max;
	carddav_skipped = 0;
	per = partial ? 40 : (int)(carddav_budget / 40000L);
	if (per < 1)
		per = 1;
	if (per > 40)
		per = 40;
	dav_max = carddav_budget * 3;
	if (dav_max < 65536L)
		dav_max = 65536L;
	sb_init(&out);
	while (i < n) {
		HTTPRESP rs;
		MGET m;
		int cnt = n - i < per ? n - i : per, part = partial || one_part;
		memset(&rs, 0, sizeof(rs));
		sb_init(&q);
		mget_query(&q, hrefs + i, cnt, part);
		ok = q.s && dav("REPORT", url, a, 1, q.s, &rs, &out, err, errlen);
		sb_free(&q);
		if (!ok) {
			if (rs.toobig && cnt > 1) {
				per = cnt / 2;		/* smaller, and again */
				continue;
			}
			if (one_part || (rs.toobig && part)) {
				/* too big even without its photo, or the server won't
				   send it so: left out, asked for again next time */
				one_part = 0;
				i += cnt;
				carddav_skipped += cnt;
				ok = 1;
				continue;
			}
			if (rs.toobig) {
				one_part = 1;		/* this one without its photo */
				continue;
			}
			if (part && rs.status >= 400 && rs.status != 401 && rs.status != 404) {
				partial = 0;		/* the server won't: the whole cards */
				per = (int)(carddav_budget / 40000L);
				if (per < 1)
					per = 1;
				if (per > 40)
					per = 40;
				continue;
			}
			break;
		}
		memset(&m, 0, sizeof(m));
		m.cb = cb;
		m.ud = ud;
		m.asked_part = part;
		parse_multistatus(out.s, out.len, got_data, &m);
		if (m.stop)
			break;
		if (m.got && !one_part) {
			long each = out.len / m.got + 1;
			per = (int)(carddav_budget / each);
			if (per < 1)
				per = 1;
			if (per > 40)
				per = 40;
		}
		one_part = 0;
		i += cnt;
	}
	sb_free(&out);
	dav_max = saved;
	return ok;
}

int carddav_put(const char *url, const char *href, const DAVAUTH *a, const char *data, long len,
		const char *etag, char *newetag, int size, char *err, int errlen)
{
	HTTPREQ rq;
	HTTPRESP rs;
	SBUF out;
	char target[512], hdr[160];
	int ok;
	url_resolve(url, href, target, sizeof(target));
	memset(&rq, 0, sizeof(rq));
	rq.method = "PUT";
	rq.url = target;
	rq.user = a->user;
	rq.pass = a->pass;
	rq.body = data;
	rq.bodylen = len;
	rq.ctype = "text/vcard; charset=utf-8";
	if (etag && *etag)
		snprintf(hdr, sizeof(hdr), "If-Match: %s\r\n", etag);
	else
		snprintf(hdr, sizeof(hdr), "If-None-Match: *\r\n");
	rq.headers = hdr;
	sb_init(&out);
	ok = http_request(&rq, &rs, &out, err, errlen);
	sb_free(&out);
	if (!ok)
		return 0;
	if (rs.status != 200 && rs.status != 201 && rs.status != 204) {
		snprintf(err, errlen, "%s (HTTP %d)", carddav_status_text(rs.status), rs.status);
		return 0;
	}
	str_copy(newetag, rs.etag, size);
	return 1;
}

int carddav_get(const char *url, const char *href, const DAVAUTH *a, SBUF *out, char *etag, int size,
		char *err, int errlen)
{
	HTTPREQ rq;
	HTTPRESP rs;
	char target[512];
	url_resolve(url, href, target, sizeof(target));
	memset(&rq, 0, sizeof(rq));
	rq.method = "GET";
	rq.url = target;
	rq.user = a->user;
	rq.pass = a->pass;
	rq.headers = "Accept: text/vcard\r\n";
	if (!http_request(&rq, &rs, out, err, errlen))
		return 0;
	if (rs.status != 200) {
		snprintf(err, errlen, "%s (HTTP %d)", carddav_status_text(rs.status), rs.status);
		return 0;
	}
	str_copy(etag, rs.etag, size);
	return 1;
}

int carddav_delete(const char *url, const char *href, const DAVAUTH *a, const char *etag,
		   char *err, int errlen)
{
	HTTPREQ rq;
	HTTPRESP rs;
	SBUF out;
	char target[512], hdr[160];
	int ok;
	url_resolve(url, href, target, sizeof(target));
	memset(&rq, 0, sizeof(rq));
	rq.method = "DELETE";
	rq.url = target;
	rq.user = a->user;
	rq.pass = a->pass;
	if (etag && *etag) {
		snprintf(hdr, sizeof(hdr), "If-Match: %s\r\n", etag);
		rq.headers = hdr;
	}
	sb_init(&out);
	ok = http_request(&rq, &rs, &out, err, errlen);
	sb_free(&out);
	if (!ok)
		return 0;
	if (rs.status == 404 || rs.status == 410)
		return 1;		/* gone already */
	if (rs.status != 200 && rs.status != 204) {
		snprintf(err, errlen, "%s (HTTP %d)", carddav_status_text(rs.status), rs.status);
		return 0;
	}
	return 1;
}
