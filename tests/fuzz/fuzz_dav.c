/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The contacts server's answers, from the fuzz input (the AERA
 * guidelines, 7.1): HTTP (status, headers, Content-Length, chunks, gzip,
 * redirects, logins), the WebDAV XML and the vCards in it, read by
 * EContact's own CardDAV code. The first byte picks the request and the
 * way (straight, or through the gateway).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "carddav.h"
#include "http.h"
#include "fuzz_net.h"

static int item(void *ud, const char *href, const char *etag)
{
	(void)ud;
	return strlen(href) + strlen(etag) < 100000;
}

static int card(void *ud, const char *href, const char *etag, const char *data, long len, int partial)
{
	volatile long sum = partial;
	long i;
	(void)ud;
	for (i = 0; i < len; i++)
		sum += data[i];
	sum += (long)strlen(href) + (long)strlen(etag);
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char book[] = "http://dav.example.org/addressbooks/me/card/";
	char err[200], ctag[100], etag[100];
	DAVAUTH a = { "user@example.org", "secret" };
	int sel;
	if (n < 1)
		return 0;
	sel = d[0];
	fuzz_net_set(d + 1, (long)n - 1);
	memset(&http_cfg, 0, sizeof(http_cfg));
	if (sel & 0x80) {
		strcpy(http_cfg.gate, "pi.example");
		http_cfg.gateport = 8180;
	}
	/* small, so the answers are asked for in pieces and read as they come */
	carddav_budget = (sel & 0x40) ? 24L * 1024 : 400L * 1024;
	switch (sel & 7) {
	case 0: {
		DAVBOOK *books = 0;
		int nb = 0;
		carddav_discover("http://dav.example.org/", &a, &books, &nb, err, sizeof(err));
		free(books);
		break;
	}
	case 1:
		carddav_ctag(book, &a, ctag, sizeof(ctag), err, sizeof(err));
		break;
	case 2:
		carddav_list(book, &a, item, 0, err, sizeof(err));
		break;
	case 3: {
		char *hrefs[] = { "/addressbooks/me/card/a.vcf", "/addressbooks/me/card/b.vcf" };
		carddav_multiget(book, &a, hrefs, 2, sel & 8, card, 0, err, sizeof(err));
		break;
	}
	case 4:
		carddav_put(book, "c.vcf", &a, "BEGIN:VCARD\r\nEND:VCARD\r\n", 24, (sel & 8) ? "\"1\"" : 0,
			    etag, sizeof(etag), err, sizeof(err));
		break;
	case 5:
		carddav_delete(book, "c.vcf", &a, "\"1\"", err, sizeof(err));
		break;
	default: {
		SBUF out;
		sb_init(&out);
		carddav_get(book, "a.vcf", &a, &out, etag, sizeof(etag), err, sizeof(err));
		sb_free(&out);
	}
	}
	http_close_all();
	return 0;
}
