/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * lowmem.c - writing a card again when there isn't the memory to read it
 * whole: a card with a big photo, mallocs over a limit refused, as on an
 * ST. Nothing may be written then, never the card without its photo and
 * the lines after it; nor ECONTACT.INF cut short (its accounts and books
 * lost: an accessory's full heap, 1.50 on a 1 MB ST). Built without the sanitizers, which take malloc
 * over themselves (make test).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vcard.h"
#include "abook.h"
#include "plat.h"

static long fail_above;
void *__real_malloc(size_t n);
void *__real_realloc(void *p, size_t n);
void *__wrap_malloc(size_t n)
{
	return fail_above && n > (size_t)fail_above ? 0 : __real_malloc(n);
}
void *__wrap_realloc(void *p, size_t n)
{
	return fail_above && n > (size_t)fail_above ? 0 : __real_realloc(p, n);
}

static int fails, checks;
static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

int main(void)
{
	SBUF old, out;
	CEDIT e;
	CONTACT *c;
	const char *mem[1] = { "abc-123" };
	long i;
	sb_init(&old);
	sb_adds(&old, "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Carter;Maya;;;\r\nFN:Maya Carter\r\n"
		      "EMAIL;TYPE=WORK:maya@example.com\r\nPHOTO;ENCODING=b;TYPE=JPEG:");
	for (i = 0; i < 200000; i++)
		sb_addc(&old, 'A' + i % 26);
	sb_adds(&old, "\r\nX-SOCIALPROFILE;type=twitter:x-apple:maya\r\nRELATED;TYPE=spouse:Sam\r\n"
		      "CATEGORIES:Family\r\nUID:abc-123\r\nEND:VCARD\r\n");
	c = vcard_parse(old.s, old.len);
	check(c != 0, "the card read");
	if (!c)
		return 1;
	vcard_to_edit(c, &e);
	str_copy(e.title, "Engineer", sizeof(e.title));

	/* with the memory: everything kept */
	sb_init(&out);
	check(vcard_write(&e, "abc-123", old.s, old.len, &out), "written with the memory");
	check(out.s && strstr(out.s, "PHOTO") && strstr(out.s, "RELATED") && strstr(out.s, "Engineer"),
	      "the photo and the lines after it kept");
	sb_free(&out);

	/* without: refused, nothing written */
	fail_above = 150000;
	sb_init(&out);
	check(!vcard_write(&e, "abc-123", old.s, old.len, &out), "vcard_write refuses without the memory");
	check(vcard_nomem, "vcard_write says why");
	check(!out.len, "vcard_write wrote nothing");
	sb_free(&out);
	sb_init(&out);
	check(!vcard_set_categories(old.s, old.len, "Friends", &out), "vcard_set_categories refuses");
	check(!out.len, "vcard_set_categories wrote nothing");
	sb_free(&out);
	sb_init(&out);
	check(!vcard_write_group("Club", "abc-123", mem, 1, old.s, old.len, &out), "vcard_write_group refuses");
	check(!out.len, "vcard_write_group wrote nothing");
	sb_free(&out);
	fail_above = 0;

	/* and again with the memory back */
	sb_init(&out);
	check(vcard_set_categories(old.s, old.len, "Friends", &out) && strstr(out.s, "PHOTO") &&
	      strstr(out.s, "CATEGORIES:Friends"), "categories written with the memory back");
	sb_free(&out);
	/* ECONTACT.INF: all of it written, or the old one left */
	{
		char dir[] = "/tmp/lowmemXXXXXX", inf[200];
		FILE *f;
		long len;
		char *t;
		static const char was[] = "[OPTIONS]\r\nSTART=5\r\nSYNC_AFTER=1\r\n[ACCOUNT]\r\nNAME=Club\r\nKIND=1\r\n"
					  "[BOOK]\r\nID=1\r\nNAME=Club\r\n";
		check(mkdtemp(dir) != 0, "a folder for ECONTACT.INF");
		snprintf(inf, sizeof(inf), "%s/ECONTACT.INF", dir);
		f = fopen(inf, "wb");
		fputs(was, f);
		fclose(f);
		store_init(dir, inf);
		fail_above = 1024;	/* the text outgrows it half way */
		store_save_settings();
		fail_above = 0;
		t = pf_load(inf, &len);
		check(t && !strcmp(t, was), "ECONTACT.INF left as it was without the memory");
		check(store_unsaved, "and written at the next save");
		free(t);
		store_save_settings();
		t = pf_load(inf, &len);
		check(t && strstr(t, "START=5") && strstr(t, "SYNC_AFTER=1") && strstr(t, "[BOOK]") &&
		      strstr(t, "NAME=Club") && !store_unsaved, "written whole with the memory back");
		free(t);
	}
	printf("lowmem: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
