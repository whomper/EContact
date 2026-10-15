/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * lowmem.c - writing a card again when there isn't the memory to read it
 * whole: a card with a big photo, mallocs over a limit refused, as on an
 * ST. Nothing may be written then, never the card without its photo and
 * the lines after it. Built without the sanitizers, which take malloc
 * over themselves (make test).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vcard.h"

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
	printf("lowmem: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
