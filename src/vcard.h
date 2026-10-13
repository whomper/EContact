/*
 * vcard.h - vCard (RFC 2426 3.0, RFC 6350 4.0, and the old 2.1 of
 * phones and Outlook), the format iCloud and every CardDAV server keep
 * contacts in.
 *
 * A contact is read into one block of memory: the CONTACT and all its
 * text (Atari characters) together, so a thousand contacts cost a
 * thousand mallocs, not twenty thousand. Changing one goes through a
 * CEDIT, plain arrays the editor fills, and vcard_write() makes the new
 * vCard from it and the old text, keeping everything this program
 * doesn't edit (the photo, related names, social profiles, Apple's own
 * properties) as it was.
 */
#ifndef VCARD_H
#define VCARD_H

#include "util.h"

/* a labelled value: an email, a phone, an address, a web page. Labels
   are as the Mac shows them: "mobile", "iPhone", "home", "work",
   "home fax", "other" or whatever the user typed. An address's value is
   its parts, one per line: street, city, region, postcode, country */
typedef struct {
	const char *label, *value;
} VCITEM;

typedef struct CONTACT {
	const char *uid;
	const char *fn;			/* the full name as written (FN) */
	const char *family, *given, *middle, *prefix, *suffix;
	const char *nick, *org, *dept, *title;
	const char *bday;		/* "1980-05-17", or "--05-17" without the year */
	const char *note;
	const char *cats;		/* CATEGORIES, comma-separated (Nextcloud's groups) */
	VCITEM *email, *tel, *adr, *url;
	const char **member;		/* a group's members: their UIDs */
	unsigned char nemail, ntel, nadr, nurl;
	unsigned short nmember;
	unsigned char company;		/* shown as a company (X-ABShowAs:COMPANY) */
	unsigned char group;		/* a group card (iCloud's way of keeping groups) */
	unsigned char photo;		/* has a picture (kept, not shown) */
	unsigned char mark;		/* for the caller */
	short book, res;		/* where it is kept (abook.c) */
	char sortkey[24];		/* upper case, for sorting and the letter headings */
} CONTACT;

/* parse one vCard (UTF-8, or what its CHARSET= says); NULL if it isn't
   one. The result is one malloc: free() it */
CONTACT *vcard_parse(const char *text, long len);
/* every vCard in a file of several (an export, an import): cb for each;
   returns how many */
int vcard_parse_all(const char *text, long len, int (*cb)(CONTACT *c, void *ud), void *ud);

/* the name to show: "First Last" (or "Last, First" when last_first), the
   company for a company card, else an email or phone, else "No Name" */
void vcard_display_name(const CONTACT *c, int last_first, char *out, int size);
/* the initials for the round picture: "EY" */
void vcard_initials(const CONTACT *c, char *out);
/* the key the list is sorted by: last name first (by_first: first name) */
void vcard_sort_key(CONTACT *c, int by_first);

/* ---- editing ---- */
#define CE_EMAIL 6
#define CE_TEL   6
#define CE_ADR   3
#define CE_URL   3
typedef struct {
	char label[24];
	char value[100];
} CEITEM;
typedef struct {
	char label[24];
	char street[100], city[48], region[48], zip[16], country[48];
} CEADR;
typedef struct {
	char prefix[24], given[64], middle[48], family[64], suffix[24];
	char nick[48], org[80], dept[64], title[64];
	CEITEM email[CE_EMAIL], tel[CE_TEL], url[CE_URL];
	CEADR adr[CE_ADR];
	char bday[16];			/* as typed: "17.05.1980", "17.05" */
	char note[600];
	short company;
} CEDIT;

void vcard_to_edit(const CONTACT *c, CEDIT *e);
/* a new vCard (UTF-8) from e: uid for a new one, else the old text's;
   old (may be NULL) gives what isn't edited. 0 if e has no name, company,
   email or phone at all */
int vcard_write(const CEDIT *e, const char *uid, const char *old, long oldlen, SBUF *out);
/* a group card with these members (UIDs); old keeps its other lines */
int vcard_write_group(const char *name, const char *uid, const char **members, int n,
		      const char *old, long oldlen, SBUF *out);
/* a contact's CATEGORIES changed (Nextcloud-style groups): the old text
   with the new list (Atari text, comma-separated) */
int vcard_set_categories(const char *old, long oldlen, const char *cats, SBUF *out);
/* a new UID: random hex in the form Apple uses */
void vcard_new_uid(char *out, int size);

/* "17.05.1980" or "17.05" -> "1980-05-17" / "--05-17"; 0 if not a date */
int vcard_bday_parse(const char *typed, char *out, int size);
/* "17 May 1980", "17 May" for the card; 0 if bday isn't a date */
int vcard_bday_format(const char *bday, char *out, int size);
/* the parts of a birthday: year 0 when it has none */
int vcard_bday_split(const char *bday, int *y, int *m, int *d);

/* label words as the Mac shows them, for the editor's popups */
extern const char *const vc_tel_labels[];	/* "mobile", "iPhone", "home"... NULL-ended */
extern const char *const vc_email_labels[];
extern const char *const vc_adr_labels[];
extern const char *const vc_url_labels[];

#endif
