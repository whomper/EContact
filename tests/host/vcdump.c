/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vcdump.c - everything vcard.c makes of each file given, as text, to
 * compare two versions of the parser (tools/parser_diff.sh): each card
 * vcard_parse_all finds, the file read as one card by vcard_parse and as
 * abook.c's parse_kept reads it (the photo a stand-in), each CONTACT's
 * fields, names and keys, the card vcard_write makes of it over the
 * text, vcard_set_categories' and the photo's span. Built for this
 * computer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vcard.h"

unsigned long pf_ms(void) { return 1234; }
void pf_entropy(unsigned char *p, int n) { memset(p, 7, n); }

static const char *text;
static long tlen;

static void s(const char *k, const char *v)
{
	printf(" %s=", k);
	if (!v) { printf("(null)"); return; }
	for (; *v; v++)
		if ((unsigned char)*v < 32 || *v == '\\') printf("\\%02x", (unsigned char)*v); else putchar(*v);
}

static void items(const char *k, VCITEM *it, int n)
{
	int i;
	for (i = 0; i < n; i++) { printf("\n  %s[%d]", k, i); s("l", it[i].label); s("v", it[i].value); }
}

static void dump(CONTACT *c)
{
	char name[200], ini[8];
	CEDIT e;
	SBUF out;
	int i;
	printf("C");
	s("uid", c->uid); s("fn", c->fn); s("fam", c->family); s("giv", c->given); s("mid", c->middle);
	s("pre", c->prefix); s("suf", c->suffix); s("nick", c->nick); s("org", c->org); s("dept", c->dept);
	s("title", c->title); s("bday", c->bday); s("note", c->note); s("cats", c->cats);
	printf(" co=%d gr=%d ph=%d key=%08lx sort=", c->company, c->group, c->photo, c->photo_key);
	s("", c->sortkey);
	items("email", c->email, c->nemail); items("tel", c->tel, c->ntel);
	items("adr", c->adr, c->nadr); items("url", c->url, c->nurl);
	for (i = 0; i < c->nmember; i++) { printf("\n  mem"); s("", c->member[i]); }
	vcard_display_name(c, 0, name, sizeof(name)); printf("\n  dn0"); s("", name);
	vcard_display_name(c, 1, name, sizeof(name)); printf(" dn1"); s("", name);
	vcard_initials(c, ini); printf(" ini"); s("", ini);
	vcard_sort_key(c, 1); printf(" sort1"); s("", c->sortkey);
	vcard_to_edit(c, &e);
	sb_init(&out);
	printf("\n  write=%d", vcard_write(&e, "uid-1", text, tlen, &out));
	if (out.s) { out.s[out.len] = 0; s("", out.s); }
	sb_free(&out);
	sb_init(&out);
	printf("\n  cats=%d", vcard_set_categories(text, tlen, "Family,Work", &out));
	if (out.s) { out.s[out.len] = 0; s("", out.s); }
	sb_free(&out);
	printf("\n");
}

/* abook.c's parse_kept */
static CONTACT *kept(const char *raw, long len)
{
	long s = 0, e = 0, n;
	char mark[40], *t;
	int ml;
	CONTACT *c;
	if (!vcard_photo_span(raw, len, &s, &e))
		return vcard_parse(raw, len);
	if (vcard_photo_empty(raw + s, e - s))
		ml = snprintf(mark, sizeof(mark), "%s:?\r\n", VC_PHOTO_MARK);
	else
		ml = snprintf(mark, sizeof(mark), "%s:%08lX\r\n", VC_PHOTO_MARK, vcard_photo_key(raw + s, e - s));
	n = s + ml + (len - e);
	if (!(t = malloc(n + 1)))
		return vcard_parse(raw, len);
	memcpy(t, raw, s);
	memcpy(t + s, mark, ml);
	memcpy(t + s + ml, raw + e, len - e);
	t[n] = 0;
	c = vcard_parse(t, n);
	free(t);
	return c;
}

static int got(CONTACT *c, void *ud)
{
	(void)ud;
	dump(c);
	free(c);
	return 1;
}

int main(int argc, char **argv)
{
	int a;
	for (a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb");
		char *buf;
		long n, st = -1, sp = -1;
		CONTACT *c;
		if (!f) continue;
		fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
#ifdef VCDUMP_EXACT
		/* the card in a buffer of its size, nothing after it: what isn't
		   NUL-terminated (vcard_parse_all wants it, it isn't run) */
		buf = malloc(n ? n : 1);
		n = (long)fread(buf, 1, n, f); fclose(f);
		text = buf; tlen = n;
		printf("== %s\n", argv[a]);
		(void)got;
#else
		buf = malloc(n + 1);
		n = (long)fread(buf, 1, n, f); fclose(f);
		buf[n] = 0;
		text = buf; tlen = n;
		printf("== %s\n", argv[a]);
		printf("all=%d\n", vcard_parse_all(buf, n, got, 0));
#endif
		c = vcard_parse(buf, n);
		printf("one nomem=%d ", vcard_nomem);
		if (c) { dump(c); free(c); } else printf("none\n");
		c = kept(buf, n);
		printf("kept nomem=%d ", vcard_nomem);
		if (c) { dump(c); free(c); } else printf("none\n");
		printf("span=%d", vcard_photo_span(buf, n, &st, &sp));
		printf(" %ld %ld\n", st, sp);
		free(buf);
	}
	return 0;
}
