/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * main.c - Contacts: start-up as a desk accessory or as a program, the
 * event loop, refreshing, and the commands behind the toolbar's menu.
 *
 * As an accessory (ECONTACT.ACC in the boot drive's root) it waits in
 * the Desk menu: choosing "Contacts" opens the window over whatever
 * program is running. Its files live in X:\ECONTACT\ on the boot drive.
 * As a program (ECONTACT.PRG) it keeps them next to itself and ends when
 * the window closes.
 *
 * EMail and ECal find the people through C:\ECONTACT.INF (FOLDER=) and
 * CONTACTS.TXT in that folder; EContact hands an address to EMail with
 * the AV protocol's VA_START ("mailto:..."), and opens its window when
 * another program sends it AC_OPEN with any menu id (see
 * docs/INTEGRATION.md).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/conn.h"
#include "../src/tls.h"
#include "../src/http.h"
#include "../src/plat.h"
#include "../src/util.h"
#include "../src/bidi.h"
#include "../src/version.h"
#include "../src/photo.h"
#include "../src/carddav.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id, is_acc, ncolors, hebrew_kbd, quit_now;
extern short _is_acc;		/* crt0.S */

static short menu_id = -1;
static int data_ready;
static char workdir[160];	/* EContact's folder: address books, keys, certificates, log */
static char inf_file[40];	/* ECONTACT.INF, in the root of the boot drive */
static unsigned long last_refresh, last_minute;

#define VA_START 0x4711		/* the AV protocol: "open this" */
#define EC_OPEN  0x4543		/* 'EC': EMail or ECal asks for the window */

/* ---------------- small things ---------------- */

static int settings_dirty;
static unsigned long settings_at;

void settings_later(void)
{
	settings_dirty = 1;
	settings_at = pf_ms();
}

void settings_flush(void)
{
	if (settings_dirty) {
		settings_dirty = 0;
		store_save_settings();
	}
}

void closing_down(int quitting)
{
	char host[100], m[160];
	busy(1);
	if (settings_dirty) {
		status("Closing: saving your settings...");
		settings_flush();
	}
	if (quitting) {
		http_kept_host(host, sizeof(host));
		if (host[0]) {
			snprintf(m, sizeof(m), "Closing: ending the connection to %s...", host);
			status(m);
		}
		http_close_all();
	}
	if (opt.log) {
		status("Closing: writing ECONTACT.LOG...");
		conn_log_flush();
	}
	status(quitting ? "Closing EContact..." : "Closing the window...");
	busy(0);
}

void status(const char *msg)
{
	char s[200];
	if (win_h <= 0)
		return;
	if (hebrew_kbd) {
		snprintf(s, sizeof(s), "%s%sHebrew keyboard (F10)", msg, *msg ? "   " : "");
		msg = s;
	}
	win_status(msg);
}

void busy(int on)
{
	graf_mouse(on ? BUSYBEE : ARROW, 0);
}

static void idle(void)
{
	evnt_timer_(5);
}

void set_logging(void)
{
	conn_log_flush();
	if (opt.log)
		path_join(conn_logfile, sizeof(conn_logfile), workdir, "ECONTACT.LOG");
	else
		conn_logfile[0] = 0;
}

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
   Atari character codes; 0 = not remapped. From Claude ST and EMail. */
static const u8 hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

unsigned char key_char(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char ascii = KEY_ASCII(key);
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_CTRL | K_ALT)) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii;
}

static void updated_status(void);

void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	if (data_ready)
		updated_status();
}

/* Contacts that didn't fit in memory are said, once for each count:
   never just a shorter list */
static long acc_heap_bytes;	/* the accessory's fixed memory; 0 for the program */
static long acc_avail;		/* what was free when it took it */
static int warned_lost;

static void memory_check(void)
{
	int lost = contacts_lost();
	if (lost <= 0 || !data_ready) {
		warned_lost = lost > 0 ? warned_lost : 0;
		return;
	}
	if (lost == warned_lost)
		return;
	warned_lost = lost;
	if (is_acc && acc_heap_bytes) {
		/* about half a KB each, and room to sync them */
		long need = (acc_heap_bytes + (long)lost * 640L) / 1024L * 5L / 4L + 32L;
		long most = (acc_avail - 160L * 1024L) / 1024L;	/* leaving some for TOS and a program */
		if (need <= most)
			alert(1, "[1][%d contact%s can't be shown: EContact's memory is full. Give it more with HEAPKB=%ld in %s and restart the Atari.][ OK ]",
			      lost, lost == 1 ? "" : "s", need, inf_file);
		else
			alert(1, "[1][%d contact%s can't be shown: there isn't memory for all. The most EContact can have: HEAPKB=%ld in %s.][ OK ]",
			      lost, lost == 1 ? "" : "s", most > acc_heap_bytes / 1024L ? most : acc_heap_bytes / 1024L, inf_file);
	}
	else
		alert(1, "[1][%d contact%s can't be shown: there isn't enough free memory. Quit other programs and start EContact again.][ OK ]",
		      lost, lost == 1 ? "" : "s");
}

