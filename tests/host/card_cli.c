/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * card_cli.c - the contacts core on Linux, for the integration tests and
 * for trying servers before the Atari does: the same abook.c, sync,
 * CardDAV, HTTP and TLS code as ECONTACT.ACC.
 *
 *   card-cli DIR carddav NAME URL USER PASS   add a CardDAV account and sync it
 *   card-cli DIR local NAME                   an On My Atari address book
 *   card-cli DIR gateway HOST PORT            go through the Pi gateway
 *   card-cli DIR falcon 0|1                   TLS here (CACERT=path for the roots)
 *   card-cli DIR sync [force]
 *   card-cli DIR books
 *   card-cli DIR list                         name, emails and phones of each
 *   card-cli DIR show NAME                    every field of one
 *   card-cli DIR add BOOK GIVEN FAMILY EMAIL [PHONE]
 *   card-cli DIR set NAME FIELD VALUE         FIELD: title org email phone note bday
 *   card-cli DIR delete NAME
 *   card-cli DIR groups
 *   card-cli DIR group-new BOOK NAME
 *   card-cli DIR group-add GROUP NAME
 *   card-cli DIR group-remove GROUP NAME
 *   card-cli DIR group-rename GROUP NEWNAME
 *   card-cli DIR group-delete GROUP
 *   card-cli DIR import BOOK FILE
 *   card-cli DIR export FILE
 *   card-cli DIR people                       CONTACTS.TXT, as EMail and ECal read it
 *   card-cli DIR photo NAME [offline]         the picture: how it came, and its colours
 *   card-cli DIR photo-ppm NAME FILE          the picture as a PPM file, to look at
 *   card-cli DIR photo-file NAME              its cache file, and whether it is there
 *   card-cli DIR prune                        pictures of contacts gone or changed out
 *   card-cli DIR cache NAME                   how the cache has it: part or whole, the photo
 *   card-cli DIR photo-sync [STOP]            Sync Contact Photos (STOP: cancel after that many)
 * CARD_BUDGET=bytes sets carddav_budget (the answer size asked for at a time).
 * BOOK is an address book's name; names are UTF-8 on the command line
 * and in the output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "abook.h"
#include "photo.h"
#include "carddav.h"
#include "charset.h"
#include "conn.h"
#include "tls.h"
#include "http.h"
#include "util.h"
#include "plat.h"

static void show_status(const char *s)
{
	if (getenv("CARD_VERBOSE") && *s)
		fprintf(stderr, "  .. %s\n", s);
}

static char *to_atari(const char *utf8)
{
	long n;
	return cs_to_atari(utf8, (long)strlen(utf8), CS_UTF8, &n);
}

static char *to_utf8(const char *atari)
{
	long n;
	return atari_to_utf8(atari ? atari : "", (long)strlen(atari ? atari : ""), &n);
}

static void out(const char *atari)
{
	char *u = to_utf8(atari);
	fputs(u, stdout);
	free(u);
}

static int find_book(const char *name)
{
	char *a = to_atari(name);
	int i, r = -1;
	for (i = 0; i < nbooks; i++)
		if (!strcmp(books[i].name, a))
			r = i;
	free(a);
	if (r < 0)
		fprintf(stderr, "no address book \"%s\"\n", name);
	return r;
}

static CONTACT *find(const char *name)
{
	char *a = to_atari(name), n[160];
	int i;
	CONTACT *r = 0;
	for (i = 0; i < npeople && !r; i++) {
		vcard_display_name(people[i], 0, n, sizeof(n));
		if (!people[i]->group && !strcmp(n, a))
			r = people[i];
	}
	free(a);
	if (!r)
		fprintf(stderr, "no contact \"%s\"\n", name);
	return r;
}

static GROUP *find_group(const char *name)
{
	char *a = to_atari(name);
	int i;
	GROUP *g = 0;
	for (i = 0; i < ngroups; i++)
		if (!strcmp(groups[i].name, a))
			g = &groups[i];
	free(a);
	if (!g)
		fprintf(stderr, "no group \"%s\"\n", name);
	return g;
}

static void show(const CONTACT *c)
{
	int i;
	char n[160], d[40];
	vcard_display_name(c, 0, n, sizeof(n));
	out(n);
	printf("\n");
#define F(f) if (c->f) { printf("  " #f ": "); out(c->f); printf("\n"); }
	F(uid) F(org) F(dept) F(title) F(nick) F(note)
#undef F
	for (i = 0; i < c->nemail; i++) {
		printf("  email ");
		out(c->email[i].label);
		printf(": %s\n", c->email[i].value);
	}
	for (i = 0; i < c->ntel; i++) {
		printf("  phone ");
		out(c->tel[i].label);
		printf(": %s\n", c->tel[i].value);
	}
	for (i = 0; i < c->nadr; i++) {
		char *v = strdup(c->adr[i].value), *p;
		for (p = v; *p; p++)
			if (*p == '\n')
				*p = '|';
		printf("  address ");
		out(c->adr[i].label);
		printf(": ");
		out(v);
		printf("\n");
		free(v);
	}
	if (vcard_bday_format(c->bday, d, sizeof(d)))
		printf("  birthday: %s\n", d);
}

