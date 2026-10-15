/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * vCards from a server or an imported file (the AERA guidelines, 7.1):
 * every card parsed, shown as EContact shows it, opened for editing and
 * written back over the text it came in; the photo line found and taken
 * apart.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vcard.h"
#include "photo.h"

static const char *text;
static long len;

static int got(CONTACT *c, void *ud)
{
	char name[200], ini[8];
	CEDIT e;
	SBUF out;
	(void)ud;
	vcard_display_name(c, 0, name, sizeof(name));
	vcard_display_name(c, 1, name, sizeof(name));
	vcard_initials(c, ini);
	vcard_sort_key(c, 1);
	vcard_to_edit(c, &e);
	sb_init(&out);
	vcard_write(&e, "uid-1", text, len, &out);
	sb_free(&out);
	sb_init(&out);
	vcard_set_categories(text, len, "Family,Work", &out);
	sb_free(&out);
	free(c);
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	char *copy = malloc(n + 1);
	long start, stop;
	PHOTOREF ref;
	if (!copy)
		return 0;
	memcpy(copy, d, n);
	copy[n] = 0;
	text = copy;
	len = (long)n;
	vcard_parse_all(copy, len, got, 0);
	if (vcard_photo_span(copy, len, &start, &stop) && start >= 0 && stop <= len && start <= stop) {
		memset(&ref, 0, sizeof(ref));
		if (photo_ref(copy + start, stop - start, &ref))
			free(ref.data);
	}
	free(copy);
	return 0;
}