void after_change(void)
{
	list_invalidate();
	win_redraw(0);
	if (data_ready)
		updated_status();
	memory_check();
}

/* ---------------- Falcon mode ---------------- */

int falcon_mode_set(int on)
{
	if (on) {
		if (!tls_available) {
			alert(1, "[3][This is the ST edition: Falcon mode is in ECONTACT.ACC.][ OK ]");
			return 0;
		}
		if (get_cookie(0x5F435055L) < 20) {	/* _CPU */
			alert(1, "[3][Falcon mode needs a 68020 or better (Falcon, TT): this Atari is too slow for TLS.][ OK ]");
			return 0;
		}
		if (!pf_exists(conn_cacert)) {
			alert(1, "[3][Falcon mode needs the root certificates: put CACERT.PEM and ROOTS.DAT in the ECONTACT folder.][ OK ]");
			return 0;
		}
	}
	opt.falcon = (short)on;
	http_close_all();
	falcon_dsp_init();
	store_save_settings();
	return 1;
}

/* ---------------- refreshing ---------------- */

static void updated_status(void)
{
	char s[120], what[40];
	PFTIME t;
	int i, n = 0;
	pf_now(&t);
	for (i = 0; i < npeople; i++)
		n += !people[i]->group;
	if (nlisted != n || search_text[0] || src_kind != SRC_ALL)
		snprintf(what, sizeof(what), "%d of %d contacts", nlisted, n);
	else
		snprintf(what, sizeof(what), "%d contact%s", n, n == 1 ? "" : "s");
	if (contacts_lost() > 0)
		snprintf(what + strlen(what), sizeof(what) - strlen(what), ", %d not shown", contacts_lost());
	if (opt.offline)
		snprintf(s, sizeof(s), "%s   Working offline", what);
	else if (!naccts)
		snprintf(s, sizeof(s), "%s", what);
	else
		snprintf(s, sizeof(s), "%s   Updated %02d:%02d", what, t.hour, t.min);
	status(s);
}

void status_updated(void)
{
	if (data_ready)
		updated_status();
	else
		status("");
}

static void refresh(int force, int quiet)
{
	int r;
	if (!naccts)
		return;
	if (opt.offline) {
		if (!quiet)
			alert(1, "[1][EContact is working offline.|Switch it off in the menu (the button at the right of the toolbar) to refresh.][ OK ]");
		return;
	}
	busy(1);
	status("Updating...");
	r = sync_all(force);
	busy(0);
	last_refresh = pf_ms();
	after_change();
	card_photo_recheck();		/* new photos, changed ones, gone ones */
	if (r < 0) {
		status(ab_err);
		if (!quiet)
			alert(1, "[1][%s][ OK ]", ab_err);
	} else {
		updated_status();
	}
}

void cmd_refresh(int force)
{
	refresh(force, 0);
}

static void auto_refresh(void)
{
	unsigned long now = pf_ms();
	if (!naccts || opt.offline || opt.refresh <= 0)
		return;
	if (now - last_refresh > (unsigned long)opt.refresh * 60000UL)
		refresh(0, 1);
}

/* ---------------- commands ---------------- */

void cmd_new_contact(void)
{
	editor_new();
}

void cmd_delete_selected(void)
{
	CONTACT *c = sel_contact();
	char name[160];
	int ok, i, next = -1;
	if (!c)
		return;
	if (!book_writable(&books[c->book])) {
		alert(1, "[1][This contact is in an address book you can't change.][ OK ]");
		return;
	}
	vcard_display_name(c, 0, name, sizeof(name));
	if (alert(2, "[2][Are you sure you want to delete|\"%s\"?][Delete|Cancel]", name) != 1)
		return;
	/* the one after it is chosen next, as on the Mac */
	for (i = 0; i < nlisted; i++)
		if (listed[i] == c)
			next = i + 1 < nlisted ? i + 1 : i - 1;
	{
		char uid[80];
		short book = -1;
		if (next >= 0 && listed[next]->uid) {
			str_copy(uid, listed[next]->uid, sizeof(uid));
			book = listed[next]->book;
		} else {
			uid[0] = 0;
		}
		busy(1);
		ok = contact_delete(c);
		busy(0);
		if (!ok)
			alert(1, "[1][Couldn't delete it:|%s][ OK ]", ab_err);
		after_change();
		if (ok)
			select_contact(uid[0] ? contact_by_uid(uid, book) : 0);
	}
}

