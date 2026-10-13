/*
 * photo.h - contacts' pictures: the JPEG a vCard carries (or links to),
 * decoded once into a small picture and kept in CACHE\PHOTOS, so a card
 * shows it at once the next time.
 *
 * A card's PHOTO is
 *   in the card:  PHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQ...   (3.0, as the
 *                 Mac and iPhone write it; 2.1's ENCODING=BASE64; 4.0's
 *                 PHOTO:data:image/jpeg;base64,...)
 *   a link:       PHOTO;VALUE=uri:https://p42-contacts.icloud.com/...
 * and Apple adds the part of it to show: X-ABCROP-RECTANGLE.
 *
 * Keeping the cache right: every cached picture records the fingerprint
 * of the PHOTO line it came from (CONTACT.photo_key, a hash of the whole
 * line). A card whose photo was changed, on the iPhone or anywhere, has
 * another line, so another fingerprint, and the picture is made again. A
 * link can stay the same while the picture behind it changes: so the
 * cache also records the card's ETag and the picture's own, and when the
 * card has changed the picture is asked for again with If-None-Match (a
 * "304 Not Modified" keeps the cached one). photo_prune() removes the
 * pictures of contacts that are gone or whose photo changed.
 */
#ifndef PHOTO_H
#define PHOTO_H

#include "abook.h"

#define PHOTO_N 64		/* the cached picture is PHOTO_N x PHOTO_N */

typedef struct {
	unsigned short px[PHOTO_N * PHOTO_N];	/* RGB 5-6-5, rows top to bottom */
} PHOTOIMG;

/* a PHOTO line, taken apart */
enum { PR_NONE, PR_DATA, PR_URI };
typedef struct {
	int kind;
	unsigned char *data;	/* PR_DATA: the decoded file (malloc'ed) */
	long len;
	char uri[400];		/* PR_URI */
	int fmt;		/* PF_JPEG, PF_PNG, PF_OTHER (from TYPE= or the bytes) */
	long crop[4];		/* Apple's: x, y from the bottom, width, height */
	int has_crop;
} PHOTOREF;
enum { PF_JPEG, PF_PNG, PF_OTHER };

/* the PHOTO line of a vCard: 1 if there is one. raw is changed (the
   data is decoded where it lies); free ref->data after */
int  photo_ref(char *raw, long len, PHOTOREF *ref);

/* a JPEG into a picture: the crop (or the middle square) scaled down;
   0 if it can't be decoded (progressive JPEGs, broken files) */
int  photo_decode_jpeg(const unsigned char *jpg, long len, const PHOTOREF *ref, PHOTOIMG *out);

/* where the pictures are kept (store_init sets it: CACHE\PHOTOS) */
void photo_dir(const char *dir);

/* the picture of c: from the cache when it is still right, else made
   (and, for a link, fetched when fetch is 1). how says which */
enum { PH_NONE, PH_OK, PH_FAILED, PH_NEEDS_NET };
enum { PHH_CACHE, PHH_DECODED, PHH_FETCHED, PHH_CHECKED };
int  photo_get(const CONTACT *c, PHOTOIMG *out, int fetch, int *how);
/* 1 if photo_get would answer from the cache without any work */
int  photo_ready(const CONTACT *c);
/* the pictures of contacts that are gone, or whose photo changed */
int  photo_prune(void);
/* the cache file of c (for the tests) */
void photo_path(const CONTACT *c, char *out, int size);

#endif
