/*
 * photo.c - contacts' pictures (see photo.h): the PHOTO line taken
 * apart, the JPEG decoded with ChaN's TJpgDec at 1/2, 1/4 or 1/8 of its
 * size (the 1/8 needs no IDCT at all: quick on a 68000) and averaged into
 * PHOTO_N x PHOTO_N, and the cache in CACHE\PHOTOS.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "photo.h"
#include "http.h"
#include "plat.h"
#include "util.h"
#include "tjpgd.h"

static char pdir[200];

void photo_dir(const char *dir)
{
	str_copy(pdir, dir, sizeof(pdir));
	pf_mkdir(pdir);
}

static unsigned long fnv(const char *s, long n, unsigned long h)
{
	long i;
	for (i = 0; i < n; i++) {
		h ^= (unsigned char)s[i];
		h *= 16777619UL;
		h &= 0xffffffffUL;
	}
	return h;
}

/* ---------------- the PHOTO line ---------------- */

/* base64 decoded where it lies (the result is shorter than the text);
   spaces and line ends are skipped, so folded lines need no unfolding.
   A table, not comparisons: a photo is tens of thousands of letters */
static long b64_in_place(char *s, long n)
{
	static signed char val[256];
	static int ready;
	unsigned char *o = (unsigned char *)s;
	unsigned long acc = 0;
	int bits = 0;
	long i;
	if (!ready) {
		int k;
		for (k = 0; k < 256; k++)
			val[k] = -1;
		for (k = 0; k < 26; k++) {
			val['A' + k] = (signed char)k;
			val['a' + k] = (signed char)(k + 26);
		}
		for (k = 0; k < 10; k++)
			val['0' + k] = (signed char)(k + 52);
		val['+'] = val['-'] = 62;
		val['/'] = val['_'] = 63;
		ready = 1;
	}
	for (i = 0; i < n; i++) {
		int v = val[(unsigned char)s[i]];
		if (v < 0) {
			if (s[i] == '=')
				break;
			continue;
		}
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			*o++ = (unsigned char)(acc >> bits);
		}
	}
	return (long)(o - (unsigned char *)s);
}

/* a parameter's value from "TYPE=JPEG;X-ABCROP-RECTANGLE=..." */
static int param_of(const char *params, long n, const char *key, char *out, int size)
{
	int kl = (int)strlen(key);
	long i = 0;
	while (i < n) {
		long s = i, e;
		while (i < n && params[i] != ';')
			i++;
		e = i;
		i++;
		if (e - s > kl && !strncasecmp(params + s, key, kl) && params[s + kl] == '=') {
			long vl = e - s - kl - 1;
			const char *v = params + s + kl + 1;
			if (vl && *v == '"') {
				v++;
				vl -= 2;
			}
			if (vl < 0)
				vl = 0;
			if (vl > size - 1)
				vl = size - 1;
			memcpy(out, v, vl);
			out[vl] = 0;
			return 1;
		}
	}
	out[0] = 0;
	return 0;
}

static int fmt_of(const char *type, const unsigned char *d, long n)
{
	if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8)
		return PF_JPEG;
	if (n >= 4 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
		return PF_PNG;
	if (str_istr(type, "png"))
		return PF_PNG;
	if (str_istr(type, "jp"))
		return PF_JPEG;
	return PF_OTHER;
}

