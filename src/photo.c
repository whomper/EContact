/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * photo.c - contacts' pictures (see photo.h): the PHOTO line taken
 * apart, the JPEG decoded with ChaN's TJpgDec at 1/2, 1/4 or 1/8 of its
 * size (the 1/8 needs no IDCT at all: quick on a 68000) and averaged into
 * PHOTO_N x PHOTO_N, and the cache in CACHE\PHOTOS.PAK (photopack.c).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "photo.h"
#include "photopack.h"
#include "http.h"
#include "plat.h"
#include "util.h"
#include "tjpgd.h"
#include "jpegdc.h"
#include "conn.h"

static char pdir[200];		/* the cache's folder: "" until set */

void photo_dir(const char *dir)
{
	char pack[220];
	str_copy(pdir, dir, sizeof(pdir));
	path_join(pack, sizeof(pack), pdir, "PHOTOS.PAK");
	pack_path(pack);
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
	/* the logical line ends at a line that doesn't start with a space
	   (2.1's base64 goes on to a blank line) */
	for (vend = line; vend < end; vend++) {
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
	/* Unfolded where it lies: a line is folded every 75 bytes, wherever
	   that falls, and iCloud's long crop parameter puts the fold in the
	   middle of the parameters or of "data:image/jpeg" (2.1's base64
	   lines, which don't start with a space, stay: they are skipped) */
	{
		char *r = line, *w = line;
		while (r < vend) {
			if (r[0] == '\r' && r + 2 < end && r[1] == '\n' && (r[2] == ' ' || r[2] == '\t')) {
				r += 3;
				continue;
			}
			if (r[0] == '\n' && r + 1 < end && (r[1] == ' ' || r[1] == '\t')) {
				r += 2;
				continue;
			}
			*w++ = *r++;
		}
		vend = w;
	}
	str_copy(ref->head, line, vend - line + 1 < (long)sizeof(ref->head) ? (int)(vend - line + 1) : (int)sizeof(ref->head));
	/* the parameters and the value */
	for (colon = line + 5; colon < vend && *colon != ':'; colon++)
		if (*colon == '"')
			for (colon++; colon < vend && *colon != '"'; colon++)
				;	/* a quoted value may have a ':' */
	if (colon >= vend || *colon != ':')
		return 0;
	{
		long pn = colon - (line + 5);
		if (pn > (long)sizeof(params) - 1)
			pn = sizeof(params) - 1;
		memcpy(params, line + 5, pn);
		params[pn] = 0;
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
		/* 2.1's bare BASE64, or no ENCODING at all with base64 that is
		   plainly a JPEG or a PNG */
		if (!enc[0]) {
			const char *b = str_istr(params, ";BASE64");
			if ((b && (b[7] == ';' || !b[7])) ||
			    (vn > 8 && (!strncmp(vs, "/9j/", 4) || !strncmp(vs, "iVBORw0K", 8))))
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
	short polls;		/* blocks since pf_idle was last called */
} DEC;

/* a decode takes seconds on an Atari (half a minute for a big picture):
   the progress box looks at Cancel as it goes (conn_tick, every 50 ms),
   or Esc is looked for five times a second (pf_idle, the busy bee), and
   a stop (conn_stop) ends the decode. 0: stop */
static int decode_poll(void)
{
	static unsigned long last;
	if (conn_tick) {
		conn_tick();
	} else if (pf_idle) {
		unsigned long now = pf_ms();
		if (now - last >= 200) {
			last = now;
			pf_idle();
		}
	}
	return !conn_stop;
}

/* which decoder made the last picture, for the log */
static const char *dec_how = "";

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
	if (++d->polls >= 8) {
		d->polls = 0;
		if (!decode_poll())
			return 0;	/* stopped: JDR_INTR */
	}
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

int photo_why;
char photo_why_detail[100];

static void why(int w, const char *detail)
{
	photo_why = w;
	str_copy(photo_why_detail, detail ? detail : "", sizeof(photo_why_detail));
}

const char *photo_why_text(int w)
{
	switch (w) {
	case PW_OK: return "made";
	case PW_PNG: return "a PNG picture";
	case PW_HEIC: return "an HEIC picture";
	case PW_GIF: return "a GIF picture";
	case PW_WEBP: return "a WebP picture";
	case PW_OTHER: return "not a picture EContact knows";
	case PW_JPEG_KIND: return "a kind of JPEG EContact can't read";
	case PW_DAMAGED: return "damaged";
	case PW_MEMORY: return "too big for the memory";
	case PW_REFUSED: return "refused by the server";
	case PW_NO_ANSWER: return "no answer from the server";
	case PW_NO_CARD: return "its card couldn't be fetched";
	}
	return "not made";
}

/* what kind of picture the bytes are */
int photo_kind(const unsigned char *d, long n)
{
	if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8)
		return PW_OK;
	if (n >= 4 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
		return PW_PNG;
	if (n >= 12 && !memcmp(d + 4, "ftyp", 4) && (!memcmp(d + 8, "hei", 3) || !memcmp(d + 8, "mif1", 4) ||
						    !memcmp(d + 8, "msf1", 4) || !memcmp(d + 8, "avif", 4)))
		return PW_HEIC;
	if (n >= 4 && !memcmp(d, "GIF8", 4))
		return PW_GIF;
	if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4))
		return PW_WEBP;
	return PW_OTHER;
}

/* the part to show: Apple's crop (its y counts from the bottom), else
   all; then a square in the middle of it */
static void square_of(long W, long H, const PHOTOREF *ref, long *sx, long *sy, long *sside)
{
	long x0 = 0, y0 = 0, x1 = W, y1 = H, side;
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
	side = x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0;
	*sx = x0 + (x1 - x0 - side) / 2;
	*sy = y0 + (y1 - y0 - side) / 2;
	*sside = side;
}

/* the cells each row or column of the square goes into */
static int cells_for(DEC *d, long sx0, long sy0, long side)
{
	int i;
	d->sx0 = sx0;
	d->sy0 = sy0;
	d->side = side;
	if (side < 1)
		return 0;
	free(d->t0);
	d->t0 = malloc(side * 2 * sizeof(short));
	if (!d->t0)
		return 0;
	d->t1 = d->t0 + side;
	for (i = 0; i < side; i++) {
		d->t0[i] = (short)((long)i * PHOTO_N / side);
		d->t1[i] = (short)((((long)i + 1) * PHOTO_N - 1) / side);
	}
	return 1;
}

static void cells_out(DEC *d, PHOTOIMG *out)
{
	int i;
	for (i = 0; i < PHOTO_N * PHOTO_N; i++) {
		unsigned short *s = d->sum + i * 4;
		unsigned r = 0, g = 0, b = 0;
		if (s[3]) {
			r = s[0] / s[3];
			g = s[1] / s[3];
			b = s[2] / s[3];
		}
		/* 6-bit averages to 5-6-5 */
		out->px[i] = (unsigned short)(((r >> 1) << 11) | (g << 5) | (b >> 1));
	}
}

/* what TJpgDec won't read, from the blocks' averages (jpegdc.c) */
static int decode_dc(const unsigned char *jpg, long len, const PHOTOREF *ref, DEC *d, PHOTOIMG *out)
{
	int w, h, wy, bx, by;
	long x0, y0, side;
	JDC *j = jdc_open(jpg, len, &w, &h, &wy);
	if (!j) {
		why(wy == JW_MEMORY ? PW_MEMORY : wy == JW_UNSUPPORTED ? PW_JPEG_KIND : PW_DAMAGED,
		    wy == JW_UNSUPPORTED ? "lossless, arithmetic-coded or 12-bit" : "");
		return 0;
	}
	jdc_poll = decode_poll;
	square_of(w, h, ref, &x0, &y0, &side);
	if (!cells_for(d, x0 >> 3, y0 >> 3, side >> 3 ? side >> 3 : 1) || !jdc_read(j)) {
		why(jdc_why(j) == JW_MEMORY || !d->t0 ? PW_MEMORY : PW_DAMAGED, "");
		jdc_free(j);
		return 0;
	}
	for (by = 0; by < d->side; by++) {
		short ty;
		for (bx = 0; bx < d->side; bx++) {
			int r, g, b;
			short tx;
			jdc_rgb(j, (int)(d->sx0 + bx), (int)(d->sy0 + by), &r, &g, &b);
			for (ty = d->t0[by]; ty <= d->t1[by]; ty++)
				for (tx = d->t0[bx]; tx <= d->t1[bx]; tx++) {
					unsigned short *s = d->sum + ((ty << 6) + tx) * 4;
					s[0] += r >> 2;
					s[1] += g >> 2;
					s[2] += b >> 2;
					s[3]++;
				}
		}
	}
	jdc_free(j);
	cells_out(d, out);
	return 1;
}

int photo_decode_jpeg(const unsigned char *jpg, long len, const PHOTOREF *ref, PHOTOIMG *out)
{
	JDEC jd;
	DEC d;
	void *pool;
	long x0, y0, side;
	int scale = 0, ok = 0, rc;
	why(PW_OK, 0);
	memset(&d, 0, sizeof(d));
	d.p = jpg;
	d.left = len;
	if (photo_kind(jpg, len) != PW_OK) {
		why(photo_kind(jpg, len), 0);
		return 0;
	}
	pool = malloc(POOL);
	d.sum = calloc(PHOTO_N * PHOTO_N * 4, sizeof(unsigned short));
	if (!pool || !d.sum) {
		why(PW_MEMORY, 0);
		goto done;
	}
	rc = jd_prepare(&jd, in_fn, pool, POOL, &d);
	if (rc != JDR_OK) {
		/* progressive, SOF1, another sampling, CMYK...: the averages */
		free(pool);
		pool = 0;
		dec_how = rc == JDR_FMT3 ? ", the averages (progressive or another kind)" : ", the averages";
		ok = decode_dc(jpg, len, ref, &d, out);
		goto done;
	}
	square_of(jd.width, jd.height, ref, &x0, &y0, &side);
	/* as small as it can be decoded with at least half of PHOTO_N across:
	   1/8 takes only each block's average (no IDCT at all), the quickest
	   by far on a 68000, and a photo from a phone is big enough for it */
	while (scale < 3 && (side >> (scale + 1)) >= PHOTO_N / 2)
		scale++;
	if (!cells_for(&d, x0 >> scale, y0 >> scale, side >> scale)) {
		why(PW_MEMORY, 0);
		goto done;
	}
	dec_how = scale == 3 ? ", 1/8" : scale == 2 ? ", 1/4" : scale == 1 ? ", 1/2" : ", whole";
	rc = jd_decomp(&jd, out_fn, (uint8_t)scale);
	if (rc == JDR_INTR && conn_stop) {
		why(PW_NO_ANSWER, "stopped");
		goto done;
	}
	if (rc != JDR_OK) {
		/* damaged as TJpgDec sees it: the averages may still be read */
		dec_how = ", the averages after TJpgDec failed";
		free(pool);
		pool = 0;
		memset(d.sum, 0, PHOTO_N * PHOTO_N * 4 * sizeof(unsigned short));
		ok = decode_dc(jpg, len, ref, &d, out);
		goto done;
	}
	cells_out(&d, out);
	ok = 1;
done:
	if (!ok && conn_stop)
		why(PW_NO_ANSWER, "stopped");
	free(pool);
	free(d.sum);
	free(d.t0);
	return ok;
}

/* ---------------- the cache ---------------- */

/* pictures that failed are tried again when the decoder is newer */
#define PHOTO_DECODER 3

typedef struct {
	int valid;
	PKENT e;		/* the index's: key, book, state, why, decoder, link */
	char uid[100];
	char ietag[100], ilmod[40];
	int px;			/* its pixels were read (into pxb) */
} PHDR;

static unsigned char pxb[PHOTO_N * PHOTO_N * 2];	/* a record's pixels, big-endian */

static const char *uid_of(const CONTACT *c)
{
	if (c->uid && *c->uid)
		return c->uid;
	if (c->book >= 0 && c->book < nbooks && c->res >= 0 && c->res < books[c->book].nres)
		return contact_href(c);
	return "";
}

static const char *card_etag(const CONTACT *c)
{
	if (c->book >= 0 && c->book < nbooks && c->res >= 0 && c->res < books[c->book].nres)
		return books[c->book].res[c->res].etag;
	return "";
}

/* c's picture in the pack: its book and UID (a .PIC file had the name) */
static unsigned long name_of(const CONTACT *c)
{
	char id[12];
	unsigned long h;
	snprintf(id, sizeof(id), "%d/", c->book >= 0 && c->book < nbooks ? books[c->book].id : 0);
	h = fnv(id, (long)strlen(id), 2166136261UL);
	h = fnv(uid_of(c), (long)strlen(uid_of(c)), h);
	return h & 0xFFFFFFFUL;
}

/* the record of c's picture, its pixels with it: one open */
static int read_hdr(const CONTACT *c, PHDR *h)
{
	char buf[1024], *p;
	int r;
	memset(h, 0, sizeof(*h));
	if (!pack_find(name_of(c), &h->e) ||
	    !(r = pack_read(&h->e, buf, sizeof(buf), h->e.failed ? 0 : pxb, (long)sizeof(pxb))))
		return 0;
	h->px = r == 2;
	for (p = buf + 10; p && *p;) {
		char *nl = strchr(p, '\n'), *eq = strchr(p, '=');
		if (nl)
			*nl = 0;
		if (eq) {
			*eq++ = 0;
			if (!strcmp(p, "UID"))
				str_copy(h->uid, eq, sizeof(h->uid));
			else if (!strcmp(p, "IETAG"))
				str_copy(h->ietag, eq, sizeof(h->ietag));
			else if (!strcmp(p, "ILMOD"))
				str_copy(h->ilmod, eq, sizeof(h->ilmod));
		}
		p = nl ? nl + 1 : 0;
	}
	h->valid = 1;
	return 1;
}

/* the pixels in the pack are big-endian 5-6-5, whatever the machine */
static int load_px(const PHDR *h, PHOTOIMG *out)
{
	int i;
	if (!h->px)
		return 0;
	for (i = 0; i < PHOTO_N * PHOTO_N; i++)
		out->px[i] = (unsigned short)((pxb[2 * i] << 8) | pxb[2 * i + 1]);
	return 1;
}

static int write_cache(const CONTACT *c, int link, const char *ietag, const char *ilmod, const PHOTOIMG *img)
{
	char head[600];
	int i, n;
	PKENT k;
	n = snprintf(head, sizeof(head), "ECPHOTO 1\nUID=%.98s\nBOOK=%d\nKEY=%08lX\nCETAG=%.98s\nIETAG=%.98s\n"
		     "ILMOD=%.38s\nLINK=%d\nSTATE=%s\nWHY=%d\nDEC=%d\n\n", uid_of(c), books[c->book].id, c->photo_key,
		     card_etag(c), ietag ? ietag : "", ilmod ? ilmod : "", link, img ? "OK" : "FAIL", img ? 0 : photo_why,
		     PHOTO_DECODER);
	if (img)
		for (i = 0; i < PHOTO_N * PHOTO_N; i++) {
			pxb[2 * i] = (unsigned char)(img->px[i] >> 8);
			pxb[2 * i + 1] = (unsigned char)img->px[i];
		}
	memset(&k, 0, sizeof(k));
	k.name = name_of(c);
	k.key = c->photo_key;
	k.cetag = pack_hash(card_etag(c));
	k.book = books[c->book].id;
	k.failed = !img;
	k.why = (unsigned char)(img ? 0 : photo_why);
	k.dec = PHOTO_DECODER;
	k.link = (unsigned char)link;
	return pack_put(&k, head, n, img ? pxb : 0, img ? (long)sizeof(pxb) : 0L);
}

/* the index's entry is this contact's, with this photo (no file read) */
static int ent_matches(const PKENT *e, const CONTACT *c)
{
	return e->key == c->photo_key && e->book == books[c->book].id && !(e->failed && e->dec < PHOTO_DECODER);
}

/* and the record's UID too */
static int hdr_matches(const PHDR *h, const CONTACT *c)
{
	return h->valid && ent_matches(&h->e, c) && !strcmp(h->uid, uid_of(c));
}

int photo_ready(const CONTACT *c)
{
	PKENT e;
	if (!c->photo_key || !pdir[0] || c->book < 0 || c->book >= nbooks)
		return 0;
	return pack_find(name_of(c), &e) && ent_matches(&e, c) && !e.failed &&
	       (!e.link || e.cetag == pack_hash(card_etag(c)));
}

int photo_cached(const CONTACT *c)
{
	PKENT e;
	return pdir[0] && c->book >= 0 && c->book < nbooks && pack_find(name_of(c), &e);
}

int photo_test_dec(const CONTACT *c, int dec)
{
	PHDR h;
	char head[1024];
	int was;
	if (!read_hdr(c, &h) || !pack_read(&h.e, head, sizeof(head), 0, 0))
		return 0;
	h.e.dec = (unsigned char)dec;
	was = pack_batch(0);
	{
		char rec[1100];
		long hl = (long)strlen(head);
		str_copy(rec, head, sizeof(rec));
		rec[hl] = '\n';	/* the blank line read cut */
		pack_put(&h.e, rec, hl + 1, h.px ? pxb : 0, h.px ? (long)sizeof(pxb) : 0L);
	}
	pack_batch(was);
	return 1;
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
	if (!http_request(&rq, rs, body, err, sizeof(err))) {
		why(PW_NO_ANSWER, err);
		return -1;
	}
	return rs->status;
}

static int get(const CONTACT *c, PHOTOIMG *out, int may_fetch, int *how, int *cached);

/* where a photo's time went, for the log: ms fetching the whole card, the
   picture behind a link (and its size), decoding, and keeping it */
static struct {
	unsigned long card, pic, dec, keep;
	long bytes;
} took;

static void secs(char *out, int size, const char *what, unsigned long ms)
{
	snprintf(out + strlen(out), size - strlen(out), "%s%s %lu.%02lu s", out[0] ? ", " : "", what,
		 ms / 1000, ms % 1000 / 10);
}

/* With Write a log on, each photo made, and where its time went */
static void log_made(const CONTACT *c)
{
	char line[300], name[100];
	line[0] = 0;
	if (took.card)
		secs(line, sizeof(line), "card fetched whole", took.card);
	if (took.bytes) {
		secs(line, sizeof(line), "picture", took.pic);
		snprintf(line + strlen(line), sizeof(line) - strlen(line), " (%ld KB)", (took.bytes + 1023) / 1024);
	}
	secs(line, sizeof(line), "decoded", took.dec);
	snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s", dec_how);
	secs(line, sizeof(line), "kept", took.keep);
	vcard_display_name(c, 0, name, sizeof(name));
	{
		char all[420];
		snprintf(all, sizeof(all), "%s: made: %s", name, line);
		conn_log("Photo", " -- ", all, (long)strlen(all));
	}
}

/* With Write a log on, each photo that can't be shown, and why */
static void log_failure(const CONTACT *c, int r, int cached)
{
	char name[80], line[300];
	if (r == PH_OK || r == PH_NONE)
		return;
	vcard_display_name(c, 0, name, sizeof(name));
	if (conn_stop)
		snprintf(line, sizeof(line), "%s: stopped (Cancel or Esc), not kept", name);
	else
		snprintf(line, sizeof(line), "%s: %s%s%s%s", name,
			 r == PH_NEEDS_NET && photo_why == PW_OK ? "needs the network" : photo_why_text(photo_why),
			 photo_why_detail[0] ? ", " : "", photo_why_detail, cached ? " (as the last time)" : "");
	conn_log("Photo", " -- ", line, (long)strlen(line));
}

int photo_get(const CONTACT *c, PHOTOIMG *out, int may_fetch, int *how)
{
	char uid[100];
	int cached = 0, r, bi = c->book;
	str_copy(uid, c->uid ? c->uid : "", sizeof(uid));
	why(PW_OK, 0);
	memset(&took, 0, sizeof(took));
	r = get(c, out, may_fetch, how, &cached);
	if (r == PH_OK && !cached) {
		const CONTACT *now = uid[0] ? contact_by_uid(uid, bi) : c;
		if (now)
			log_made(now);
	}
	if (r != PH_OK && r != PH_NONE) {
		/* c may have gone (fetched whole): found again to be named */
		const CONTACT *now = uid[0] ? contact_by_uid(uid, bi) : c;
		if (now)
			log_failure(now, r, cached);
	}
	return r;
}

static int get(const CONTACT *c, PHOTOIMG *out, int may_fetch, int *how, int *cached)
{
	char *raw;
	PHDR h;
	PHOTOREF ref;
	long len;
	int have, r = PH_FAILED, completed = 0;
	if (how)
		*how = PHH_CACHE;
	if (!pdir[0] || c->book < 0 || c->book >= nbooks)
		return PH_NONE;
	if (c->photo_key == VC_KEY_UNKNOWN) {
		/* the card came without its photo: now the whole card (c goes) */
		unsigned long t = pf_ms();
		if (!may_fetch)
			return PH_NEEDS_NET;
		c = contact_complete(c);
		took.card = pf_ms() - t;
		if (!c) {
			why(PW_NO_CARD, ab_err);
			return PH_NEEDS_NET;
		}
		if (c->photo_key == VC_KEY_UNKNOWN) {
			why(PW_MEMORY, "the card is too big to fetch whole");
			return PH_FAILED;
		}
		completed = 1;
	}
	have = read_hdr(c, &h);
	if (!c->photo_key) {
		/* no photo (any more): its picture goes */
		if (have)
			pack_drop(name_of(c));
		return PH_NONE;
	}
	if (have && hdr_matches(&h, c)) {
		int changed = h.e.link && h.e.cetag != pack_hash(card_etag(c));
		if (!changed || !may_fetch) {
			*cached = 1;
			if (h.e.failed) {
				why(h.e.why, 0);
				return PH_FAILED;
			}
			return load_px(&h, out) ? PH_OK : PH_FAILED;
		}
		/* the card changed and the photo is a link: has the picture? */
	} else if (have) {
		h.valid = 0;		/* another photo: start again */
	}
	raw = contact_raw(c, &len);
	if (!raw) {
		why(PW_MEMORY, "the card couldn't be read from the cache");
		return PH_FAILED;
	}
	if (!photo_ref(raw, len, &ref)) {
		free(raw);
		{
			char d[100];
			snprintf(d, sizeof(d), "the PHOTO line isn't data or a web link: %.60s", ref.head);
			why(PW_OTHER, d);
		}
		write_cache(c, 0, 0, 0, 0);
		return PH_FAILED;
	}
	free(raw);
	if (ref.kind == PR_DATA) {
		unsigned long t = pf_ms();
		if (photo_decode_jpeg(ref.data, ref.len, &ref, out)) {
			took.dec = pf_ms() - t;
			write_cache(c, 0, 0, 0, out);
			took.keep = pf_ms() - t - took.dec;
			r = PH_OK;
		} else if (conn_stop) {
			r = PH_NEEDS_NET;	/* stopped: tried again next time */
		} else {
			write_cache(c, 0, 0, 0, 0);	/* not again until it changes */
		}
		if (how)
			*how = completed ? PHH_FETCHED : PHH_DECODED;
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
		const PHDR *old = h.valid && !h.e.failed ? &h : 0;
		unsigned long t = pf_ms();
		sb_init(&body);
		st = fetch(c, ref.uri, old, &body, &rs);
		took.pic = pf_ms() - t;
		took.bytes = body.len ? body.len : 1;
		if (st == 304 && old && load_px(&h, out)) {
			/* the same picture: the card's new ETag noted */
			write_cache(c, 1, h.ietag, h.ilmod, out);
			if (how)
				*how = PHH_CHECKED;
			r = PH_OK;
		} else if (st == 200) {
			t = pf_ms();
			if (photo_decode_jpeg((unsigned char *)body.s, body.len, &ref, out)) {
				took.dec = pf_ms() - t;
				write_cache(c, 1, rs.etag, rs.lastmod, out);
				took.keep = pf_ms() - t - took.dec;
				r = PH_OK;
			} else if (conn_stop) {
				r = PH_NEEDS_NET;	/* stopped: tried again next time */
			} else {
				if (photo_why != PW_OK && !photo_why_detail[0])
					snprintf(photo_why_detail, sizeof(photo_why_detail), "%.40s, %ld bytes", rs.ctype, body.len);
				write_cache(c, 1, rs.etag, rs.lastmod, 0);
			}
			if (how)
				*how = PHH_FETCHED;
		} else if (st > 0) {
			/* refused, gone: not asked again until the card changes */
			char d[100];
			URL u;
			snprintf(d, sizeof(d), "HTTP %d from %.60s", st, url_parse(ref.uri, &u) ? u.host : "?");
			why(PW_REFUSED, d);
			write_cache(c, 1, 0, 0, 0);
		} else if (old && load_px(&h, out)) {
			r = PH_OK;		/* no answer: the picture we have */
		} else {
			if (photo_why != PW_NO_ANSWER)
				why(PW_NO_ANSWER, 0);
			r = PH_NEEDS_NET;
		}
		sb_free(&body);
	}
	return r;
}

/* ---------------- tidying ---------------- */

static int cmp_names(const void *a, const void *b)
{
	unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
	return x < y ? -1 : x > y;
}

static int unwanted(const PKENT *e, void *ud)
{
	const unsigned long *want = ((const unsigned long **)ud)[0];
	long lo = 0, hi = (long)((const unsigned long *)((const unsigned long **)ud)[1]) - 1;
	while (lo <= hi) {
		long mid = (lo + hi) / 2;
		if (want[mid] == e->name)
			return 0;
		if (want[mid] < e->name)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return 1;
}

/* From the pack's index alone: a picture whose contact is gone, or has
   no photo, goes (one open, only when there is one). One whose photo
   changed is replaced when it is next made. */
int photo_prune(void)
{
	unsigned long *want;
	const void *ud[2];
	int i, nw = 0, removed;
	if (!pdir[0])
		return 0;
	want = malloc((npeople + 1) * sizeof(*want));
	if (!want)
		return 0;
	for (i = 0; i < npeople; i++) {
		const CONTACT *c = people[i];
		if (c->photo_key && c->book >= 0 && c->book < nbooks)
			want[nw++] = name_of(c);
	}
	qsort(want, nw, sizeof(*want), cmp_names);
	ud[0] = want;
	ud[1] = (const void *)(long)nw;
	removed = pack_each(unwanted, (void *)ud);
	free(want);
	if (removed) {
		pack_flush();
		pack_tidy();
	}
	return removed;
}

/* ---------------- Sync Contact Photos ---------------- */

typedef struct {
	short book;
	int order;
	char *href;
} PTARGET;

static int cmp_targets(const void *a, const void *b)
{
	const PTARGET *x = a, *y = b;
	return x->book != y->book ? x->book - y->book : x->order - y->order;
}

#define PS_CHUNK 8	/* cards fetched whole at a time */

int photo_sync_why[PW_N];

int photo_sync_all(PHOTO_CB cb, void *ud, int *failed)
{
	PTARGET *t;
	PHOTOIMG *img;
	int nt = 0, i, done = 0, made = 0, bad = 0, stop = 0, net = !opt.offline, job = -1, was, chunk;
	*failed = 0;
	memset(photo_sync_why, 0, sizeof(photo_sync_why));
	if (!pdir[0])
		return 0;
	pack_check();			/* another EContact may have added some */
	t = malloc((npeople + 1) * sizeof(PTARGET));
	img = malloc(sizeof(PHOTOIMG));
	if (!t || !img) {
		free(t);
		free(img);
		str_copy(ab_err, "out of memory", sizeof(ab_err));
		return -1;
	}
	/* the cards fetched whole at a time: as many as the job's memory
	   holds (a photo's card is some 10 to 40 KB), one on a 1 MB ST */
	chunk = (int)(ab_job_bytes / (24L * 1024));
	chunk = chunk < 1 ? 1 : chunk > PS_CHUNK ? PS_CHUNK : chunk;
	/* who still needs one: a card without its photo, or one whose
	   picture isn't made (the pack's index says, no file read) */
	for (i = 0; i < npeople; i++) {
		const CONTACT *c = people[i];
		if (!c->photo_key || c->book < 0 || c->book >= nbooks)
			continue;
		if (c->photo_key == VC_KEY_UNKNOWN) {
			if (!net)
				continue;
		} else if (photo_ready(c)) {
			continue;
		}
		t[nt].book = c->book;
		t[nt].order = i;
		t[nt].href = strdup(contact_href(c));
		if (t[nt].href)
			nt++;
	}
	qsort(t, nt, sizeof(PTARGET), cmp_targets);
	/* the cards fetched and the pictures made gather in memory, and go
	   to the files in one go at the end, or when it is full (6.5) */
	was = pack_batch(1);
	for (i = 0; i < nt && !stop;) {
		int k, j, bi = t[i].book, nh = 0;
		char *hrefs[PS_CHUNK];
		if (bi != job) {
			if (job >= 0)
				book_fetch_job_end();
			job = book_fetch_job(bi) ? bi : -1;
		}
		/* the next few of one book: those without their photo fetched together */
		for (k = i; k < nt && k < i + chunk && t[k].book == bi; k++) {
			CONTACT *c = contact_by_href(bi, t[k].href);
			if (c && c->photo_key == VC_KEY_UNKNOWN)
				hrefs[nh++] = t[k].href;
		}
		if (nh) {
			if (!cb(ud, done, nt, 0))
				break;
			if (book_fetch_whole(&books[bi], hrefs, nh) < 0) {
				if (conn_stop) {
					cb(ud, done, nt, 0);	/* stopped: says so */
					break;
				}
				/* no answer: the rest another time */
				*failed = bad + nt - done;
				made = -1;
				break;
			}
		}
		for (j = i; j < k; j++) {
			CONTACT *c = contact_by_href(bi, t[j].href);
			char name[80];
			int how, r;
			name[0] = 0;
			if (c)
				vcard_display_name(c, 0, name, sizeof(name));
			if (!cb(ud, done, nt, name)) {
				stop = 1;
				break;
			}
			if (c && c->photo_key && c->photo_key != VC_KEY_UNKNOWN) {
				r = photo_get(c, img, net, &how);
				if (conn_stop && r != PH_OK) {
					cb(ud, done, nt, 0);	/* stopped, this one not done */
					stop = 1;
					break;
				}
				if (r == PH_OK) {
					made++;
				} else if (r != PH_NONE) {
					bad++;
					photo_sync_why[photo_why > 0 && photo_why < PW_N ? photo_why :
						       r == PH_NEEDS_NET ? PW_NO_ANSWER : PW_DAMAGED]++;
				}
			} else if (c && c->photo_key) {
				bad++;		/* still without its photo */
				photo_sync_why[PW_NO_CARD]++;
			}
			done++;
		}
		i = k;
		book_fetch_job_room();	/* written once the job's memory is full */
	}
	if (job >= 0 && !book_fetch_job_end() && made >= 0) {
		made = -1;		/* the cards couldn't be kept: fetched again next time */
		*failed = bad + nt - done;
	}
	pack_flush();
	pack_tidy();
	pack_batch(was);
	if (made >= 0) {
		cb(ud, done, nt, 0);
		*failed = bad;
	}
	{
		/* in the log: what was done, and the reasons */
		char line[300];
		int k, n;
		n = snprintf(line, sizeof(line), "Sync Contact Photos: %d to do, %d made, %d not", nt, made < 0 ? 0 : made,
			     bad);
		for (k = 1; k < PW_N; k++)
			if (photo_sync_why[k] && n < (int)sizeof(line) - 40)
				n += snprintf(line + n, sizeof(line) - n, "; %d %s", photo_sync_why[k], photo_why_text(k));
		conn_log("Photo", " -- ", line, (long)strlen(line));
		conn_log_flush();
	}
	for (i = 0; i < nt; i++)
		free(t[i].href);
	free(t);
	free(img);
	return made;
}