void cmd_new_group(int book)
{
	char name[60] = "";
	int k;
	if (book < 0)
		book = src_kind == SRC_BOOK ? src_idx : src_kind == SRC_GROUP && src_idx < ngroups ? groups[src_idx].book
										  : book_for_new();
	if (book < 0 || !book_writable(&books[book])) {
		alert(1, "[1][There's no address book to keep|a group in: add an account first.][ OK ]");
		return;
	}
	if (!dlg_ask("New Group", "Name:", name, 40) || !name[0])
		return;
	busy(1);
	k = group_new(book, name);
	busy(0);
	if (k < 0)
		alert(1, "[1][Couldn't make the group:|%s][ OK ]", ab_err);
	after_change();
	if (k >= 0) {
		extern void list_source(short kind, short idx);
		list_source(SRC_GROUP, (short)k);
		win_redraw(0);
		status("The new group is empty: right-click a contact > Add to Group...");
	}
}

void cmd_import(int book)
{
	char path[200], name[20] = "", dir[200];
	short button = 0;
	int n;
	if (book < 0)
		book = book_for_new();
	if (book < 0) {
		alert(1, "[1][Add an account first: the contacts go into one of its address books.][ OK ]");
		return;
	}
	snprintf(dir, sizeof(dir), "%s\\*.VCF", workdir);
	if (!fsel_exinput(dir, name, &button, "Import which .vcf file?") || !button || !name[0])
		return;
	{
		char *slash = strrchr(dir, '\\');
		if (slash)
			slash[1] = 0;
		snprintf(path, sizeof(path), "%s%s", dir, name);
	}
	busy(1);
	if (!contacts_import(book, path, &n)) {
		busy(0);
		alert(1, "[1][Import stopped: %s|(%d contacts imported)][ OK ]", ab_err, n);
	} else {
		busy(0);
		alert(1, "[1][%d contacts imported into|%s.][ OK ]", n, books[book].name);
	}
	after_change();
}

/* the chosen contact, or with nothing chosen all the list shows */
void cmd_export(void)
{
	char path[200], name[20], dir[200];
	short button = 0;
	CONTACT *c = sel_contact(), **list;
	int n = 0, i;
	short all;
	all = alert(1, "[2][Export as a vCard file:][Chosen card|All shown|Cancel]");
	if (all == 3)
		return;
	if (all == 1 && !c) {
		alert(1, "[1][No card is chosen.][ OK ]");
		return;
	}
	list = malloc((nlisted + 1) * sizeof(CONTACT *));
	if (!list)
		return;
	if (all == 1)
		list[n++] = c;
	else
		for (i = 0; i < nlisted; i++)
			list[n++] = listed[i];
	snprintf(dir, sizeof(dir), "%s\\*.VCF", workdir);
	str_copy(name, "CONTACTS.VCF", sizeof(name));
	if (all == 1) {
		/* the person's name, in 8.3 */
		char dn[64];
		int k, m = 0;
		vcard_display_name(c, 0, dn, sizeof(dn));
		for (k = 0; dn[k] && m < 8; k++)
			if ((dn[k] >= 'A' && dn[k] <= 'Z') || (dn[k] >= 'a' && dn[k] <= 'z') || (dn[k] >= '0' && dn[k] <= '9'))
				name[m++] = (char)(dn[k] >= 'a' ? dn[k] - 32 : dn[k]);
		if (m) {
			name[m] = 0;
			strcat(name, ".VCF");
		}
	}
	if (fsel_exinput(dir, name, &button, "Export to which file?") && button && name[0]) {
		char *slash = strrchr(dir, '\\');
		if (slash)
			slash[1] = 0;
		snprintf(path, sizeof(path), "%s%s", dir, name);
		busy(1);
		if (!contacts_export(list, n, path)) {
			busy(0);
			alert(1, "[1][Export failed: %s][ OK ]", ab_err);
		} else {
			busy(0);
			status("Exported.");
		}
	}
	free(list);
}

