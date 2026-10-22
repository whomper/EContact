/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialogs.c - adding and changing accounts, the settings, the
 * connection and the keys (after ECal's).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "util.h"
#include "conn.h"
#include "../src/http.h"
#include "../src/version.h"
#include "../src/plat.h"
#include "tls.h"

static void put(FIELD *f, short type, short flags, short x, short y, short w, const char *label)
{
	memset(f, 0, sizeof(*f));
	f->type = type;
	f->flags = flags;
	f->x = x;
	f->y = y;
	f->w = w;
	f->h = 1;
	f->label = label;
}

static void text(FIELD *f, short flags, short x, short y, short w, char *buf, short max)
{
	put(f, FT_TEXT, flags, x, y, w, 0);
	f->buf = buf;
	f->max = max;
}

static short run(const char *title, FIELD *f, short n, short w, short h, short focus)
{
	DLG d;
	memset(&d, 0, sizeof(d));
	d.title = title;
	d.f = f;
	d.n = n;
	d.w = w;
	d.h = h;
	d.focus = focus;
	return dlg_run(&d);
}

/* ---------------- accounts ---------------- */

static int connection_ready(const char *url)
{
	URL u;
	if (!url_parse(url, &u) || !u.tls || opt.falcon || opt.gate[0])
		return 1;
	if (alert(1, "[1][Contacts servers want a secure connection. Set up the Raspberry Pi gateway (ECal's serves EContact too), or Falcon mode, in Settings, Connection.][Settings|Cancel]") == 1)
		dlg_connection();
	return opt.falcon || opt.gate[0];
}

static int try_account(int a)
{
	int r;
	busy(1);
	r = sync_account(a, 1);
	busy(0);
	if (r < 0) {
		alert(1, "[1][%s: %s][ OK ]", accts[a].name, ab_err);
		return 0;
	}
	return 1;
}

static int edit_carddav(ACCOUNT *a, int is_new, int icloud)
{
	static char name[40], url[400], user[80], pass[80];
	enum { L1, F1, L2, F2, L3, F3, L4, F4, N1, N2, N3, BDEL, BCAN, BOK, N };
	FIELD f[N];
	short r, row = 1;
	str_copy(name, a->name, sizeof(name));
	str_copy(url, a->url, sizeof(url));
	str_copy(user, a->user, sizeof(user));
	str_copy(pass, a->pass, sizeof(pass));
	put(&f[L1], FT_LABEL, 0, 0, row, 0, "Name:");
	text(&f[F1], 0, 13, row, 30, name, sizeof(name));
	row++;
	put(&f[L2], FT_LABEL, icloud ? FF_DISABLED : 0, 0, row, 0, icloud ? "" : "Server:");
	text(&f[F2], FF_LTR | (icloud ? FF_DISABLED : 0), 13, row, 42, url, sizeof(url));
	if (icloud)
		f[F2].type = FT_LABEL, f[F2].label = "";
	row++;
	put(&f[L3], FT_LABEL, 0, 0, row, 0, icloud ? "Apple ID:" : "User name:");
	text(&f[F3], FF_LTR, 13, row, 42, user, sizeof(user));
	row++;
	put(&f[L4], FT_LABEL, 0, 0, row, 0, "Password:");
	text(&f[F4], FF_LTR, 13, row, 30, pass, sizeof(pass));
	f[F4].type = FT_PASS;
	row += 2;
	put(&f[N1], FT_LABEL, FF_LTR, 0, row, 0, icloud ? "iCloud needs an app-specific password: make one at"
						    : "e.g. https://carddav.fastmail.com/  https://cloud.example.org/remote.php/dav");
	put(&f[N2], FT_LABEL, FF_LTR, 0, row + 1, 0, icloud ? "account.apple.com > Sign-In and Security >"
							: "or the address of one address book. Google wants OAuth,");
	put(&f[N3], FT_LABEL, FF_LTR, 0, row + 2, 0, icloud ? "App-Specific Passwords, and type it here."
							: "which EContact can't do: export a .vcf and import it.");
	row += 4;
	put(&f[BDEL], FT_BUTTON, is_new ? FF_DISABLED : FF_DANGER, 0, row, 10, "Delete");
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, 34, row, 10, "Cancel");
	put(&f[BOK], FT_BUTTON, FF_DEFAULT, 46, row, 10, is_new ? "Sign In" : "OK");
	for (;;) {
		r = run(icloud ? "iCloud account" : "CardDAV account", f, N, 57, row + 1, is_new ? F3 : F1);
		if (r == BDEL)
			return -1;
		if (r != BOK)
			return 0;
		if (!url[0] && !icloud) {
			alert(1, "[1][Please type the server's address.][ OK ]");
			continue;
		}
		break;
	}
	str_copy(a->name, name[0] ? name : icloud ? "iCloud" : "Contacts", sizeof(a->name));
	if (!icloud) {
		if (strncasecmp(url, "http", 4)) {
			char tmp[420];
			snprintf(tmp, sizeof(tmp), "https://%s", url);
			str_copy(url, tmp, sizeof(url));
		}
		str_copy(a->url, url, sizeof(a->url));
	} else {
		str_copy(a->url, "https://contacts.icloud.com/", sizeof(a->url));
	}
	str_copy(a->user, user, sizeof(a->user));
	str_copy(a->pass, pass, sizeof(a->pass));
	memset(pass, 0, sizeof(pass));
	return 1;
}