int photo_ref(char *raw, long len, PHOTOREF *ref)
{
	char *p = raw, *end = raw + len, *line = 0, *colon, *vend, params[300];
	int v21 = 0;
	memset(ref, 0, sizeof(*ref));
	/* the line: PHOTO or group.PHOTO at the start of a line */
	while (p < end) {
		char *q = p, *nm;
		while (q < end && *q != ':' && *q != ';' && *q != '\n' && *q != '.')
			q++;
		nm = (q < end && *q == '.') ? q + 1 : p;
		if (end - nm > 6 && !strncasecmp(nm, "PHOTO", 5) && (nm[5] == ':' || nm[5] == ';')) {
			line = nm;
			break;
		}
		if (end - nm > 8 && !strncasecmp(nm, "VERSION:2", 9))
			v21 = 1;
		while (p < end && *p != '\n')
			p++;
		p++;
	}
	if (!line)
		return 0;
	/* the parameters (they don't fold in practice) and the value */
	for (colon = line + 5; colon < end && *colon != ':' && *colon != '\n'; colon++)
		;
	if (colon >= end || *colon != ':')
		return 0;
	{
		long pn = colon - (line + 5);
		if (pn > (long)sizeof(params) - 1)
			pn = sizeof(params) - 1;
		memcpy(params, line + 5, pn);
		params[pn] = 0;
	}
	/* the logical line ends at a line that doesn't start with a space
	   (2.1's base64 goes on to a blank line) */
	for (vend = colon + 1; vend < end; vend++) {
		if (*vend != '\n')
			continue;
		if (vend + 1 < end && (vend[1] == ' ' || vend[1] == '\t'))
			continue;
		if (v21 && vend + 1 < end && vend[1] != '\r' && vend[1] != '\n') {
			char *n2 = vend + 1;
			while (n2 < end && *n2 != '\n' && *n2 != ':')
				n2++;
			if (n2 >= end || *n2 == '\n')
				continue;	/* no ':' in it: more of the base64 */
		}
		break;
	}
	{
		char crop[120];
		if (param_of(params, (long)strlen(params), "X-ABCROP-RECTANGLE", crop, sizeof(crop))) {
			/* ABClipRect_1&x&y&w&h&checksum */
			char *s = strchr(crop, '&');
			int k;
			for (k = 0; k < 4 && s; k++) {
				ref->crop[k] = atol(s + 1);
				s = strchr(s + 1, '&');
			}
			ref->has_crop = k == 4 && ref->crop[2] > 0 && ref->crop[3] > 0;
		}
	}
	{
		char type[40], enc[20], val[20];
		char *vs = colon + 1;
		long vn = vend - vs;
		param_of(params, (long)strlen(params), "TYPE", type, sizeof(type));
		param_of(params, (long)strlen(params), "ENCODING", enc, sizeof(enc));
		param_of(params, (long)strlen(params), "VALUE", val, sizeof(val));
		/* 2.1 writes the parameters bare: PHOTO;JPEG;ENCODING=BASE64 */
		if (!type[0] && str_istr(params, "JPEG"))
			str_copy(type, "JPEG", sizeof(type));
		while (vn > 0 && (*vs == ' ' || *vs == '\r'))
			vs++, vn--;
		if (vn > 5 && !strncasecmp(vs, "data:", 5)) {
			/* 4.0: data:image/jpeg;base64,... */
			char *comma = memchr(vs, ',', vn);
			if (!comma)
				return 0;
			{
				long tl = comma - vs;
				str_copy(type, "", sizeof(type));
				if (tl > 5 && tl < (long)sizeof(type) + 5) {
					memcpy(type, vs + 5, tl - 5);
					type[tl - 5] = 0;
				}
			}
			vn -= comma + 1 - vs;
			vs = comma + 1;
			enc[0] = 'b';
		}
		if (enc[0] == 'b' || enc[0] == 'B') {
			ref->len = b64_in_place(vs, vn);
			ref->data = malloc(ref->len + 1);
			if (!ref->data)
				return 0;
			memcpy(ref->data, vs, ref->len);
			ref->kind = PR_DATA;
			ref->fmt = fmt_of(type, ref->data, ref->len);
			return 1;
		}
		/* a link: VALUE=uri, or 4.0's bare http(s) */
		if (vn > 7 && (!strncasecmp(vs, "http://", 7) || !strncasecmp(vs, "https://", 8))) {
			long n = vn;
			while (n && (vs[n - 1] == '\r' || vs[n - 1] == '\n' || vs[n - 1] == ' '))
				n--;
			if (n >= (long)sizeof(ref->uri))
				return 0;
			/* folded: the line breaks and their spaces out */
			{
				long i, o = 0;
				for (i = 0; i < n; i++) {
					if (vs[i] == '\r')
						continue;
					if (vs[i] == '\n') {
						i++;		/* the space that follows */
						continue;
					}
					ref->uri[o++] = vs[i];
				}
				ref->uri[o] = 0;
			}
			ref->kind = PR_URI;
			ref->fmt = str_istr(type, "png") ? PF_PNG : PF_JPEG;
			(void)val;
			return 1;
		}
	}
	return 0;
}

/* ---------------- decoding ---------------- */

#if PHOTO_N != 64
#error out_fn counts on 64 cells a row
#endif
#define POOL 11000		/* TJpgDec's workspace with its Huffman tables */