/* the GEM clipboard: SCRAP.TXT in its folder */
void cmd_copy(const char *text)
{
	char dir[160], path[200];
	dir[0] = 0;
	if (!scrp_read(dir) || !dir[0]) {
		snprintf(dir, sizeof(dir), "%c:\\CLIPBRD", 'A' + boot_drive());
		pf_mkdir(dir);
		scrp_write(dir);
	}
	{
		int n = (int)strlen(dir);
		while (n && dir[n - 1] == '\\')
			dir[--n] = 0;
		/* some programs leave a file name in it */
		if (n > 4 && dir[n - 4] == '.') {
			char *s = strrchr(dir, '\\');
			if (s)
				*s = 0;
		}
	}
	path_join(path, sizeof(path), dir, "SCRAP.TXT");
	if (pf_save(path, text, (long)strlen(text)) < 0)
		alert(1, "[1][Couldn't write the clipboard.][ OK ]");
	else
		status("Copied to the clipboard.");
}

/* "Maya Carter <maya@example.com>" for EMail's To: line */
void cmd_mail_to(const CONTACT *c, const char *email)
{
	char name[120], line[300];
	vcard_display_name(c, 0, name, sizeof(name));
	if (!name[0] || !strcmp(name, email))
		str_copy(line, email, sizeof(line));
	else if (strchr(name, ',') || strchr(name, '"'))
		snprintf(line, sizeof(line), "\"%s\" <%s>", name, email);
	else
		snprintf(line, sizeof(line), "%s <%s>", name, email);
	cmd_mail(line);
}

/* Writing to someone: EMail, when it runs, gets "mailto:..." with VA_START
   and opens a new message to them. The string must stay where it is until
   EMail has read it, and EMail must be allowed to read it (MiNT's memory
   protection): global memory where there is Mxalloc, else ours. */
void cmd_mail(const char *addr)
{
	static char local[1024];
	static char *buf;
	short id = appl_find("EMAIL   "), msg[8];
	char line[1024];
	snprintf(line, sizeof(line), "mailto:%s", addr);
	if (id < 0) {
		cmd_copy(addr);
		alert(1, "[1][EMail isn't running, so the address|is on the clipboard: paste it in|EMail's To: field (^V).][ OK ]");
		return;
	}
	if (!buf) {
		long m = (long)Mxalloc(sizeof(local), 0x2B);	/* global, TT RAM preferred */
		buf = (m > 0) ? (char *)m : local;
	}
	str_copy(buf, line, sizeof(local));
	memset(msg, 0, sizeof(msg));
	msg[0] = VA_START;
	msg[1] = ap_id;
	msg[2] = 0;
	msg[3] = (short)((unsigned long)buf >> 16);
	msg[4] = (short)(unsigned long)buf;
	appl_write(id, 16, msg);
	status("Sent to EMail.");
}

/* ---------------- Sync Contact Photos ---------------- */

typedef struct {
	unsigned long t0;
	int stopped;
} PSYNC;

static int photo_progress(void *ud, int done, int total, const char *name)
{
	PSYNC *p = ud;
	char l1[60], l2[60];
	unsigned long el = pf_ms() - p->t0;
	if (done >= total)
		str_copy(l1, "Done.", sizeof(l1));
	else if (name)
		snprintf(l1, sizeof(l1), "%d of %d: %s", done + 1, total, name[0] ? name : "No Name");
	else
		snprintf(l1, sizeof(l1), "%d of %d: fetching cards...", done + 1, total);
	/* the time left, from how long each has taken so far */
	if (done >= total) {
		l2[0] = 0;
	} else if (done < 2 || el < 4000UL) {
		str_copy(l2, "Working out the time left...", sizeof(l2));
	} else {
		unsigned long left = el / (unsigned long)done * (unsigned long)(total - done) / 1000UL;
		if (left >= 5400UL)
			snprintf(l2, sizeof(l2), "About %lu hours left", (left + 1800UL) / 3600UL);
		else if (left >= 90UL)
			snprintf(l2, sizeof(l2), "About %lu minutes left", (left + 30UL) / 60UL);
		else if (left >= 45UL)
			str_copy(l2, "About a minute left", sizeof(l2));
		else
			str_copy(l2, "Less than a minute left", sizeof(l2));
	}
	if (!prog_update(done, total, l1, l2)) {
		p->stopped = 1;
		return 0;
	}
	return 1;
}