void dlg_add_account(void)
{
	enum { L1, B1, B2, B3, BCAN, N };
	FIELD f[N];
	short r;
	put(&f[L1], FT_LABEL, 0, 0, 1, 0, "Where are your contacts?");
	put(&f[B1], FT_BUTTON, FF_DEFAULT, 0, 3, 40, "iCloud");
	put(&f[B2], FT_BUTTON, 0, 0, 4, 40, "Other CardDAV account...");
	put(&f[B3], FT_BUTTON, 0, 0, 5, 40, "On My Atari (no server)");
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, 30, 7, 10, "Cancel");
	r = run("Add an account", f, N, 40, 8, -1);
	if (r == B1 || r == B2) {
		ACCOUNT *a = acct_add(AK_CARDDAV);
		int ai;
		if (!a) {
			alert(1, "[1][There are too many accounts.][ OK ]");
			return;
		}
		ai = naccts - 1;
		if (r == B1) {
			str_copy(a->name, "iCloud", sizeof(a->name));
			str_copy(a->url, "https://contacts.icloud.com/", sizeof(a->url));
		}
		for (;;) {
			if (edit_carddav(a, 1, r == B1) <= 0 || !connection_ready(a->url)) {
				acct_delete(ai);
				store_save_settings();
				return;
			}
			if (try_account(ai))
				break;
			if (alert(2, "[2][Try again with other settings?][Edit|Cancel]") != 1) {
				acct_delete(ai);
				store_save_settings();
				after_change();
				return;
			}
		}
		store_save_settings();
		after_change();
	} else if (r == B3) {
		char name[64] = "Contacts";
		int i, a = -1;
		if (!dlg_ask("New address book on this Atari", "Name:", name, sizeof(name) - 1) || !name[0])
			return;
		for (i = 0; i < naccts; i++)
			if (accts[i].kind == AK_LOCAL)
				a = i;
		if (a < 0) {
			ACCOUNT *ac = acct_add(AK_LOCAL);
			if (!ac)
				return;
			str_copy(ac->name, "On My Atari", sizeof(ac->name));
			a = naccts - 1;
		}
		book_add(a, name);
		store_save_settings();
		after_change();
	}
}