typedef struct {
	const unsigned char *p;
	long left;
	/* the square kept, in the scaled picture */
	long sx0, sy0, side;
	short *t0, *t1;		/* a row or column of it -> its first and last cell */
	unsigned short *sum;	/* r, g, b, count per cell (6-bit values summed) */
} DEC;

static size_t in_fn(JDEC *jd, uint8_t *buf, size_t n)
{
	DEC *d = jd->device;
	if ((long)n > d->left)
		n = (size_t)d->left;
	if (buf)
		memcpy(buf, d->p, n);
	d->p += n;
	d->left -= (long)n;
	return n;
}

/* each decoded block into the cells it covers: no division per pixel
   (a 68000 divides 32-bit numbers in software, slowly) */
static int out_fn(JDEC *jd, void *bitmap, JRECT *r)
{
	DEC *d = jd->device;
	const unsigned char *px = bitmap;
	long x, y, w = r->right - r->left + 1;
	if (r->right < d->sx0 || r->left >= d->sx0 + d->side || r->bottom < d->sy0 || r->top >= d->sy0 + d->side)
		return 1;		/* a block outside the square */
	for (y = r->top; y <= r->bottom; y++, px += w * 3) {
		long ry = y - d->sy0;
		const unsigned char *q = px;
		short ty;
		if (ry < 0 || ry >= d->side)
			continue;
		for (x = r->left; x <= r->right; x++, q += 3) {
			long rx = x - d->sx0;
			short tx;
			if (rx < 0 || rx >= d->side)
				continue;
			for (ty = d->t0[ry]; ty <= d->t1[ry]; ty++)
				for (tx = d->t0[rx]; tx <= d->t1[rx]; tx++) {
					unsigned short *s = d->sum + ((ty << 6) + tx) * 4;	/* PHOTO_N is 64 */
					s[0] += q[0] >> 2;
					s[1] += q[1] >> 2;
					s[2] += q[2] >> 2;
					s[3]++;
				}
		}
	}
	return 1;
}

int photo_decode_jpeg(const unsigned char *jpg, long len, const PHOTOREF *ref, PHOTOIMG *out)
{
	JDEC jd;
	DEC d;
	void *pool;
	long W, H, x0, y0, x1, y1, side;
	int scale = 0, i, ok = 0;
	memset(&d, 0, sizeof(d));
	d.p = jpg;
	d.left = len;
	pool = malloc(POOL);
	d.sum = calloc(PHOTO_N * PHOTO_N * 4, sizeof(unsigned short));
	if (!pool || !d.sum)
		goto done;
	if (jd_prepare(&jd, in_fn, pool, POOL, &d) != JDR_OK)
		goto done;		/* progressive, or not a JPEG */
	W = jd.width;
	H = jd.height;
	/* the part to show: Apple's crop (its y counts from the bottom), else all */
	x0 = 0;
	y0 = 0;
	x1 = W;
	y1 = H;
	if (ref && ref->has_crop) {
		long cx0 = ref->crop[0], cy0 = H - ref->crop[1] - ref->crop[3];
		long cx1 = cx0 + ref->crop[2], cy1 = cy0 + ref->crop[3];
		if (cx0 < 0)
			cx0 = 0;
		if (cy0 < 0)
			cy0 = 0;
		if (cx1 > W)
			cx1 = W;
		if (cy1 > H)
			cy1 = H;
		if (cx1 - cx0 >= 8 && cy1 - cy0 >= 8) {
			x0 = cx0;
			y0 = cy0;
			x1 = cx1;
			y1 = cy1;
		}
	}
	/* a square in the middle of it */
	side = x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0;
	x0 += (x1 - x0 - side) / 2;
	y0 += (y1 - y0 - side) / 2;
	/* as small as it can be decoded with at least half of PHOTO_N across:
	   1/8 takes only each block's average (no IDCT at all), the quickest
	   by far on a 68000, and a photo from a phone is big enough for it */
	while (scale < 3 && (side >> (scale + 1)) >= PHOTO_N / 2)
		scale++;
	d.sx0 = x0 >> scale;
	d.sy0 = y0 >> scale;
	d.side = side >> scale;
	if (d.side < 1)
		goto done;
	d.t0 = malloc(d.side * 2 * sizeof(short));
	if (!d.t0)
		goto done;
	d.t1 = d.t0 + d.side;
	for (i = 0; i < d.side; i++) {
		d.t0[i] = (short)((long)i * PHOTO_N / d.side);
		d.t1[i] = (short)((((long)i + 1) * PHOTO_N - 1) / d.side);
	}
	if (jd_decomp(&jd, out_fn, (uint8_t)scale) != JDR_OK)
		goto done;
	for (i = 0; i < PHOTO_N * PHOTO_N; i++) {
		unsigned short *s = d.sum + i * 4;
		unsigned r = 0, g = 0, b = 0;
		if (s[3]) {
			r = s[0] / s[3];
			g = s[1] / s[3];
			b = s[2] / s[3];
		}
		/* 6-bit averages to 5-6-5 */
		out->px[i] = (unsigned short)(((r >> 1) << 11) | (g << 5) | (b >> 1));
	}
	ok = 1;
done:
	free(pool);
	free(d.sum);
	free(d.t0);
	return ok;
}