/* every photo fetched and made now, rather than as each card is opened */
void cmd_sync_photos(void)
{
	PSYNC p;
	int failed = 0, made;
	if (!npeople)
		return;
	if (opt.offline &&
	    alert(1, "[2][EContact is working offline: only the photos already on this Atari are made.][Go on|Cancel]") != 1)
		return;
	memset(&p, 0, sizeof(p));
	p.t0 = pf_ms();
	prog_open("Sync Contact Photos");
	made = photo_sync_all(photo_progress, &p, &failed);
	prog_close();
	http_close_all();
	conn_log_flush();
	after_change();
	card_photo_recheck();
	if (made < 0)
		alert(1, "[1][The photos couldn't all be fetched: %s|What is done is kept: choose Sync Contact Photos again to go on.][ OK ]",
		      ab_err);
	else if (p.stopped)
		alert(1, "[1][Stopped: %d photo%s made. What is done is kept: choose Sync Contact Photos again to go on.][ OK ]",
		      made, made == 1 ? "" : "s");
	else if (!made && !failed)
		alert(1, "[1][Every contact's photo is here already.][ OK ]");
	else if (failed) {
		/* the two reasons most of them had; the log has each one */
		char why[120];
		int k, first = 0, second = 0;
		for (k = 1; k < PW_N; k++)
			if (photo_sync_why[k] > photo_sync_why[first])
				first = k;
		for (k = 1; k < PW_N; k++)
			if (k != first && photo_sync_why[k] > photo_sync_why[second])
				second = k;
		snprintf(why, sizeof(why), "%d %s", photo_sync_why[first], photo_why_text(first));
		if (second && photo_sync_why[second])
			snprintf(why + strlen(why), sizeof(why) - strlen(why), ", %d %s", photo_sync_why[second],
				 photo_why_text(second));
		alert(1, "[1][%d photo%s made, %d not: %s.%s][ OK ]", made, made == 1 ? "" : "s", failed, why,
		      opt.log ? " Each one is in the log." : " Write a log (in the menu) for each one.");
	}
	else
		alert(1, "[1][%d photo%s made.][ OK ]", made, made == 1 ? "" : "s");
	updated_status();
}

void cmd_menu(short x, short y)
{
	enum { M_ADD, M_NEW, M_GROUP, M_S1, M_IMP, M_EXP, M_PHOTOS, M_S2, M_SIDE, M_KBD, M_S5,
	       M_OFFL, M_FALC, M_LOG, M_S3, M_SET, M_FONT, M_KEYS, M_ABOUT, M_S4, M_QUIT, M_N };
	const char *items[M_N];
	short k, n = is_acc ? M_S4 : M_N;
	items[M_ADD] = "  Add account...";
	items[M_NEW] = "  New contact          ^N";
	items[M_GROUP] = "  New group            ^G";
	items[M_S1] = "-";
	items[M_IMP] = "  Import vCards...";
	items[M_EXP] = "  Export vCards...";
	items[M_PHOTOS] = "  Sync Contact Photos...";
	items[M_S2] = "-";
	items[M_SIDE] = opt.sidebar ? "\x08 Groups               ^S" : "  Groups               ^S";
	items[M_KBD] = hebrew_kbd ? "\x08 Hebrew keyboard     F10" : "  Hebrew keyboard     F10";
	items[M_S5] = "-";
	items[M_OFFL] = opt.offline ? "\x08 Work offline" : "  Work offline";
	items[M_FALC] = opt.falcon ? "\x08 Falcon mode (TLS)" : "  Falcon mode (TLS)";
	items[M_LOG] = opt.log ? "\x08 Write a log" : "  Write a log";
	items[M_S3] = "-";
	items[M_SET] = "  Settings...";
	items[M_FONT] = "  Font...";
	items[M_KEYS] = "  Keys...             Help";
	items[M_ABOUT] = "  About EContact...";
	items[M_S4] = "-";
	items[M_QUIT] = "  Quit                  ^Q";
	k = win_popup(x, y, items, n);
	switch (k) {
	case M_ADD: dlg_add_account(); break;
	case M_NEW: cmd_new_contact(); break;
	case M_GROUP: cmd_new_group(-1); break;
	case M_IMP: cmd_import(src_kind == SRC_BOOK ? src_idx : -1); break;
	case M_EXP: cmd_export(); break;
	case M_PHOTOS: cmd_sync_photos(); break;
	case M_SIDE:
		opt.sidebar = !opt.sidebar;
		settings_later();
		win_layout();
		win_redraw(0);
		break;
	case M_KBD: set_hebrew_kbd(!hebrew_kbd); break;
	case M_OFFL:
		opt.offline = !opt.offline;
		if (opt.offline)
			http_close_all();
		store_save_settings();
		updated_status();
		break;
	case M_FALC:
		if (falcon_mode_set(!opt.falcon)) {
			http_close_all();
			store_save_settings();
		}
		break;
	case M_LOG:
		opt.log = !opt.log;
		set_logging();
		store_save_settings();
		break;
	case M_SET: dlg_settings(); break;
	case M_FONT: font_menu(); break;
	case M_KEYS: dlg_keys(); break;
	case M_ABOUT: dlg_about(); break;
	case M_QUIT:
		quit_now = 1;
		win_close();
		break;
	}
}