int dlg_account(int a)
{
	ACCOUNT *ac = &accts[a];
	int r;
	if (ac->kind != AK_CARDDAV)
		return 0;
	r = edit_carddav(ac, 0, strstr(ac->url, "icloud.com") != 0);
	if (r < 0) {
		if (alert(2, "[2][Delete \"%s\" from this Atari?|(Nothing is deleted on the server.)][Delete|Cancel]", ac->name) == 1) {
			acct_delete(a);
			store_save_settings();
			return -1;
		}
		return 0;
	}
	if (r > 0) {
		http_close_all();
		store_save_settings();
		try_account(a);
	}
	return r;
}

/* ---------------- settings ----------------
 * As in ECal and EMail: a list of sections on the left (the chosen one
 * in a black bar) and that section's settings on the right. */

static const char *const refresh_items[] = {
	"  Never", "  Every 15 minutes", "  Every hour", "  Every day"
};
static const short refresh_min[] = { 0, 15, 60, 1440 };
static const char *const after_items[] = {
	"  No", "  2 minutes after", "  5 minutes after", "  15 minutes after"
};
static const short after_min[] = { 0, 2, 5, 15 };
static const char *const sort_items[] = { "  Last name", "  First name" };
static const char *const show_items[] = { "  First Last", "  Last, First" };
static const char *const heb_items[] = { "  Atari (standard)", "  ISO-8859-8 font", "  PC (code page 862)" };

enum { S_GENERAL = 1, S_REFRESH, S_CONN };
static const char *const section_names[] = { "General", "Refresh", "Connection" };

#define SIDE 12
#define CX (SIDE + 2)
#define VX (CX + 16)
#define SW (VX + 30)

