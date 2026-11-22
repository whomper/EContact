/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * corecheck.c - EContact's core as the FireBee package runs it (the
 * guidelines, 7.4): built for this computer and as ColdFire code
 * (-mcpu=5475, run under qemu-m68k -cpu cfv4e by `make core-cf`), its
 * output must be the same, byte for byte:
 *
 * - vcard.c: everything tests/host/vcdump.c says of each file of the
 *   list (each card vcard_parse_all finds, the file as one card and as
 *   abook.c keeps it, vcard_write, vcard_set_categories, the photo span);
 * - the search: contact_matches over every card of the list for texts
 *   of each kind (a letter, a name, a space, digits, an address, none);
 * - the photos: each card's PHOTO and each JPEG of the list made into
 *   EContact's 64 x 64 picture (TJpgDec with its two 68000 changes, else
 *   jpegdc.c), a sum of its pixels, and jpegdc.c's blocks on their own.
 *
 *   corecheck LIST		(LIST: a path a line)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vcard.h"
#include "abook.h"
#include "photo.h"
#include "jpegdc.h"

#define main vcdump_main
#include "host/vcdump.c"
#undef main
#ifdef CFRUN
#define main cf_main	/* (tests/coldfire/cfrun.c's main calls it) */
#endif

/* what abook.c's and photo.c's other code links to (corecheck calls none
   of it; pf_ms and pf_entropy are vcdump.c's, fixed) */
int conn_stop;
void (*conn_tick)(void);
void (*pf_idle)(void);

static unsigned long fnv(unsigned long h, const void *p, long n)
{
	const unsigned char *b = p;
	while (n-- > 0)
		h = (h ^ *b++) * 16777619UL;
	return h & 0xffffffffUL;
}

static const char *const texts[] = {
	"a", "A", "jo", "Johnny", "tal az", "Azulay1", "555", "408 555", "+1", "@", "orchard.example",
	"Cupertino", "e z", " ", "zz", "Work", "\xc2", "0101", "Founder", "x@y", 0
};

static unsigned long search_sum;
static int search_cards;

static int searched(CONTACT *c, void *ud)
{
	unsigned long bits = 0;
	int i;
	(void)ud;
	for (i = 0; texts[i]; i++)
		if (contact_matches(c, texts[i]))
			bits |= 1UL << i;
	search_sum = fnv(search_sum, &bits, sizeof(bits));
	search_cards++;
	free(c);
	return 1;
}

static void picture(const char *what, const unsigned char *d, long n, const PHOTOREF *ref)
{
	static PHOTOIMG img;
	unsigned long h = 2166136261UL;
	int i, ok;
	memset(&img, 0, sizeof(img));
	ok = photo_decode_jpeg(d, n, ref, &img);
	for (i = 0; i < PHOTO_N * PHOTO_N; i++) {
		unsigned char b[2];
		b[0] = (unsigned char)(img.px[i] >> 8);
		b[1] = (unsigned char)img.px[i];
		h = fnv(h, b, 2);
	}
	printf("photo %s kind=%d decode=%d sum=%08lx\n", what, photo_kind(d, n), ok, h);
}

static void blocks(const char *what, const unsigned char *d, long n)
{
	JDC *j;
	int w = 0, h = 0, why = 0, r, g, b, x, y;
	unsigned long sum = 2166136261UL;
	j = jdc_open(d, n, &w, &h, &why);
	if (!j) {
		printf("jdc %s none why=%d\n", what, why);
		return;
	}
	if (jdc_read(j)) {
		for (y = 0; y < (h + 7) / 8 && y < 64; y++)
			for (x = 0; x < (w + 7) / 8 && x < 64; x++) {
				unsigned char c[3];
				jdc_rgb(j, x, y, &r, &g, &b);
				c[0] = (unsigned char)r;
				c[1] = (unsigned char)g;
				c[2] = (unsigned char)b;
				sum = fnv(sum, c, 3);
			}
		printf("jdc %s %dx%d sum=%08lx\n", what, w, h, sum);
	} else
		printf("jdc %s %dx%d read=0 why=%d\n", what, w, h, jdc_why(j));
	jdc_free(j);
}

static int pictured;

static void photos_of(const char *path, char *buf, long n)
{
	const char *p = buf, *end = buf + n;
	int k = 0;
	/* card by card: BEGIN:VCARD to END:VCARD */
	while (p < end) {
		const char *b = strstr(p, "BEGIN:VCARD"), *e;
		PHOTOREF ref;
		char what[300];
		if (!b || !(e = strstr(b, "END:VCARD")))
			break;
		e += 9;
		memset(&ref, 0, sizeof(ref));
		if (photo_ref((char *)b, (long)(e - b), &ref) && ref.kind == PR_DATA && ref.data) {
			snprintf(what, sizeof(what), "%s#%d", path, k);
			picture(what, ref.data, ref.len, &ref);
			blocks(what, ref.data, ref.len);
			pictured++;
		}
		free(ref.data);
		k++;
		p = e;
	}
}

static char *load(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	*n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (!(buf = malloc(*n + 1))) {
		fclose(f);
		return 0;
	}
	*n = (long)fread(buf, 1, *n, f);
	fclose(f);
	buf[*n] = 0;
	return buf;
}

int main(int argc, char **argv)
{
	static char *files[400];
	char *list, *p;
	long n;
	int nf = 0, i;
	if (argc < 2 || !(list = load(argv[1], &n))) {
		printf("corecheck LIST\n");
		return 2;
	}
	files[nf++] = "corecheck";
	for (p = list; *p && nf < 400; ) {
		char *e = strchr(p, '\n');
		if (e)
			*e = 0;
		if (*p)
			files[nf++] = p;
		if (!e)
			break;
		p = e + 1;
	}
	/* vcard.c, as vcdump says it */
	vcdump_main(nf, files);
	for (i = 1; i < nf; i++) {
		char *buf = load(files[i], &n);
		if (!buf)
			continue;
		if (!strncmp(buf, "\xff\xd8", 2)) {
			PHOTOREF ref;
			memset(&ref, 0, sizeof(ref));
			ref.kind = PR_DATA;
			ref.fmt = PF_JPEG;
			picture(files[i], (unsigned char *)buf, n, &ref);
			blocks(files[i], (unsigned char *)buf, n);
		} else {
			search_sum = 2166136261UL;
			search_cards = 0;
			vcard_parse_all(buf, n, searched, 0);
			printf("search %s cards=%d sum=%08lx\n", files[i], search_cards, search_sum);
			photos_of(files[i], buf, n);
		}
		free(buf);
	}
	printf("corecheck: %d files, %d photos\n", nf - 1, pictured);
	return 0;
}