/* ---------------- start-up ---------------- */

/* ECONTACT.INF lives in the root of the boot drive, where an accessory,
   EMail and ECal can always find it; FOLDER= in it says where the rest
   of EContact's files are. */

static void inf_folder(const char *inf, char *out, int size)
{
	/* read without malloc: the accessory calls this before its heap is taken */
	static char t[4096];
	char *k;
	int h = pf_open(inf, PF_READ);
	long n;
	out[0] = 0;
	if (h < 0)
		return;
	n = pf_read(h, t, sizeof(t) - 1);
	pf_close(h);
	t[n > 0 ? n : 0] = 0;
	for (k = t; (k = (char *)str_istr(k, "folder=")); k += 7)
		if (k == t || k[-1] == '\n') {
			char *e = k + 7 + strcspn(k + 7, "\r\n");
			*e = 0;
			str_copy(out, k + 7, size);
			break;
		}
	{
		int n2 = (int)strlen(out);
		while (n2 > 3 && (out[n2 - 1] == '\\' || out[n2 - 1] == ' '))
			out[--n2] = 0;
	}
}

static void find_workdir(void)
{
	char path[160], old[200];
	short drv = (short)Dgetdrv(), boot = boot_drive();
	snprintf(inf_file, sizeof(inf_file), "%c:\\ECONTACT.INF", 'A' + boot);
	if (is_acc) {
		inf_folder(inf_file, workdir, sizeof(workdir));
		if (!pf_exists(inf_file)) {
			/* settings in the folder instead (copied there, or written
			   by card-cli): taken to the root at the first opening */
			snprintf(workdir, sizeof(workdir), "%c:\\ECONTACT", 'A' + boot);
			path_join(old, sizeof(old), workdir, "ECONTACT.INF");
			if (!pf_exists(old))
				workdir[0] = 0;
		}
		if (workdir[0] && !pf_exists(workdir))
			workdir[0] = 0;		/* gone: ask at the first opening */
		return;
	}
	path[0] = 0;
	Dgetpath(path, 0);
	snprintf(workdir, sizeof(workdir), "%c:%s", 'A' + drv, path);
	if (strlen(workdir) > 3 && workdir[strlen(workdir) - 1] == '\\')
		workdir[strlen(workdir) - 1] = 0;
}

/* settings kept in the folder, and none in the root: copied there, the
   first time (as ECal does) */
static void move_inf(void)
{
	char old[200], *t;
	long len;
	if (pf_exists(inf_file) || !workdir[0])
		return;
	path_join(old, sizeof(old), workdir, "ECONTACT.INF");
	if (!strcasecmp(old, inf_file) || !(t = pf_load(old, &len)))
		return;
	pf_save(inf_file, t, len);
	free(t);
}

/* the accessory without its folder: the user shows it where it is */
static void locate_folder(void)
{
	char path[200], name[20] = "", why[200];
	short button = 0, boot = boot_drive();
	inf_folder(inf_file, why, sizeof(why));
	for (;;) {
		short k = why[0] ? alert(1, "[2][EContact's folder is not at|%s.|Show me where it is?][Locate...|%c:\\ECONTACT]",
					 why, 'A' + boot)
				 : alert(1, "[2][Where is EContact's folder,|with its address books, keys|and certificates?][Locate...|%c:\\ECONTACT]",
					 'A' + boot);
		char *e;
		if (k != 1)
			break;
		snprintf(path, sizeof(path), "%c:\\*.*", 'A' + boot);
		if (!fsel_exinput(path, name, &button, "EContact's folder: pick any file in it") || !button)
			continue;
		e = strrchr(path, '\\');
		if (e)
			*e = 0;
		if (strlen(path) == 2)
			strcat(path, "\\");
		if (pf_exists(path) || strlen(path) == 3) {
			str_copy(workdir, path, sizeof(workdir));
			return;
		}
	}
	snprintf(workdir, sizeof(workdir), "%c:\\ECONTACT", 'A' + boot);
	pf_mkdir(workdir);
}