static int set_field(CONTACT *c, const char *field, const char *value)
{
	CEDIT e;
	char *v = to_atari(value);
	CONTACT *r;
	vcard_to_edit(c, &e);
	if (!strcmp(field, "title"))
		str_copy(e.title, v, sizeof(e.title));
	else if (!strcmp(field, "org"))
		str_copy(e.org, v, sizeof(e.org));
	else if (!strcmp(field, "note"))
		str_copy(e.note, v, sizeof(e.note));
	else if (!strcmp(field, "bday"))
		str_copy(e.bday, v, sizeof(e.bday));
	else if (!strcmp(field, "email")) {
		int i;
		for (i = 0; i < CE_EMAIL && e.email[i].value[0]; i++)
			;
		if (i < CE_EMAIL) {
			str_copy(e.email[i].label, "work", sizeof(e.email[i].label));
			str_copy(e.email[i].value, v, sizeof(e.email[i].value));
		}
	} else if (!strcmp(field, "phone")) {
		int i;
		for (i = 0; i < CE_TEL && e.tel[i].value[0]; i++)
			;
		if (i < CE_TEL) {
			str_copy(e.tel[i].label, "mobile", sizeof(e.tel[i].label));
			str_copy(e.tel[i].value, v, sizeof(e.tel[i].value));
		}
	} else {
		fprintf(stderr, "no field %s\n", field);
		free(v);
		return 0;
	}
	free(v);
	r = contact_save(&e, c, -1);
	if (!r)
		fprintf(stderr, "save: %s\n", ab_err);
	return r != 0;
}

static int stop_after = -1;

static int sync_cb(void *ud, int done, int total, const char *name)
{
	(void)ud;
	if (name && stop_after >= 0 && done >= stop_after)
		return 0;
	if (name) {
		printf("%d/%d ", done + 1, total);
		out(name);
		printf("\n");
	} else if (done < total) {
		printf("%d/%d fetching\n", done + 1, total);
	}
	return 1;
}