static void settings(short first)
{
	enum { L1, F1, L2, F2, L3, F3, L4, F4, F5, L6, F6,
	       L11, F11, F12, L13, F13, N3,
	       C1, C2, N1, L7, C3, L8, C4, N2, C7,
	       BCAN, BOK, N };
	FIELD f[N];
	DLG d;
	static char gate[64], port[8];
	static char book_lab[MAXBOOK][70];
	static const char *book_items[MAXBOOK];
	short book_id[MAXBOOK], nb = 0;
	short r, i, row;
	int can_falcon = tls_available && get_cookie(0x5F435055L) >= 20;	/* _CPU: 68020 or better */
	str_copy(gate, opt.gate, sizeof(gate));
	snprintf(port, sizeof(port), "%u", opt.gateport);
	for (i = 0; i < nbooks && nb < MAXBOOK; i++)
		if (book_writable(&books[i])) {
			snprintf(book_lab[nb], sizeof(book_lab[nb]), "  %s (%s)", books[i].name, accts[books[i].acct].name);
			book_items[nb] = book_lab[nb];
			book_id[nb++] = books[i].id;
		}
	if (!nb) {
		book_items[0] = "  (no address book yet)";
		book_id[0] = -1;
		nb = 1;
	}

	/* General */
	row = 3;
	put(&f[L1], FT_LABEL, 0, CX, row, 0, "Sort by:");
	put(&f[F1], FT_POPUP, 0, VX, row, 16, 0);
	f[F1].items = sort_items;
	f[F1].nitems = 2;
	f[F1].val = opt.sort_first;
	row++;
	put(&f[L2], FT_LABEL, 0, CX, row, 0, "Show names as:");
	put(&f[F2], FT_POPUP, 0, VX, row, 16, 0);
	f[F2].items = show_items;
	f[F2].nitems = 2;
	f[F2].val = opt.last_first;
	row++;
	put(&f[L3], FT_LABEL, 0, CX, row, 0, "New contacts in:");
	put(&f[F3], FT_POPUP, 0, VX, row, 30, 0);
	f[F3].items = book_items;
	f[F3].nitems = nb;
	for (i = 0; i < nb; i++)
		if (book_id[i] == opt.default_book)
			f[F3].val = i;
	row += 2;
	put(&f[L4], FT_LABEL, 0, CX, row, 0, "Hebrew in font:");
	put(&f[F4], FT_POPUP, 0, VX, row, 24, 0);
	f[F4].items = heb_items;
	f[F4].nitems = 3;
	f[F4].val = opt.hebfont;
	row++;
	put(&f[F5], FT_CHECK, 0, VX, row, 32, "Start with Hebrew keyboard");
	f[F5].val = opt.hebrew;
	row++;
	row++;
	put(&f[L6], FT_LABEL, 0, CX, row, 0, "Photos:");
	put(&f[F6], FT_CHECK, 0, VX, row, 32, "Show contacts' photos");
	f[F6].val = opt.photos;
	for (i = L1; i <= F6; i++)
		f[i].page = S_GENERAL;

	/* Refresh */
	row = 3;
	put(&f[L11], FT_LABEL, 0, CX, row, 0, "Refresh all:");
	put(&f[F11], FT_POPUP, 0, VX, row, 22, 0);
	f[F11].items = refresh_items;
	f[F11].nitems = 4;
	for (i = 0; i < 4; i++)
		if (refresh_min[i] == opt.refresh)
			f[F11].val = i;
	row++;
	put(&f[F12], FT_CHECK, 0, VX, row, 30, "When the window opens");
	f[F12].val = opt.sync_open;
	row += 2;
	put(&f[L13], FT_LABEL, 0, CX, row, 0, "Once at start:");
	put(&f[F13], FT_POPUP, 0, VX, row, 22, 0);
	f[F13].items = after_items;
	f[F13].nitems = 4;
	for (i = 0; i < 4; i++)
		if (after_min[i] == opt.sync_after)
			f[F13].val = i;
	row++;
	put(&f[N3], FT_LABEL, FF_LTR, CX, row, 0, "The accessory: with its window closed too");
	for (i = L11; i <= N3; i++)
		f[i].page = S_REFRESH;

	/* Connection */
	row = 3;
	put(&f[C1], FT_CHECK, 0, CX, row, 44, "Falcon mode: TLS on this Atari");
	f[C1].val = opt.falcon;
	row++;
	put(&f[C2], FT_CHECK, 0, CX + 3, row, 40, "Check certificates on the DSP");
	f[C2].val = opt.dsp;
	row++;
	put(&f[N1], FT_LABEL, FF_LTR, CX + 3, row, 0, can_falcon ? "Needs CACERT.PEM, ROOTS.DAT in the folder"
							  : !tls_available ? "Not in the ST edition: ECONTACT.ACC has it"
							  : "Needs a 68030 (Falcon or TT)");
	row += 2;
	put(&f[L7], FT_LABEL, 0, CX, row, 0, "Pi gateway:");
	text(&f[C3], FF_LTR, CX + 12, row, 20, gate, sizeof(gate));
	put(&f[L8], FT_LABEL, 0, CX + 34, row, 0, "Port:");
	text(&f[C4], FF_LTR | FF_DIGITS, CX + 40, row, 6, port, 6);
	row++;
	put(&f[N2], FT_LABEL, FF_LTR, CX + 12, row, 0, "aera-gateway install dav");
	row++;
	put(&f[C7], FT_CHECK, 0, CX, row, 40, "Write a log (ECONTACT.LOG)");
	f[C7].val = opt.log;
	for (i = C1; i <= C7; i++)
		f[i].page = S_CONN;

	row = 11;
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, SW - 24, row, 10, "Cancel");
	put(&f[BOK], FT_BUTTON, FF_DEFAULT, SW - 12, row, 10, "OK");

	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.f = f;
	d.n = N;
	d.w = SW;
	d.h = row + 1;
	d.focus = -1;
	d.pages = section_names;
	d.npages = 3;
	d.side = SIDE;
	d.page = first;
	r = dlg_run(&d);
	if (r != BOK)
		return;

	if (opt.sort_first != f[F1].val || opt.last_first != f[F2].val) {
		opt.sort_first = f[F1].val;
		opt.last_first = f[F2].val;
		/* sorted again */
		for (i = 0; i < npeople; i++)
			vcard_sort_key(people[i], opt.sort_first);
		book_load_all();
	}
	if (book_id[f[F3].val] >= 0)
		opt.default_book = book_id[f[F3].val];
	opt.hebfont = f[F4].val;
	opt.photos = f[F6].val;
	opt.hebrew = f[F5].val;
	set_hebrew_kbd(opt.hebrew);
	opt.refresh = refresh_min[f[F11].val];
	opt.sync_open = f[F12].val;
	opt.sync_after = after_min[f[F13].val];
	str_copy(opt.gate, gate, sizeof(opt.gate));
	opt.gateport = (unsigned short)(atoi(port) > 0 ? atoi(port) : 8180);
	opt.dsp = f[C2].val;
	opt.log = f[C7].val;
	if (f[C1].val != opt.falcon)
		falcon_mode_set(f[C1].val);
	falcon_dsp_init();
	falcon_dsp_ec_init();
	http_close_all();
	store_save_settings();
	set_logging();
	after_change();
}