/* how much of a server's answer to ask for at a time: an eighth of the
   memory (an answer is in memory twice while it is read, with the rest) */
static void answer_budget(long heap)
{
	long b = heap / 8;
	if (b < 24L * 1024)
		b = 24L * 1024;
	if (b > 400L * 1024)
		b = 400L * 1024;
	carddav_budget = b;
}

/* an accessory takes its memory once, now: later it would belong to
   whichever program is in front. HEAPKB=n in ECONTACT.INF overrides */
static void acc_heap(void)
{
	static char buf[2048];
	char inf[200];
	long avail = (long)Malloc(-1L), alt = (long)Mxalloc(-1L, 1), want, h, n;
	void *p;
	/* a Falcon or TT: 1.5 MB (more in TT RAM). An ST: half of what is
	   free (below), the other half left to the programs run after: about
	   250 KB on a 1 MB ST, 750 KB on 2 MB, 1.8 MB on 4 MB */
	want = tls_available && get_cookie(0x5F435055L) >= 30 ? 1536L * 1024 : 2048L * 1024;
	if (alt == -32 || alt < 0)
		alt = 0;
	if (alt >= 256L * 1024) {
		avail = alt;
		if (want < 4096L * 1024)
			want = 4096L * 1024;
	}
	str_copy(inf, inf_file, sizeof(inf));
	h = Fopen(inf, 0);
	if (h >= 0) {
		n = Fread((short)h, sizeof(buf) - 1, buf);
		Fclose((short)h);
		if (n > 0) {
			char *k;
			buf[n] = 0;
			k = (char *)str_istr(buf, "heapkb=");
			if (k && atol(k + 7) >= 64)
				want = atol(k + 7) * 1024;
		}
	}
	if (want > avail / 2)
		want = avail / 2;
	if (want < 96L * 1024)
		want = avail - 32L * 1024;
	if (want < 32L * 1024)
		return;
	p = Mxalloc(want, alt ? 1 : 3);
	if (alt && (long)p <= 0) {
		long st = (long)Malloc(-1L) / 2;
		if (want > st)
			want = st;
		p = 0;
	}
	if ((long)p == -32 || (long)p <= 0)
		p = Malloc(want);
	if ((long)p > 0) {
		heap_add(p, (size_t)want);
		acc_heap_bytes = want;
		acc_avail = avail;
		answer_budget(want);
	}
}

static int settings_ready;
static int sync_pending;

static void load_settings(void)
{
	if (!workdir[0])
		locate_folder();
	move_inf();
	store_init(workdir, inf_file);
	if (strcasecmp(opt.folder, workdir))
		store_save_settings();	/* FOLDER= as it is now */
	path_join(conn_cacert, sizeof(conn_cacert), workdir, "CACERT.PEM");
	path_join(tls_roots_cache, sizeof(tls_roots_cache), workdir, "ROOTS.DAT");
	path_join(tls_seed_path, sizeof(tls_seed_path), workdir, "SEED.DAT");
	set_logging();
	net_init();
	pf_idle = idle;
	ab_status = status;
	tls_note = status;
	falcon_dsp_init();
	if (store_plain_passwords && !store_lost_passwords)
		store_save_settings();
	hebrew_kbd = opt.hebrew;
	font_apply();
	{
		char m[100];
		void *h = malloc(16);
		snprintf(m, sizeof(m), "EContact %s: program at %08lx (%s RAM), memory at %08lx (%s RAM)",
			 app_version, (unsigned long)load_settings,
			 (unsigned long)load_settings >= 0x01000000UL ? "TT" : "ST",
			 (unsigned long)h, (unsigned long)h >= 0x01000000UL ? "TT" : "ST");
		free(h);
		conn_log("EContact", "--", m, (long)strlen(m));
		conn_log_flush();
	}
	settings_ready = 1;
}

static void load_contacts(void)
{
	busy(1);
	status("Reading your contacts...");
	book_load_all();
	list_invalidate();
	busy(0);
	data_ready = 1;
	if (store_lost_passwords)
		alert(1, "[1][ECONTACT.KEY is missing or new: type the passwords again (right-click an account).][ OK ]");
}

static void open_window(void)
{
	int first = win_h <= 0;
	if (!settings_ready)
		load_settings();
	win_open();
	if (win_h <= 0 || !first)
		return;
	if (!data_ready) {
		win_redraw(0);
		load_contacts();
	}
	/* "my card", or the first, chosen at the start as on the Mac */
	if (!sel_contact()) {
		CONTACT *me = contact_by_uid(opt.me, -1);
		if (me)
			select_contact(me);
		else
			list_step(1);
	}
	after_change();
	if (!naccts) {
		alert(1, "[1][Welcome to Contacts!||Let's add your iCloud or other contacts.][ OK ]");
		dlg_add_account();
	} else if (opt.sync_open && !opt.offline) {
		sync_pending = 1;
	}
}