/* ---------------- the cache ---------------- */

typedef struct {
	int valid, failed, link;
	char uid[100];
	short book;
	unsigned long key;
	char cetag[100], ietag[100], ilmod[40];
	long data;		/* where the pixels start */
} PHDR;

static const char *uid_of(const CONTACT *c)
{
	if (c->uid && *c->uid)
		return c->uid;
	if (c->book >= 0 && c->book < nbooks && c->res >= 0 && c->res < books[c->book].nres)
		return books[c->book].res[c->res].href;
	return "";
}

static const char *card_etag(const CONTACT *c)
{
	if (c->book >= 0 && c->book < nbooks && c->res >= 0 && c->res < books[c->book].nres)
		return books[c->book].res[c->res].etag;
	return "";
}

void photo_path(const CONTACT *c, char *out, int size)
{
	char n[20], id[12];
	unsigned long h;
	snprintf(id, sizeof(id), "%d/", c->book >= 0 && c->book < nbooks ? books[c->book].id : 0);
	h = fnv(id, (long)strlen(id), 2166136261UL);
	h = fnv(uid_of(c), (long)strlen(uid_of(c)), h);
	snprintf(n, sizeof(n), "P%07lX.PIC", h & 0xFFFFFFFUL);
	path_join(out, size, pdir, n);
}

static int read_hdr(const char *path, PHDR *h)
{
	char buf[1024], *p, *e;
	int f = pf_open(path, PF_READ);
	long n;
	memset(h, 0, sizeof(*h));
	if (f < 0)
		return 0;
	n = pf_read(f, buf, sizeof(buf) - 1);
	pf_close(f);
	if (n < 12 || strncmp(buf, "ECPHOTO 1\n", 10))
		return 0;
	buf[n] = 0;
	e = strstr(buf, "\n\n");
	if (!e)
		return 0;
	h->data = (long)(e + 2 - buf);
	*e = 0;
	for (p = buf + 10; p && *p;) {
		char *nl = strchr(p, '\n'), *eq = strchr(p, '=');
		if (nl)
			*nl = 0;
		if (eq) {
			*eq++ = 0;
			if (!strcmp(p, "UID"))
				str_copy(h->uid, eq, sizeof(h->uid));
			else if (!strcmp(p, "BOOK"))
				h->book = (short)atoi(eq);
			else if (!strcmp(p, "KEY"))
				h->key = strtoul(eq, 0, 16);
			else if (!strcmp(p, "CETAG"))
				str_copy(h->cetag, eq, sizeof(h->cetag));
			else if (!strcmp(p, "IETAG"))
				str_copy(h->ietag, eq, sizeof(h->ietag));
			else if (!strcmp(p, "ILMOD"))
				str_copy(h->ilmod, eq, sizeof(h->ilmod));
			else if (!strcmp(p, "LINK"))
				h->link = atoi(eq);
			else if (!strcmp(p, "STATE"))
				h->failed = !strcmp(eq, "FAIL");
		}
		p = nl ? nl + 1 : 0;
	}
	h->valid = 1;
	return 1;
}

/* the pixels in the file are big-endian 5-6-5, whatever the machine */
static int load_px(const char *path, const PHDR *h, PHOTOIMG *out)
{
	static unsigned char b[PHOTO_N * PHOTO_N * 2];
	int f = pf_open(path, PF_READ), i;
	long n = 0;
	if (f < 0)
		return 0;
	if (pf_seek(f, h->data) == h->data)
		n = pf_read(f, b, sizeof(b));
	pf_close(f);
	if (n != (long)sizeof(b))
		return 0;
	for (i = 0; i < PHOTO_N * PHOTO_N; i++)
		out->px[i] = (unsigned short)((b[2 * i] << 8) | b[2 * i + 1]);
	return 1;
}

