/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * carddav.h - CardDAV (RFC 6352), the contacts protocol of iCloud,
 * Fastmail, Nextcloud, Radicale, Synology and others: WebDAV requests
 * carrying vCards. Built like ECal's caldav.c.
 */
#ifndef CARDDAV_H
#define CARDDAV_H

#include "util.h"

typedef struct {
	char url[400];		/* the address book collection, absolute */
	char name[64];		/* its name, Atari text */
	char ctag[100];		/* changes whenever anything in it changes */
	int readonly;
} DAVBOOK;

typedef struct {
	const char *user, *pass;
} DAVAUTH;

/* the address books of an account. base: the server's address, e.g.
   "https://contacts.icloud.com" (the principal and the address book
   home are found from there), or an address book's own URL. *books is
   malloc'ed */
int carddav_discover(const char *base, const DAVAUTH *a, DAVBOOK **books, int *nbooks,
		     char *err, int errlen);

/* the address book's ctag now; "" when the server doesn't give one */
int carddav_ctag(const char *url, const DAVAUTH *a, char *ctag, int size, char *err, int errlen);

/* the href (as the server wrote it, a path usually) and etag of every
   contact in it, to cb one by one (0 from cb stops: the call fails).
   Under 200 KB of carddav_budget the answer is read as it comes, so the
   list of thousands of contacts never is in memory whole */
typedef int (*DAV_ITEM_CB)(void *ud, const char *href, const char *etag);
int carddav_list(const char *url, const DAVAUTH *a, DAV_ITEM_CB cb, void *ud, char *err, int errlen);

/* fetch contacts by href, as many at a time as fit in carddav_budget;
   cb gets each (UTF-8 text). partial: without their photos and the lines
   EContact doesn't show, where the server can (cb's partial says if it
   did) */
typedef int (*DAV_CB)(void *ud, const char *href, const char *etag, const char *data, long len, int partial);
int carddav_multiget(const char *url, const DAVAUTH *a, char **hrefs, int n, int partial,
		     DAV_CB cb, void *ud, char *err, int errlen);
/* bytes of answer asked for at a time (the UI sets it from the memory) */
extern long carddav_budget;
/* cards the last carddav_multiget left out: too big for the memory */
extern int carddav_skipped;

/* store a vCard at href (relative to the address book): etag NULL for
   a new one (fails if it exists), else only if unchanged since. The
   new etag comes back in newetag ("" if the server didn't say) */
int carddav_put(const char *url, const char *href, const DAVAUTH *a, const char *data, long len,
		const char *etag, char *newetag, int size, char *err, int errlen);
int carddav_delete(const char *url, const char *href, const DAVAUTH *a, const char *etag,
		   char *err, int errlen);
/* one vCard by href (to see a contact again after the server changed
   it on saving); malloc'ed into *out */
int carddav_get(const char *url, const char *href, const DAVAUTH *a, SBUF *out, char *etag, int size,
		char *err, int errlen);

/* an HTTP status as words for alerts */
const char *carddav_status_text(int status);

#endif