static void wait_release(void)
{
	short mx, my, mb, ks;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_timer_(10);
	}
}

/* one click or two: only clicks where a double means something wait */
static short click_count(short mx, short my, short button)
{
	unsigned long t0 = pf_ms();
	short x, y, b, k, released = 0;
	if ((button & 2) || win_instant(mx, my))
		return 1;
	while (pf_ms() - t0 < 300UL) {
		graf_mkstate(&x, &y, &b, &k);
		if (!(b & 1))
			released = 1;
		else if (released)
			return (x - mx < 6 && mx - x < 6 && y - my < 6 && my - y < 6) ? 2 : 1;
		evnt_timer_(5);
	}
	return 1;
}

int main(void)
{
	short work_out[57], msg[8], d;
	AEVENT ev;

	is_acc = _is_acc;
	{
		extern int mem_unaligned_ok;
		mem_unaligned_ok = get_cookie(0x5F435055L) >= 20;	/* _CPU */
	}
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	if (is_acc)
		menu_id = menu_register(ap_id, "  Contacts");
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	ncolors = work_out[13];
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	if (ncolors <= 0 || planes > 8)
		ncolors = 256;
	vst_alignment(vdi_h, 0, 5);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	find_workdir();
	st_ram_init();
	dlg_after = win_frame_refresh;
	if (is_acc) {
		acc_heap();
	} else {
		long st = (long)Malloc(-1L), tt = (long)Mxalloc(-1L, 1);
		answer_budget(tt > st ? tt : st);
		open_window();
	}

	while (!quit_now) {
		short flags = MU_MESAG | MU_TIMER, which;
		unsigned long wait = sync_pending ? 150 : card_photo_pending() ? 100 : 1000;
		if (win_h > 0)
			flags |= MU_KEYBD | MU_BUTTON;
		which = evnt_multi_(flags, is_acc ? 0x102 : 0x101, 3, 0, wait, msg, &ev);
		if (which & MU_MESAG) {
			switch (msg[0]) {
			case AC_OPEN:
				/* from the Desk menu, or EMail and ECal asking for us */
				if (msg[4] == menu_id || msg[4] == -1)
					open_window();
				break;
			case EC_OPEN:
				open_window();
				break;
			case AC_CLOSE:
				if (msg[3] == menu_id) {
					win_h = -1;
					settings_flush();
				}
				break;
			case AP_TERM:
				if (!is_acc)
					quit_now = 1;
				win_close();
				break;
			default:
				win_message(msg);
			}
		}
		if ((which & MU_KEYBD) && win_h > 0) {
			short scan = KEY_SCAN(ev.kreturn);
			if (scan == 0x44)
				set_hebrew_kbd(!hebrew_kbd);
			else if (scan == 0x62)
				dlg_keys();
			else
				win_key(ev.kstate, ev.kreturn);
		}
		if ((which & MU_BUTTON) && win_h > 0) {
			short h = wind_find(ev.mx, ev.my), t, dd;
			wind_get(0, WF_TOP, &t, &dd, &dd, &dd);
			if (h == win_h) {
				if (h != t)
					wind_set(h, WF_TOP, 0, 0, 0, 0);
				else {
					short n = click_count(ev.mx, ev.my, ev.mbutton);
					if (is_acc && ev.breturn >= 2 && !(ev.mbutton & 2) && !win_instant(ev.mx, ev.my))
						n = 2;
					win_mouse(ev.mx, ev.my, ev.mbutton, n, ev.kstate);
				}
			}
			wait_release();
		}
		if ((which & MU_TIMER) && settings_dirty && pf_ms() - settings_at >= 3000UL)
			settings_flush();
		if ((which & MU_TIMER) && win_h > 0)
			card_photo_tick();
		if ((which & MU_TIMER) && sync_pending && win_h > 0) {
			sync_pending = 0;
			refresh(0, 0);
		}
		if ((which & MU_TIMER) && data_ready && win_h > 0) {
			unsigned long now = pf_ms();
			if (now - last_minute >= 30000UL) {
				last_minute = now;
				auto_refresh();
			}
		}
	}
	win_close();
	settings_flush();
	http_close_all();
	conn_log_flush();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