static int write_cache(const char *path, const CONTACT *c, int link, const char *ietag, const char *ilmod,
		       const PHOTOIMG *img)
{
	static unsigned char b[PHOTO_N * PHOTO_N * 2];
	char head[600];
	int f, i, n, ok;
	n = snprintf(head, sizeof(head), "ECPHOTO 1\nUID=%.98s\nBOOK=%d\nKEY=%08lX\nCETAG=%.98s\nIETAG=%.98s\n"
		     "ILMOD=%.38s\nLINK=%d\nSTATE=%s\n\n", uid_of(c), books[c->book].id, c->photo_key, card_etag(c),
		     ietag ? ietag : "", ilmod ? ilmod : "", link, img ? "OK" : "FAIL");
	f = pf_open(path, PF_WRITE);
	if (f < 0)
		return 0;
	ok = pf_write(f, head, n) == n;
	if (ok && img) {
		for (i = 0; i < PHOTO_N * PHOTO_N; i++) {
			b[2 * i] = (unsigned char)(img->px[i] >> 8);
			b[2 * i + 1] = (unsigned char)img->px[i];
		}
		ok = pf_write(f, b, sizeof(b)) == (long)sizeof(b);
	}
	pf_close(f);
	if (!ok)
		pf_remove(path);
	return ok;
}

/* the cache file is this contact's, with this photo */
static int hdr_matches(const PHDR *h, const CONTACT *c)
{
	return h->valid && h->key == c->photo_key && !strcmp(h->uid, uid_of(c)) && h->book == books[c->book].id;
}

int photo_ready(const CONTACT *c)
{
	char path[220];
	PHDR h;
	if (!c->photo_key || !pdir[0] || c->book < 0 || c->book >= nbooks)
		return 0;
	photo_path(c, path, sizeof(path));
	return read_hdr(path, &h) && hdr_matches(&h, c) && !h.failed && (!h.link || !strcmp(h.cetag, card_etag(c)));
}

/* send the account's login only to its own server (iCloud's photos are
   on other iCloud hosts) */
static int same_site(const char *a, const char *b)
{
	URL ua, ub;
	int la, lb;
	if (!url_parse(a, &ua) || !url_parse(b, &ub))
		return 0;
	if (!strcasecmp(ua.host, ub.host))
		return 1;
	la = (int)strlen(ua.host);
	lb = (int)strlen(ub.host);
	return la > 11 && lb > 11 && !strcasecmp(ua.host + la - 11, ".icloud.com") &&
	       !strcasecmp(ub.host + lb - 11, ".icloud.com");
}

/* the picture behind a link; 304 when it hasn't changed. -1: no answer */
static int fetch(const CONTACT *c, const char *uri, const PHDR *old, SBUF *body, HTTPRESP *rs)
{
	HTTPREQ rq;
	char hdr[300], err[120];
	BOOK *b = &books[c->book];
	ACCOUNT *a = &accts[b->acct];
	memset(&rq, 0, sizeof(rq));
	rq.method = "GET";
	rq.url = uri;
	if (a->user[0] && same_site(uri, b->url[0] ? b->url : a->url)) {
		rq.user = a->user;
		rq.pass = a->pass;
	}
	snprintf(hdr, sizeof(hdr), "Accept: image/jpeg, image/*\r\n");
	if (old && old->ietag[0])
		snprintf(hdr + strlen(hdr), sizeof(hdr) - strlen(hdr), "If-None-Match: %s\r\n", old->ietag);
	else if (old && old->ilmod[0])
		snprintf(hdr + strlen(hdr), sizeof(hdr) - strlen(hdr), "If-Modified-Since: %s\r\n", old->ilmod);
	rq.headers = hdr;
	rq.maxbody = 1500000L;
	if (!http_request(&rq, rs, body, err, sizeof(err)))
		return -1;
	return rs->status;
}

