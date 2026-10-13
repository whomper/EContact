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
	char *href;		/* as the server wrote it (a path, usually) */
	char *etag;
} DAVITEM;

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

/* hrefs and etags of every contact in it */
int carddav_list(const char *url, const DAVAUTH *a, DAVITEM **items, int *n, char *err, int errlen);
void carddav_free_items(DAVITEM *items, int n);

/* fetch contacts by href, a few at a time; cb gets each (UTF-8 text) */
typedef int (*DAV_CB)(void *ud, const char *href, const char *etag, const char *data, long len);
int carddav_multiget(const char *url, const DAVAUTH *a, char **hrefs, int n,
		     DAV_CB cb, void *ud, char *err, int errlen);

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