void dlg_settings(void)
{
	settings(S_GENERAL);
}

void dlg_connection(void)
{
	settings(S_CONN);
}

/* the keys, as a table: headings, the keys in one column and what they
   do in the next, two to a row */
void dlg_keys(void)
{
	static const struct {
		short row, col;		/* col 0: left pair, 1: right pair, -1: heading */
		const char *key, *does;
	} k[] = {
		{ 1, -1, "The list", 0 },
		{ 2, 0, "\x01 \x02", "the one above, below" },
		{ 2, 1, "Clr/Home", "the first; Shift: last" },
		{ 3, 0, "Return  ^E", "edit the card" },
		{ 3, 1, "Delete", "delete the card" },
		{ 4, 0, "^N", "a new contact" },
		{ 4, 1, "^G", "a new group" },
		{ 5, -1, "Search", 0 },
		{ 6, 0, "Typing", "searches as you type" },
		{ 6, 1, "^F", "a new search" },
		{ 7, 0, "Backspace", "a letter back" },
		{ 7, 1, "F10", "Hebrew keyboard" },
		{ 8, -1, "Commands", 0 },
		{ 9, 0, "^R", "refresh" },
		{ 9, 1, "Shift+^R", "everything again" },
		{ 10, 0, "\x01^S", "show the groups" },
		{ 10, 1, "^W  ^Q", "close the window" },
		{ 11, 0, "Double-click", "edit; an email: write" },
		{ 11, 1, "Right-click", "more choices" },
		{ 12, 0, "Help", "the guide, in a viewer" },
		{ 12, 1, "^C", "copy Name <email>" },
		{ 13, 0, "^V  ^X", "paste, cut the search" },
		{ 13, 1, "^C ^X ^V", "in a dialog's field" },
		{ 14, 0, "^,", "settings" },
	};
	enum { N = sizeof(k) / sizeof(k[0]) };
	FIELD f[2 * N + 1];
	short i, n = 0;
	for (i = 0; i < N; i++) {
		short kx = k[i].col == 1 ? 37 : 1, dx = k[i].col == 1 ? 49 : 14;
		if (k[i].col < 0) {
			put(&f[n++], FT_HEAD, 0, 0, k[i].row, 0, k[i].key);
			continue;
		}
		put(&f[n++], FT_LABEL, FF_LTR, kx, k[i].row, 0, k[i].key);
		put(&f[n++], FT_LABEL, FF_LTR, dx, k[i].row, 0, k[i].does);
	}
	put(&f[n++], FT_BUTTON, FF_DEFAULT | FF_CANCEL, 62, 15, 10, "OK");
	run("Keys", f, n, 73, 16, -1);
}