int photo_get(const CONTACT *c, PHOTOIMG *out, int may_fetch, int *how)
{
	char path[220], *raw;
	PHDR h;
	PHOTOREF ref;
	long len;
	int have, r = PH_FAILED;
	if (how)
		*how = PHH_CACHE;
	if (!pdir[0] || c->book < 0 || c->book >= nbooks)
		return PH_NONE;
	photo_path(c, path, sizeof(path));
	have = read_hdr(path, &h);
	if (!c->photo_key) {
		/* no photo (any more): its picture goes */
		if (have)
			pf_remove(path);
		return PH_NONE;
	}
	if (have && hdr_matches(&h, c)) {
		int changed = h.link && strcmp(h.cetag, card_etag(c));
		if (!changed || !may_fetch) {
			if (h.failed)
				return PH_FAILED;
			return load_px(path, &h, out) ? PH_OK : PH_FAILED;
		}
		/* the card changed and the photo is a link: has the picture? */
	} else if (have) {
		h.valid = 0;		/* another photo: start again */
	}
	raw = contact_raw(c, &len);
	if (!raw)
		return PH_FAILED;
	if (!photo_ref(raw, len, &ref)) {
		free(raw);
		write_cache(path, c, 0, 0, 0, 0);
		return PH_FAILED;
	}
	free(raw);
	if (ref.kind == PR_DATA) {
		if (ref.fmt == PF_JPEG && photo_decode_jpeg(ref.data, ref.len, &ref, out)) {
			write_cache(path, c, 0, 0, 0, out);
			r = PH_OK;
		} else {
			write_cache(path, c, 0, 0, 0, 0);	/* not again until it changes */
		}
		if (how)
			*how = PHH_DECODED;
		free(ref.data);
		return r;
	}
	/* a link */
	if (!may_fetch)
		return PH_NEEDS_NET;
	{
		SBUF body;
		HTTPRESP rs;
		int st;
		const PHDR *old = h.valid && !h.failed ? &h : 0;
		sb_init(&body);
		st = fetch(c, ref.uri, old, &body, &rs);
		if (st == 304 && old && load_px(path, &h, out)) {
			/* the same picture: the card's new ETag noted */
			write_cache(path, c, 1, h.ietag, h.ilmod, out);
			if (how)
				*how = PHH_CHECKED;
			r = PH_OK;
		} else if (st == 200) {
			if (fmt_of(rs.ctype, (unsigned char *)body.s, body.len) == PF_JPEG &&
			    photo_decode_jpeg((unsigned char *)body.s, body.len, &ref, out)) {
				write_cache(path, c, 1, rs.etag, rs.lastmod, out);
				r = PH_OK;
			} else {
				write_cache(path, c, 1, rs.etag, rs.lastmod, 0);
			}
			if (how)
				*how = PHH_FETCHED;
		} else if (st > 0) {
			/* refused, gone: not asked again until the card changes */
			write_cache(path, c, 1, 0, 0, 0);
		} else if (old && load_px(path, &h, out)) {
			r = PH_OK;		/* no answer: the picture we have */
		} else {
			r = PH_NEEDS_NET;
		}
		sb_free(&body);
	}
	return r;
}

/* ---------------- tidying ---------------- */

/* the names first, then the removing: TOS doesn't like a folder changed
   while it is being listed */
typedef struct {
	char (*names)[14];
	int n, max;
} PRUNE;

static int list_one(const char *name, long size, void *ud)
{
	PRUNE *p = ud;
	(void)size;
	if (strlen(name) < 5 || strlen(name) > 12 || strcasecmp(name + strlen(name) - 4, ".PIC"))
		return 0;
	if (p->n >= p->max) {
		int max = p->max ? p->max * 2 : 64;
		char (*nn)[14] = realloc(p->names, max * sizeof(*nn));
		if (!nn)
			return 1;
		p->names = nn;
		p->max = max;
	}
	str_copy(p->names[p->n++], name, 14);
	return 0;
}

int photo_prune(void)
{
	PRUNE p;
	int k, removed = 0;
	if (!pdir[0])
		return 0;
	memset(&p, 0, sizeof(p));
	pf_list(pdir, list_one, &p);
	for (k = 0; k < p.n; k++) {
		char path[220];
		PHDR h;
		int i, keep = 0;
		path_join(path, sizeof(path), pdir, p.names[k]);
		if (read_hdr(path, &h))
			for (i = 0; i < npeople && !keep; i++) {
				const CONTACT *c = people[i];
				if (c->book >= 0 && c->book < nbooks && books[c->book].id == h.book &&
				    c->photo_key == h.key && !strcmp(uid_of(c), h.uid))
					keep = 1;
			}
		if (!keep && pf_remove(path) == 0)
			removed++;
	}
	free(p.names);
	return removed;
}