int main(int argc, char **argv)
{
	const char *dir, *cmd;
	int ok = 1;
	if (argc < 3) {
		fprintf(stderr, "usage: card-cli DIR command ...\n");
		return 2;
	}
	dir = argv[1];
	cmd = argv[2];
	pf_mkdir(dir);
	ab_status = show_status;
	store_init(dir, 0);
	if (getenv("CACERT"))
		str_copy(conn_cacert, getenv("CACERT"), sizeof(conn_cacert));
	if (getenv("CARD_LOG")) {
		str_copy(conn_logfile, getenv("CARD_LOG"), sizeof(conn_logfile));
	}
	if (getenv("CARD_BUDGET"))
		carddav_budget = atol(getenv("CARD_BUDGET"));
	net_init();
	book_load_all();

	if (!strcmp(cmd, "carddav") && argc >= 7) {
		ACCOUNT *a = acct_add(AK_CARDDAV);
		char *n = to_atari(argv[3]);
		str_copy(a->name, n, sizeof(a->name));
		free(n);
		str_copy(a->url, argv[4], sizeof(a->url));
		str_copy(a->user, argv[5], sizeof(a->user));
		str_copy(a->pass, argv[6], sizeof(a->pass));
		store_save_settings();
		if (sync_account(naccts - 1, 1) < 0) {
			fprintf(stderr, "sync: %s\n", ab_err);
			ok = 0;
		}
	} else if (!strcmp(cmd, "local") && argc >= 4) {
		ACCOUNT *a = acct_add(AK_LOCAL);
		char *n = to_atari(argv[3]);
		str_copy(a->name, "On My Atari", sizeof(a->name));
		book_add(naccts - 1, n);
		free(n);
	} else if (!strcmp(cmd, "gateway") && argc >= 5) {
		str_copy(opt.gate, argv[3], sizeof(opt.gate));
		opt.gateport = (unsigned short)atoi(argv[4]);
	} else if (!strcmp(cmd, "falcon") && argc >= 4) {
		opt.falcon = (short)atoi(argv[3]);
	} else if (!strcmp(cmd, "sync")) {
		if (sync_all(argc >= 4) < 0) {
			fprintf(stderr, "sync: %s\n", ab_err);
			ok = 0;
		}
	} else if (!strcmp(cmd, "accessory")) {
		/* the accessory on single TOS: what it read stays in memory
		   while the program runs (here: until a line comes on stdin);
		   when its window opens again it reads the files again if they
		   changed (ui/main.c files_check), then refreshes */
		char line[16];
		unsigned long was = store_stamp();
		printf("waiting\n");
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin)) {
			ok = 0;
		} else {
			if (store_stamp() != was) {
				printf("the files changed: read again\n");
				store_release();
				store_init(dir, 0);
				book_load_all();
			} else {
				printf("the files as they were\n");
			}
			if (sync_all(0) < 0) {
				fprintf(stderr, "sync: %s\n", ab_err);
				ok = 0;
			}
		}
	} else if (!strcmp(cmd, "books")) {
		int i;
		for (i = 0; i < nbooks; i++) {
			out(books[i].name);
			printf(" (%s) %d contacts%s\n", accts[books[i].acct].name, books[i].count,
			       books[i].readonly ? " read-only" : "");
		}
	} else if (!strcmp(cmd, "list")) {
		int i, j;
		for (i = 0; i < npeople; i++) {
			char n[160];
			CONTACT *c = people[i];
			if (c->group)
				continue;
			vcard_display_name(c, 0, n, sizeof(n));
			out(n);
			for (j = 0; j < c->nemail; j++)
				printf(" | %s", c->email[j].value);
			for (j = 0; j < c->ntel; j++)
				printf(" | %s", c->tel[j].value);
			printf("\n");
		}
	} else if (!strcmp(cmd, "show") && argc >= 4) {
		CONTACT *c = find(argv[3]);
		if (c)
			show(c);
		ok = c != 0;
	} else if (!strcmp(cmd, "add") && argc >= 7) {
		int b = find_book(argv[3]);
		CEDIT e;
		char *g = to_atari(argv[4]), *f = to_atari(argv[5]);
		memset(&e, 0, sizeof(e));
		str_copy(e.given, g, sizeof(e.given));
		str_copy(e.family, f, sizeof(e.family));
		free(g);
		free(f);
		str_copy(e.email[0].label, "home", sizeof(e.email[0].label));
		str_copy(e.email[0].value, argv[6], sizeof(e.email[0].value));
		if (argc >= 8) {
			str_copy(e.tel[0].label, "mobile", sizeof(e.tel[0].label));
			str_copy(e.tel[0].value, argv[7], sizeof(e.tel[0].value));
		}
		if (b < 0 || !contact_save(&e, 0, b)) {
			fprintf(stderr, "add: %s\n", ab_err);
			ok = 0;
		}
	} else if (!strcmp(cmd, "set") && argc >= 6) {
		CONTACT *c = find(argv[3]);
		ok = c && set_field(c, argv[4], argv[5]);
	} else if (!strcmp(cmd, "delete") && argc >= 4) {
		CONTACT *c = find(argv[3]);
		ok = c && contact_delete(c);
		if (c && !ok)
			fprintf(stderr, "delete: %s\n", ab_err);
	} else if (!strcmp(cmd, "groups")) {
		int i;
		for (i = 0; i < ngroups; i++) {
			int j, first = 1;
			out(groups[i].name);
			printf(" (%s, %d):", groups[i].card ? "card" : "category", group_count(&groups[i]));
			for (j = 0; j < npeople; j++)
				if (group_has(&groups[i], people[j])) {
					char n[160];
					vcard_display_name(people[j], 0, n, sizeof(n));
					printf("%s", first ? " " : ", ");
					out(n);
					first = 0;
				}
			printf("\n");
		}
	} else if (!strcmp(cmd, "group-new") && argc >= 5) {
		int b = find_book(argv[3]);
		char *n = to_atari(argv[4]);
		ok = b >= 0 && group_new(b, n) >= 0;
		free(n);
		if (!ok)
			fprintf(stderr, "group-new: %s\n", ab_err);
	} else if ((!strcmp(cmd, "group-add") || !strcmp(cmd, "group-remove")) && argc >= 5) {
		GROUP *g = find_group(argv[3]);
		CONTACT *c = find(argv[4]);
		ok = g && c && (cmd[6] == 'a' ? group_add(g, c) : group_remove(g, c));
		if (g && c && !ok)
			fprintf(stderr, "%s: %s\n", cmd, ab_err);
	} else if (!strcmp(cmd, "group-rename") && argc >= 5) {
		GROUP *g = find_group(argv[3]);
		char *n = to_atari(argv[4]);
		ok = g && group_rename(g, n);
		free(n);
	} else if (!strcmp(cmd, "group-delete") && argc >= 4) {
		GROUP *g = find_group(argv[3]);
		ok = g && group_delete(g);
	} else if (!strcmp(cmd, "import") && argc >= 5) {
		int b = find_book(argv[3]), n = 0;
		ok = b >= 0 && contacts_import(b, argv[4], &n);
		printf("%d imported\n", n);
		if (!ok)
			fprintf(stderr, "import: %s\n", ab_err);
	} else if (!strcmp(cmd, "export") && argc >= 4) {
		CONTACT **l = malloc((npeople + 1) * sizeof(CONTACT *));
		int i, n = 0;
		for (i = 0; i < npeople; i++)
			if (!people[i]->group)
				l[n++] = people[i];
		ok = contacts_export(l, n, argv[3]);
		free(l);
	} else if (!strcmp(cmd, "photo") && argc >= 4) {
		/* "ok decoded TL=230,30,30 TR=... BL=... BR=..." */
		static const char *const st[] = { "none", "ok", "failed", "needs-net" };
		static const char *const hw[] = { "cache", "decoded", "fetched", "checked" };
		CONTACT *c = find(argv[3]);
		PHOTOIMG im;
		int how = 0, r;
		if (!c)
			return 1;
		r = photo_get(c, &im, !(argc >= 5 && !strcmp(argv[4], "offline")), &how);
		printf("%s %s", st[r], hw[how]);
		if (r == PH_FAILED)
			printf(" (%s)", photo_why_text(photo_why));
		if (r == PH_FAILED && photo_why_detail[0] && getenv("CARD_VERBOSE"))
			printf(" [%s]", photo_why_detail);
		if (r == PH_OK) {
			static const int at[4][2] = { { 8, 8 }, { 55, 8 }, { 8, 55 }, { 55, 55 } };
			static const char *const nm[4] = { "TL", "TR", "BL", "BR" };
			int k;
			for (k = 0; k < 4; k++) {
				unsigned short p = im.px[at[k][1] * PHOTO_N + at[k][0]];
				int r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
				/* the colour by name: JPEG moves the numbers a little */
				const char *name = r > 150 && g > 150 && b < 100 ? "yellow" : r > 150 && g < 100 && b < 100 ? "red" :
						   g > 150 && r < 100 && b < 100 ? "green" : b > 150 && r < 100 && g < 100 ? "blue" :
						   abs(r - g) < 40 && abs(g - b) < 40 ? "grey" : "other";
				printf(" %s=%s", nm[k], name);
				(void)r;
			}
		}
		printf("\n");
	} else if (!strcmp(cmd, "photo-ppm") && argc >= 5) {
		CONTACT *c = find(argv[3]);
		PHOTOIMG im;
		FILE *f;
		int i;
		if (!c || photo_get(c, &im, 1, 0) != PH_OK || !(f = fopen(argv[4], "wb")))
			return 1;
		fprintf(f, "P6 %d %d 255\n", PHOTO_N, PHOTO_N);
		for (i = 0; i < PHOTO_N * PHOTO_N; i++) {
			unsigned short p = im.px[i];
			fputc((p >> 11) << 3, f);
			fputc(((p >> 5) & 63) << 2, f);
			fputc((p & 31) << 3, f);
		}
		fclose(f);
	} else if (!strcmp(cmd, "photo-file") && argc >= 4) {
		CONTACT *c = find(argv[3]);
		char path[300];
		if (!c)
			return 1;
		photo_path(c, path, sizeof(path));
		printf("%s %s\n", path, pf_exists(path) ? "there" : "missing");
	} else if (!strcmp(cmd, "cache") && argc >= 4) {
		/* "partial photo=unknown", "whole photo=1A2B3C4D", "whole photo=none" */
		CONTACT *c = find(argv[3]);
		if (!c)
			return 1;
		printf("%s photo=", contact_partial(c) ? "partial" : "whole");
		if (!c->photo_key)
			printf("none\n");
		else if (c->photo_key == VC_KEY_UNKNOWN)
			printf("unknown\n");
		else
			printf("%08lX\n", c->photo_key);
	} else if (!strcmp(cmd, "photo-sync")) {
		int failed = 0, made;
		stop_after = argc >= 4 ? atoi(argv[3]) : -1;
		made = photo_sync_all(sync_cb, 0, &failed);
		printf("made %d failed %d\n", made, failed);
		if (made < 0)
			fprintf(stderr, "photo-sync: %s\n", ab_err);
	} else if (!strcmp(cmd, "prune")) {
		printf("%d removed\n", photo_prune());
	} else if (!strcmp(cmd, "people")) {
		char path[300], *t;
		long len;
		path_join(path, sizeof(path), dir, "CONTACTS.TXT");
		t = pf_load(path, &len);
		if (t) {
			out(t);
			free(t);
		}
	} else {
		fprintf(stderr, "unknown command %s\n", cmd);
		ok = 0;
	}
	store_save_settings();
	http_close_all();
	conn_log_flush();
	return ok ? 0 : 1;
}
