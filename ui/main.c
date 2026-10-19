/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * main.c - Contacts: start-up as a desk accessory or as a program, the
 * event loop, refreshing, and the commands behind the toolbar's menu.
 *
 * As an accessory (ECONTACT.ACC in the boot drive's root) it waits in
 * the Desk menu: choosing "EContact" opens the window over whatever
 * program is running. Its files live in X:\ECONTACT\ on the boot drive.
 * As a program (ECONTACT.PRG) it keeps them next to itself and ends when
 * the window closes.
 *
 * EMail and ECal ask EContact about people with PEOPLE (answer.c): names
 * for addresses, people chosen in the window, word when they change.
 * EContact hands an address to EMail with the AV protocol's VA_START
 * ("mailto:..."), and takes VA_START itself: nothing opens its window,
 * .vcf files are imported (see docs/INTEGRATION.md).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "conn.h"
#include "secret.h"
#include "busy.h"
#include "tls.h"
#include "../src/http.h"
#include "../src/plat.h"
#include "util.h"
#include "bidi.h"
#include "../src/version.h"
#include "crash.h"
#include "help.h"
#include "av.h"
#include "aboutbox.h"
#include "../src/photo.h"
#include "../src/photopack.h"
#include "../src/carddav.h"
#include "clip.h"
#include "dd.h"
#include "mbar.h"
#include "twin.h"
#include "bgwork.h"
#include "people_ask.h"
#include "answer.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id, is_acc, ncolors, hebrew_kbd, quit_now;
extern short _is_acc;		/* crt0.S */

static short menu_id = -1;
static int data_ready;
static void files_left(void);
static void open_window(void);
static char workdir[160];	/* EContact's folder: address books, keys, certificates, log */
static char inf_file[40];	/* ECONTACT.INF, in the root of the boot drive */
static unsigned long last_refresh, last_minute;
static int synced_once;		/* refreshed since the start (SYNC_AFTER=) */

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

/* ---------------- choosing people for EMail or ECal (pick:) ---------------- */

short picking;			/* the window chooses for pick_to (answer.c) */
static int pick_opened;		/* the window was opened for it */

static void pick_start(void)
{
	int was_open = win_h > 0, i;
	pick_new = 0;
	if (pick_to < 0)
		return;
	picking = 0;		/* a pick before it: answer.c has cancelled it */
	if (was_open)
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
	else
		open_window();
	if (win_h <= 0) {
		answer_picked(0, 0);
		return;
	}
	pick_opened = !was_open;
	picking = 1;
	for (i = 0; i < npeople; i++)
		people[i]->mark = 0;
	str_copy(search_text, pick_search, sizeof(search_text));
	win_search_set();
	status_updated();
	win_layout();
	win_redraw(0);
}

/* Choose (choose = 1) or Cancel: the ticked people, else a group's members
   with an address (chosen on the left), else the one chosen. may_close:
   the window, opened for the pick, closes (not when it is closing) */
static void pick_finish(int choose, int may_close)
{
	CONTACT **l = 0;
	int n = 0, i;
	if (!picking)
		return;
	picking = 0;
	if (choose && (l = malloc((npeople + 1) * sizeof(CONTACT *))) != 0) {
		for (i = 0; i < npeople; i++)
			if (people[i]->mark && !people[i]->group)
				l[n++] = people[i];
		if (!n && !(src_kind == SRC_GROUP && src_idx < ngroups) && sel_contact() && !sel_contact()->group)
			l[n++] = sel_contact();
	}
	if (choose && !n && src_kind == SRC_GROUP && src_idx < ngroups)
		answer_picked_group(&groups[src_idx]);
	else
		answer_picked(l, n);
	free(l);
	for (i = 0; i < npeople; i++)
		people[i]->mark = 0;
	if (win_h > 0) {
		status_updated();
		win_layout();
		win_redraw(0);
	}
	/* opened for it: closed again, back to the program that asked */
	if (pick_opened && may_close && is_acc && win_h > 0) {
		pick_opened = 0;
		win_close();
	}
	pick_opened = 0;
}

void pick_end(int choose)
{
	pick_finish(choose, 1);
}

void closing_down(int quitting)
{
	char host[100], m[160];
	pick_finish(0, 0);		/* a pick open: cancelled */
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
	files_left();
}

void status(const char *msg)
{
	char s[200];
	crash_note(msg);		/* for CRASH.LOG, should EContact crash */
	if (win_h <= 0)
		return;
	if (hebrew_kbd) {
		snprintf(s, sizeof(s), "%s%sHebrew keyboard (F10)", msg, *msg ? "   " : "");
		msg = s;
	}
	win_status(msg);
}

/* the bee; while it shows, Esc or Undo stops the network (busy.h) */
void busy(int on)
{
	busy_bee(on);
}

/* a password that can't be encrypted goes into ECONTACT.INF as it is, and the
   user is told, once (the AERA guidelines, 7.2) */
static void warn_plain(const char *keyfile)
{
	alert(1, "[1][EContact can't write its key file, %s, so the passwords in ECONTACT.INF are kept unencrypted. "
		 "Is the disk locked or full?][ OK ]", keyfile);
}

/* the log's first line, whenever it is switched on: which version, and
   whether the program and its memory are in TT RAM (ST RAM is several
   times slower on a TT, and far slower behind a CT60; an accessory's
   program is always in ST RAM) */
void set_logging(void)
{
	int was = conn_logfile[0] != 0;
	conn_log_flush();
	if (opt.log)
		path_join(conn_logfile, sizeof(conn_logfile), workdir, "ECONTACT.LOG");
	else
		conn_logfile[0] = 0;
	if (conn_logfile[0] && !was) {
		char m[120];
		void *h = malloc(16);
		snprintf(m, sizeof(m), "EContact %s (%s): program at %08lx (%s RAM), memory at %08lx (%s RAM)",
			 app_version, is_acc ? "accessory" : "program", (unsigned long)set_logging,
			 (unsigned long)set_logging >= 0x01000000UL ? "TT" : "ST",
			 (unsigned long)h, (unsigned long)h >= 0x01000000UL ? "TT" : "ST");
		free(h);
		conn_log("EContact", "--", m, (long)strlen(m));
		if (*tos_clock())
			conn_log("EContact", "--", tos_clock(), (long)strlen(tos_clock()));
		conn_log_flush();
	}
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
	if (hebrew_kbd && !key_ctrl(kstate, key) && !(kstate & (K_LSHIFT | K_RSHIFT | K_ALT)) &&
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

/* What the accessory needs, as it measured it: the heap at its busiest
   since boot (common's heap_peak), and at least what it uses now with
   room for a job and a photo shown it may not have had yet; a tenth
   more for growth. MEMKB= in ECONTACT.INF, which the next start takes
   (acc_heap): no more memory than the contacts need (Erez, 19 Oct 2026) */
static void mem_note(void)
{
	long need, kb;
	if (!is_acc || !acc_heap_bytes || !data_ready)
		return;
	need = heap_use + 2 * ab_job_bytes + (opt.photos ? 50L * 1024 : 0L);
	if (heap_peak > need)
		need = heap_peak;
	need += (long)contacts_lost() * 640L;	/* those that didn't fit */
	kb = need / 1024 + need / 10240 + 16;
	if (kb > 32000)
		kb = 32000;
	{
		/* in the log: what it holds, its busiest, and what it took */
		char t[120];
		snprintf(t, sizeof(t), "memory: %ld KB in use, %ld KB at the most, of %ld KB; next start %ld KB",
			 heap_use / 1024, heap_peak / 1024, acc_heap_bytes / 1024, kb);
		conn_log("EContact", "--", t, (long)strlen(t));
	}
	if (opt.memkb && kb <= opt.memkb && kb > opt.memkb - opt.memkb / 8)
		return;			/* near enough: the file not written for it */
	opt.memkb = (short)kb;
	if (win_h > 0)
		settings_later();
	else
		store_unsaved = 1;	/* written when the window opens */
}

static void memory_check(void)
{
	int lost = contacts_lost();
	if (win_h <= 0)
		return;			/* said when the window opens: no alerts in the background */
	if (lost <= 0 || !data_ready) {
		warned_lost = lost > 0 ? warned_lost : 0;
		return;
	}
	if (lost == warned_lost)
		return;
	warned_lost = lost;
	if (is_acc && acc_heap_bytes) {
		long most = (acc_avail - 160L * 1024L) / 1024L;	/* leaving some for TOS and a program */
		mem_note();		/* what it takes at the next start */
		if (opt.heapkb <= 0 && opt.memkb <= most)
			alert(1, "[1][%d contact%s can't be shown: EContact's memory is full. It takes more the next time the Atari starts.][ OK ]",
			      lost, lost == 1 ? "" : "s");
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

/* how many files a sync opened, in the log: TOS's internal pool is
   small (pfkeep.c), and the accessory opens its files again after every
   event call (pf_park) */
static void opens_note(int start)
{
	static long at;
	char t[60];
	if (start) {
		at = pf_opens;
		conn_log_hold(1);	/* the job's log written once, at its end (6.5) */
		return;
	}
	snprintf(t, sizeof(t), "%ld files opened", pf_opens - at);
	conn_log("EContact", "--", t, (long)strlen(t));
	conn_log_hold(0);
	mem_note();
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
			alert(1, "[3][Falcon mode needs the root certificates: put CACERT.PEM and ROOTS.DAT in EContact's folder.][ OK ]");
			return 0;
		}
	}
	opt.falcon = (short)on;
	http_close_all();
	falcon_dsp_init();
	falcon_dsp_ec_init();
	store_save_settings();
	return 1;
}

/* ---------------- refreshing ---------------- */

/* when the contacts were last read from the servers (or from the disk,
   until then): the status line says so whatever else changes it */
static PFTIME updated_at;
static int updated_known;

static void updated_now(void)
{
	pf_now(&updated_at);
	updated_known = 1;
}

static void updated_status(void)
{
	char s[120], what[40];
	PFTIME t;
	int i, n = 0;
	if (!updated_known)
		updated_now();
	t = updated_at;
	for (i = 0; i < npeople; i++)
		n += !people[i]->group;
	if (nlisted != n || search_text[0] || src_kind != SRC_ALL)
		snprintf(what, sizeof(what), "%d of %d contacts", nlisted, n);
	else
		snprintf(what, sizeof(what), "%d contact%s", n, n == 1 ? "" : "s");
	if (contacts_lost() > 0)
		snprintf(what + strlen(what), sizeof(what) - strlen(what), ", %d not shown", contacts_lost());
	if (picking)
		snprintf(s, sizeof(s), "%s   Choose for %s: tick people or a group, then Choose", what, pick_asker);
	else if (opt.offline)
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
	pf_keep(1);		/* the cache and the log stay open while it runs */
	opens_note(1);
	r = sync_all(force);
	pf_keep(0);
	opens_note(0);
	busy(0);
	last_refresh = pf_ms();
	synced_once = 1;
	if (r >= 0)
		updated_now();
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

static void import_file(int book, const char *path);

void cmd_import(int book)
{
	char path[200], name[20] = "", dir[200];
	short button = 0;
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
	import_file(book, path);
}

/* the contacts in a .vcf file into the address book */
static void import_file(int book, const char *path)
{
	int n;
	busy(1);
	if (!contacts_import(book, path, &n)) {
		busy(0);
		alert(1, "[1][Import stopped: %s|(%d contacts imported)][ OK ]", ab_err, n);
	} else {
		busy(0);
		alert(1, "[1][%d contact%s imported into|%s.][ OK ]", n, n == 1 ? "" : "s", books[book].name);
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

/* the GEM clipboard (common/ui/clip.c): SCRAP.TXT in its folder, the
   other SCRAP.* gone, other programs told */
void cmd_copy(const char *text)
{
	if (!clip_put(text, (long)strlen(text)))
		alert(1, "[1][Couldn't write the clipboard.][ OK ]");
	else
		status("Copied to the clipboard.");
}

/* a contact as EMail's To: line takes it: "Maya Carter <maya@example.com>" */
void cmd_copy_contact(const CONTACT *c)
{
	char name[160], t[300];
	vcard_display_name(c, 0, name, sizeof(name));
	if (c->nemail)
		snprintf(t, sizeof(t), "%s <%s>", name, c->email[0].value);
	else
		str_copy(t, name, sizeof(t));
	cmd_copy(t);
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

/* The Help key: the user guide, ECONTACT.HYP, in EHypView or ST-Guide
   (common/ui/help.c, which keeps the name in memory the viewer may
   read); without a viewer, an alert says how to get one and offers the
   keys */
static void help_key_pressed(void)
{
	if (help_key(opt.workdir, "ECONTACT.HYP", 0, "Keys"))
		dlg_keys();
}

/* Writing to someone: EMail, when it runs, gets "mailto:..." with VA_START
   and opens a new message to them (av_send, common/ui/help.c: the string
   stays where EMail may read it). */
void cmd_mail(const char *addr)
{
	char line[1024];
	snprintf(line, sizeof(line), "mailto:%s", addr);
	if (!av_send("EMAIL   ", line)) {
		cmd_copy(addr);
		alert(1, "[1][EMail isn't running, so the address|is on the clipboard: paste it in|EMail's To: field (^V).][ OK ]");
		return;
	}
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
	    alert(2, "[2][EContact is working offline: only the photos already on this Atari are made.][Go on|Cancel]") != 1)
		return;
	memset(&p, 0, sizeof(p));
	p.t0 = pf_ms();
	prog_open("Sync Contact Photos");
	pf_keep(1);		/* the cache and the log stay open while it runs */
	opens_note(1);
	made = photo_sync_all(photo_progress, &p, &failed);
	pf_keep(0);
	opens_note(0);
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

/* the commands of the menu bar (the program) and the toolbar's menu */
enum { C_NONE, C_ABOUT, C_NEW, C_GROUP, C_ADD, C_IMP, C_EXP, C_PHOTOS, C_REFRESH, C_REFRESH_ALL,
       C_QUIT, C_COPY, C_EDIT, C_DELETE, C_FIND, C_SIDE, C_SET, C_FONT, C_OFFL, C_FALC, C_LOG,
       C_KBD, C_HELP, C_KEYS };

static const MBAR_ITEM m_desk[] = {
	{ "About EContact...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "New contact", "^N", C_NEW },
	{ "New group", "^G", C_GROUP },
	{ "Add account...", 0, C_ADD },
	{ "-", 0, 0 },
	{ "Import vCards...", 0, C_IMP },
	{ "Export vCards...", 0, C_EXP },
	{ "Sync Contact Photos...", 0, C_PHOTOS },
	{ "-", 0, 0 },
	{ "Refresh", "^R", C_REFRESH },
	{ "Refresh everything", "\x01^R", C_REFRESH_ALL },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Copy contact", "^C", C_COPY },
	{ "-", 0, 0 },
	{ "Edit contact...", "^E", C_EDIT },
	{ "Delete...", "Del", C_DELETE },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_view[] = {
	{ "Find", "^F", C_FIND },
	{ "-", 0, 0 },
	{ "Groups", "\x01^S", C_SIDE },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_opts[] = {
	{ "Settings...", "^,", C_SET },
	{ "Font...", 0, C_FONT },
	{ "-", 0, 0 },
	{ "Work offline", 0, C_OFFL },
	{ "Falcon mode (TLS)", 0, C_FALC },
	{ "Write a log", 0, C_LOG },
	{ "Hebrew keyboard", "F10", C_KBD },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_help[] = {
	{ "EContact help", "Help", C_HELP },
	{ "Keys...", 0, C_KEYS },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " EContact ", m_desk },
	{ " File ", m_file },
	{ " Edit ", m_edit },
	{ " View ", m_view },
	{ " Options ", m_opts },
	{ " Help ", m_help },
	{ 0, 0 }
};
static MBAR mb;
static int has_menu;

static void check(int cmd, int on)
{
	short o = mbar_object(&mb, cmd);
	if (o > 0)
		menu_icheck(mb.tree, o, on ? 1 : 0);
}

static void enable(int cmd, int on)
{
	short o = mbar_object(&mb, cmd);
	if (o > 0)
		menu_ienable(mb.tree, o, on ? 1 : 0);
}

/* the menu bar's ticks and greyed items, as things are now */
static void menu_update(void)
{
	if (!has_menu)
		return;
	enable(C_COPY, sel_contact() != 0 || search_text[0]);
	enable(C_EDIT, sel_contact() != 0);
	enable(C_DELETE, sel_contact() != 0);
	check(C_SIDE, opt.sidebar);
	check(C_OFFL, opt.offline);
	check(C_FALC, opt.falcon);
	check(C_LOG, opt.log);
	check(C_KBD, hebrew_kbd);
}

static void help_key_pressed(void);

static void command(int c)
{
	switch (c) {
	case C_ABOUT: dlg_about(); break;
	case C_NEW: cmd_new_contact(); break;
	case C_GROUP: cmd_new_group(-1); break;
	case C_ADD: dlg_add_account(); break;
	case C_IMP: cmd_import(src_kind == SRC_BOOK ? src_idx : -1); break;
	case C_EXP: cmd_export(); break;
	case C_PHOTOS: cmd_sync_photos(); break;
	case C_REFRESH: cmd_refresh(0); break;
	case C_REFRESH_ALL: cmd_refresh(1); break;
	case C_COPY:
		if (sel_contact())
			cmd_copy_contact(sel_contact());
		else if (search_text[0])
			cmd_copy(search_text);
		break;
	case C_EDIT:
		if (sel_contact())
			editor_open(sel_contact());
		break;
	case C_DELETE: cmd_delete_selected(); break;
	case C_FIND: win_find(); break;
	case C_SIDE:
		opt.sidebar = !opt.sidebar;
		settings_later();
		win_layout();
		win_redraw(0);
		break;
	case C_KBD: set_hebrew_kbd(!hebrew_kbd); break;
	case C_OFFL:
		opt.offline = !opt.offline;
		if (opt.offline)
			http_close_all();
		store_save_settings();
		updated_status();
		break;
	case C_FALC:
		if (falcon_mode_set(!opt.falcon)) {
			http_close_all();
			store_save_settings();
		}
		break;
	case C_LOG:
		opt.log = !opt.log;
		set_logging();
		store_save_settings();
		break;
	case C_SET: dlg_settings(); break;
	case C_FONT: font_menu(); break;
	case C_HELP: help_key_pressed(); break;
	case C_KEYS: dlg_keys(); break;
	case C_QUIT:
		quit_now = 1;
		win_close();
		break;
	}
	menu_update();
}

/* the toolbar's menu: the menu bar's commands, for the accessory, which
   has none */
void cmd_menu(short x, short y)
{
	static const struct {
		const char *text;
		short cmd;
	} m[] = {
		{ "  Add account...", C_ADD },
		{ "  New contact          ^N", C_NEW },
		{ "  New group            ^G", C_GROUP },
		{ "-", 0 },
		{ "  Import vCards...", C_IMP },
		{ "  Export vCards...", C_EXP },
		{ "  Sync Contact Photos...", C_PHOTOS },
		{ "-", 0 },
		{ "  Groups              \x01^S", C_SIDE },
		{ "  Hebrew keyboard     F10", C_KBD },
		{ "-", 0 },
		{ "  Work offline", C_OFFL },
		{ "  Falcon mode (TLS)", C_FALC },
		{ "  Write a log", C_LOG },
		{ "-", 0 },
		{ "  Settings...          ^,", C_SET },
		{ "  Font...", C_FONT },
		{ "  Keys...", C_KEYS },
		{ "  About EContact...", C_ABOUT },
		{ "-", 0 },
		{ "  Quit                  ^Q", C_QUIT },
	};
	enum { N = sizeof(m) / sizeof(m[0]) };
	char ticked[N][40];
	const char *items[N];
	short i, k, n = is_acc ? N - 2 : N;
	for (i = 0; i < N; i++) {
		int on = (m[i].cmd == C_SIDE && opt.sidebar) || (m[i].cmd == C_KBD && hebrew_kbd) ||
			 (m[i].cmd == C_OFFL && opt.offline) || (m[i].cmd == C_FALC && opt.falcon) ||
			 (m[i].cmd == C_LOG && opt.log);
		items[i] = m[i].text;
		if (on) {
			str_copy(ticked[i], m[i].text, sizeof(ticked[i]));
			ticked[i][0] = '\x08';		/* the tick */
			items[i] = ticked[i];
		}
	}
	k = win_popup(x, y, items, n);
	if (k >= 0 && k < N)
		command(m[k].cmd);
}

/* ---------------- start-up ---------------- */

/* ECONTACT.INF lives in the root of the boot drive, where an accessory,
   EMail and ECal can always find it; FOLDER= in it says where the rest
   of EContact's files are. */

/* the start of ECONTACT.INF, read once (inf_read) without malloc: the
   accessory reads it before its heap is taken. -1: there is none */
static char inf_t[8192];
static long inf_n = -1;

static void inf_read(const char *inf)
{
	int h = pf_open(inf, PF_READ);
	long n;
	inf_n = -1;
	inf_t[0] = 0;
	if (h < 0)
		return;
	n = pf_read(h, inf_t, sizeof(inf_t) - 1);
	pf_close(h);
	inf_n = n > 0 ? n : 0;
	inf_t[inf_n] = 0;
}

/* KEY= ("folder=", "start=") in what inf_read read */
static void inf_value(const char *key, char *out, int size)
{
	const char *k;
	int kl = (int)strlen(key);
	out[0] = 0;
	for (k = inf_t; inf_n > 0 && (k = str_istr(k, key)); k += kl)
		if (k == inf_t || k[-1] == '\n') {
			int n = (int)strcspn(k + kl, "\r\n");
			if (n >= size)
				n = size - 1;
			memcpy(out, k + kl, n);
			out[n] = 0;
			break;
		}
}

static void inf_folder(char *out, int size)
{
	inf_value("folder=", out, size);
	{
		int n2 = (int)strlen(out);
		while (n2 > 3 && (out[n2 - 1] == '\\' || out[n2 - 1] == ' '))
			out[--n2] = 0;
	}
}

static char boot_start[12];	/* START= as ECONTACT.INF had it at boot */
static char boot_font[8];	/* FONT_ID=: the GDOS fonts loaded at boot or never */

static void find_workdir(void)
{
	char path[160], old[200];
	short drv = (short)Dgetdrv(), boot = boot_drive();
	snprintf(inf_file, sizeof(inf_file), "%c:\\ECONTACT.INF", 'A' + boot);
	if (is_acc) {
		inf_read(inf_file);	/* once: FOLDER=, START=, FONT_ID=, HEAPKB= */
		inf_folder(workdir, sizeof(workdir));
		inf_value("start=", boot_start, sizeof(boot_start));
		inf_value("font_id=", boot_font, sizeof(boot_font));
		if (inf_n < 0) {
			/* settings in the folder instead (copied there, or written
			   by card-cli): taken to the root at the first opening */
			snprintf(workdir, sizeof(workdir), "%c:\\ECONTACT", 'A' + boot);
			path_join(old, sizeof(old), workdir, "ECONTACT.INF");
			if (!pf_exists(old))
				workdir[0] = 0;
		}
		if (workdir[0] && strlen(workdir) > 3 && !pf_exists(workdir))
			workdir[0] = 0;		/* gone: ask at the first opening (a
						   drive's root has no entry to find) */
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

/* the accessory without its folder: the user shows it where it is. 0:
   Cancel (the window doesn't open) */
static int locate_folder(void)
{
	char path[200], name[20] = "", why[200];
	short button = 0, boot = boot_drive();
	inf_read(inf_file);
	inf_folder(why, sizeof(why));
	for (;;) {
		/* the path in the text: a button holds 10 characters (6.2) */
		short k = why[0] ? alert(1, "[2][EContact's folder is not at|%s.|Show me where it is, or use|%c:\\ECONTACT?][Locate...|Use %c:|Cancel]",
					 why, 'A' + boot, 'A' + boot)
				 : alert(1, "[2][Where is EContact's folder,|with its address books, keys|and certificates? Or use|%c:\\ECONTACT?][Locate...|Use %c:|Cancel]",
					 'A' + boot, 'A' + boot);
		char *e;
		if (k == 3 || k <= 0)
			return 0;
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
			return 1;
		}
	}
	snprintf(workdir, sizeof(workdir), "%c:\\ECONTACT", 'A' + boot);
	pf_mkdir(workdir);
	return 1;
}

/* Bulk work's memory (the guidelines, 6.5), of a fixed size: what a
   refresh or Sync Contact Photos gathers before it writes, each file's
   write buffer, the photo pack's, and how much of a server's answer to
   ask for at a time (an answer is in memory twice while it is read).
   By the memory free at boot: free, for the accessory (0: the program,
   whose memory grows) */
static void job_memory(long free)
{
	long job = !free ? 256L * 1024 : free >= 3072L * 1024 ? 64L * 1024 : free >= 1536L * 1024 ? 32L * 1024
												  : 24L * 1024;
	ab_read_bytes = get_cookie(0x5F435055L) >= 30 ? 32768L : 16384L;	/* _CPU */
	ab_job_bytes = job;
	/* a 1 MB ST: little each, so a sync's answers still fit */
	ab_out_max = job >= 32L * 1024 ? job / 2 : 8L * 1024;
	pack_buf_max = job >= 32L * 1024 ? job : ab_out_max;	/* 8 KB a picture */
	carddav_budget = job / 2 > 24L * 1024 ? job / 2 : 24L * 1024;
}

/* an accessory takes its memory once, now: later it would belong to
   whichever program is in front. HEAPKB=n in ECONTACT.INF overrides */
static void acc_heap(void)
{
	char v[12];
	long avail = (long)Malloc(-1L), alt = (long)Mxalloc(-1L, 1), want, kb;
	void *p;
	if (alt == -32 || alt < 0)
		alt = 0;
	if (alt >= 256L * 1024)
		avail = alt;		/* TT RAM, when there is some */
	/* (find_workdir read the file) HEAPKB= chosen; else MEMKB=, what it
	   measured it needed the last time (mem_note) */
	inf_value("heapkb=", v, sizeof(v));
	kb = atol(v);
	if (kb < 64) {
		inf_value("memkb=", v, sizeof(v));
		kb = atol(v);
		if (kb < 32)
			kb = 0;
	}
	if (kb) {
		want = kb * 1024;
	} else {
		/* not known yet: a Falcon or TT 1.5 MB (4 MB in TT RAM), an ST
		   half of what is free (below), the other half left to the
		   programs run after: about 250 KB on a 1 MB ST, 750 KB on
		   2 MB, 1.8 MB on 4 MB */
		want = tls_available && get_cookie(0x5F435055L) >= 30 ? 1536L * 1024 : 2048L * 1024;
		if (alt >= 256L * 1024 && want < 4096L * 1024)
			want = 4096L * 1024;
	}
	if (want < 64L * 1024)
		want = 64L * 1024;
	if (want > avail / 2) {
		want = avail / 2;	/* the rest left to the programs run after */
		if (want < 96L * 1024)
			want = avail - 32L * 1024;
	}
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
		job_memory(avail);
	}
}

static int settings_ready;
static int fonts_ready;
static int sync_pending;

/* said when the window opens, not in the background */
static int lost_untold, plain_untold;
static char plain_key[120];

static void warn_later(const char *keyfile)
{
	str_copy(plain_key, keyfile, sizeof(plain_key));
	plain_untold = 1;
}

static void tell_untold(void)
{
	if (plain_untold) {
		plain_untold = 0;
		warn_plain(plain_key);
	}
	if (lost_untold) {
		lost_untold = 0;
		alert(1, "[1][ECONTACT.KEY is missing or new: type the passwords again (right-click an account).][ OK ]");
	}
}

/* quiet: the accessory in the background, its window closed: the fonts
   wait for the window, and what would be an alert is said then */
static int store_ready;		/* ECONTACT.INF read (the first half) */

static int load_store(int quiet)
{
	if (!workdir[0] && !locate_folder())
		return 0;		/* (only when the window is to open: quiet has the folder) */
	secret_warn = quiet ? warn_later : warn_plain;	/* before anything is saved */
	move_inf();
	store_init(workdir, inf_file);
	store_ready = 1;
	return 1;
}

/* 0: there is no folder (the user cancelled locating it) */
static int load_settings(int quiet)
{
	if (!store_ready && !load_store(quiet))
		return 0;
	store_ready = 0;
	secret_warn = quiet ? warn_later : warn_plain;
	if (strcasecmp(opt.folder, workdir))
		store_save_settings();	/* FOLDER= as it is now */
	path_join(conn_cacert, sizeof(conn_cacert), workdir, "CACERT.PEM");
	path_join(tls_roots_cache, sizeof(tls_roots_cache), workdir, "ROOTS.DAT");
	path_join(tls_seed_path, sizeof(tls_seed_path), workdir, "SEED.DAT");
	path_join(tls_sigs_path, sizeof(tls_sigs_path), workdir, "SIGS.DAT");
	tls_file_stamp = pf_stamp;	/* ROOTS.DAT told by CACERT.PEM's size and date */
	tls_file_info = pf_info;	/* both from one look (6.5) */
	tls_read_at = pf_read_at;	/* CACERT.PEM never open across pf_idle (2.2) */
	pf_size_open = pf_size_of;	/* pf_load: one look a file */
	set_logging();
	net_init();
	pf_idle = busy_idle;
	ab_status = status;
	tls_note = status;
	falcon_dsp_init();
	falcon_dsp_ec_init();
	if (store_plain_passwords && !store_lost_passwords)
		store_save_settings();
	secret_warn = warn_plain;
	hebrew_kbd = opt.hebrew;
	fonts_ready = 0;
	if (!quiet) {
		font_apply();
		fonts_ready = 1;
	}
	settings_ready = 1;
	return 1;
}

/* The accessory's files as it left them. On single TOS its memory
   outlives its window: ECONTACT.PRG may have run since and changed
   ECONTACT.INF and the caches, and what the accessory holds would then
   be written over them (the cards copied from the wrong places). So it
   reads them all again, as at the first opening. */
static unsigned long files_stamp;
static int files_known;

/* the accessory reading its books in the background, after START=
   (common/ui/bgwork.c): the files as they were when it began */
static unsigned long load_stamp;
static int load_quiet;		/* the settings were read in the background */

static void loaded(void)
{
	list_invalidate();
	data_ready = 1;
	lost_untold = store_lost_passwords;
	if (is_acc && load_quiet) {
		/* the files it read are those of when it began */
		files_stamp = load_stamp;
		files_known = 1;
	}
	load_quiet = 0;
	mem_note();
}

static void load_contacts(void)
{
	busy(1);
	status("Reading your contacts...");
	if (!books_loading())
		books_load_begin();
	books_load_run(0);		/* what is left of a load in the background, or all */
	busy(0);
	loaded();
}

/* The accessory brings its books up to date with its window closed too
   (REFRESH=, SYNC_AFTER=): one exchange with a server a step (abook.c
   quiet_sync), every message that comes meanwhile held for the main
   loop, and the sync stopped by it */
static short held[16][8];
static int nheld;
static void (*was_idle)(void);
static long was_budget;

/* While the network works in the window (busy.c), messages are read
   too: PEOPLE is answered at once, the rest waits for the main loop */
static void busy_hold(const short *msg)
{
	if (nheld < 16)
		memcpy(held[nheld++], msg, 16);
}

/* PEOPLE (answer.c): the accessory, while ECONTACT.PRG runs, hands it
   on, as twin_pass does VA_START; the answer goes from the program to
   the asker (msg[1] stays the asker's) */
static int people_pass(const char *s, short from)
{
	static char pass[2][1024];
	static int k;
	short msg[8], id;
	char *p;
	if (!is_acc || (id = twin_prg()) < 0)
		return 0;
	p = pass[k = !k];
	str_copy(p, s, sizeof(pass[0]));
	memset(msg, 0, sizeof(msg));
	msg[0] = 0x4E43;		/* PEOPLE */
	msg[1] = from;
	msg[3] = (short)((unsigned long)p >> 16);
	msg[4] = (short)(unsigned long)p;
	appl_write(id, 16, msg);
	return 1;
}

static void people_got(const char *s, short from)
{
	if (!people_pass(s, from))
		answer_message(s, from, data_ready);
}

static unsigned long quiet_at;	/* its last event call */

static void quiet_idle(void)
{
	short msg[8];
	AEVENT ev;
	if ((evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 5, msg, &ev) & MU_MESAG) && !av_modal_message(msg)) {
		/* (PEOPLE is answered at once: av_people_hook) */
		if (nheld < 16)
			memcpy(held[nheld++], msg, sizeof(msg));
		conn_stop = 1;		/* the window may be wanted: the sync stops */
	}
	quiet_at = pf_ms();
}

/* an answer read as it keeps arriving, or many cards in one: an event
   call at least every 30 ms */
static void quiet_yield(void)
{
	if (pf_ms() - quiet_at >= 30UL)
		quiet_idle();
}

static int quiet_due(void)
{
	unsigned long now = pf_ms();
	if (!is_acc || win_h > 0 || !data_ready || !naccts || opt.offline)
		return 0;
	if (opt.refresh > 0 && now - last_refresh >= (unsigned long)opt.refresh * 60000UL)
		return 1;
	return opt.sync_after > 0 && !synced_once && now >= (unsigned long)opt.sync_after * 60000UL;
}

static void quiet_begin(void)
{
	synced_once = 1;
	last_refresh = pf_ms();
	/* the program changed the files meanwhile: read again at the opening
	   (files_check), not written over now */
	if (files_known && store_stamp() != files_stamp)
		return;
	if (!quiet_sync_begin())
		return;
	conn_log_hold(1);		/* its log written once, at its end (6.5) */
	conn_log("EContact", "--", "refreshing in the background", 28L);
	was_idle = pf_idle;
	was_budget = carddav_budget;
	pf_idle = quiet_idle;
	http_yield = quiet_yield;
	quiet_at = pf_ms();
	carddav_budget = 32L * 1024;	/* a few cards an answer */
}

static unsigned long sync_stamp;	/* the files as the refresh left them */

static void quiet_end(void)
{
	conn_log_hold(0);
	pf_idle = was_idle;
	http_yield = 0;
	carddav_budget = was_budget;
	sync_stamp = store_stamp();
	if (!quiet_sync_changed())
		files_left();
	mem_note();
}

/* the books sorted again after it: what it wrote is its own (noted as
   it ended: the program may have run while they were sorted) */
static void quiet_sorted(void)
{
	list_invalidate();
	updated_now();
	last_refresh = pf_ms();
	if (is_acc && data_ready) {
		files_stamp = sync_stamp;
		files_known = 1;
	}
}

/* a program started or ended (AC_CLOSE) while it refreshed or sorted:
   it may be ECONTACT.PRG, which changes the same files. Stopped, and
   all read again at the opening */
static void quiet_abort(void)
{
	if (quiet_syncing()) {
		quiet_sync_cancel();
		conn_log_hold(0);
		pf_idle = was_idle;
		http_yield = 0;
		carddav_budget = was_budget;
	}
	books_load_cancel();
	files_stamp = 0;		/* never the files' */
	files_known = 1;
}

/* work the accessory has while its window is closed */
static int bg_pending(void)
{
	if (!is_acc || win_h > 0 || !workdir[0])
		return 0;
	return !data_ready || books_loading() || quiet_syncing() || quiet_due();
}

/* A slice's steps (common/ui/bgwork.c): the next one is begun only when
   the slice has room for one as long as the longest so far (parsing a
   card takes a Falcon about 17 ms), so the event calls stay within 50 ms
   (100 on a 68000) of each other */
static unsigned long slice_at, step_at, step_ms, slice_ms;
static int slice_over;

static void bg_slice(void)
{
	bg_begin();
	slice_at = step_at = pf_ms();
	step_ms = 0;
	slice_over = 0;
	slice_ms = get_cookie(0x5F435055L) >= 20 ? BG_SLICE_030 : BG_SLICE_000;	/* _CPU */
}

static int bg_more_step(void)
{
	static const char *was;
	const char *what = books_load_what();
	unsigned long now = pf_ms();
	if (now - step_at > step_ms)
		step_ms = now - step_at;
	step_at = now;
	/* a step of another kind (parsing after the records) may be far
	   longer: it begins a slice of its own. A quarter of the slice at
	   least for the next */
	if (slice_over)
		return 0;
	if (what != was && now != slice_at) {
		was = what;
		slice_over = 1;
		return 0;
	}
	was = what;
	if (!bg_more() || now - slice_at + (step_ms > slice_ms / 4 ? step_ms : slice_ms / 4) > slice_ms) {
		slice_over = 1;
		return 0;
	}
	aes_profile_tag(what);			/* the step next, should it be long */
	return 1;
}

/* one step of it */
static void bg_step(void)
{
	if (!settings_ready) {
		if (!store_ready) {
			aes_profile_tag("settings-inf");
			load_store(1);	/* in two steps: ECONTACT.INF, then the rest */
			return;
		}
		aes_profile_tag("settings-rest");
		load_settings(1);
		load_quiet = 1;
		load_stamp = store_stamp();
		return;
	}
	if (!data_ready) {
		if (!books_loading())
			books_load_begin();
		if (!books_load_run(bg_more_step))
			loaded();
		return;
	}
	if (books_loading()) {
		/* sorted again after a sync */
		if (!books_load_run(bg_more_step))
			quiet_sorted();
		return;
	}
	if (!quiet_syncing()) {
		quiet_begin();
		if (!quiet_syncing())
			return;
	}
	aes_profile_tag(quiet_sync_what());
	quiet_at = pf_ms();
	if (!quiet_sync_step())
		quiet_end();
}

static void files_left(void)
{
	if (is_acc && data_ready) {
		files_stamp = store_stamp();
		files_known = 1;
	}
}

static void files_check(void)
{
	static const char m[] = "the files changed since the window closed: reading them again";
	static const char m2[] = "the files changed while they were read: reading them again";
	unsigned long was;
	if (!is_acc)
		return;
	if (store_ready && !settings_ready) {
		store_release();	/* half read in the background: read whole now */
		store_ready = 0;
		return;
	}
	if (data_ready && files_known)
		was = files_stamp;
	else if (!data_ready && settings_ready && load_quiet)
		was = load_stamp;	/* a load in the background, part done */
	else
		return;
	if (store_stamp() == was)
		return;
	files_known = 0;
	load_quiet = 0;
	if (data_ready)
		conn_log("EContact", "--", m, (long)(sizeof(m) - 1));
	else
		conn_log("EContact", "--", m2, (long)(sizeof(m2) - 1));
	http_close_all();
	store_release();		/* and the load, part done */
	list_invalidate();
	settings_ready = 0;
	data_ready = 0;
	updated_known = 0;
	find_workdir();			/* FOLDER= may name another folder now */
}

static void open_window(void)
{
	if (!screen_check("EContact", scr_w))	/* not in ST low (the accessory) */
		return;
	int first = win_h <= 0;
	aes_profile_tag("open");
	bg_opened();
	if (first && quiet_syncing()) {
		/* a refresh in the background: what came is kept, the rest
		   left for the refresh at the opening */
		quiet_sync_stop();
		quiet_end();
		if (quiet_sync_changed())
			quiet_sorted();
	}
	if (first)
		files_check();
	if (!settings_ready)
		if (!load_settings(0))
			return;		/* no folder: cancelled */
	aes_profile_tag("open-window");
	if (!fonts_ready) {
		font_apply();
		fonts_ready = 1;
	}
	win_open();
	if (win_h <= 0 || !first)
		return;
	if (data_ready && books_loading()) {
		books_load_run(0);	/* sorted again, at once */
		quiet_sorted();
	}
	if (store_unsaved) {
		store_save_settings();	/* what a refresh in the background found */
		files_left();
	}
	if (!data_ready) {
		win_redraw(0);
		load_contacts();
	} else {
		/* read in the background: drawn once, when the AES asks for the
		   new window (WM_REDRAW), not for each thing set up here */
		win_hold = 1;
	}
	aes_profile_tag("open-select");
	/* the timed refresh counts from now: the 200 Hz timer counts from
	   when the Atari was switched on, and the window may have been
	   closed for hours (only "refresh when the window opens" refreshes
	   now) */
	last_refresh = last_minute = pf_ms();
	/* "my card", or the first, chosen at the start as on the Mac */
	if (!sel_contact()) {
		CONTACT *me = contact_by_uid(opt.me, -1);
		if (me)
			select_contact(me);
		else
			list_step(1);
	}
	aes_profile_tag("open-after");
	after_change();
	win_hold = 0;
	if (plain_untold || lost_untold || !naccts)
		win_redraw(0);		/* behind the alert */
	tell_untold();
	if (!naccts) {
		alert(1, "[1][Welcome to EContact!||Let's add your iCloud or other contacts.][ OK ]");
		dlg_add_account();
	} else if (opt.sync_open && !opt.offline) {
		sync_pending = 1;
	}
}

/* VA_START (common/ui/av.c), from the desktop or another program:
   nothing opens the window; .vcf files go into the address book new
   contacts go into */
static void av_start(const char *s)
{
	char path[200];
	const char *p = s;
	open_window();
	if (win_h <= 0)
		return;
	while ((p = av_arg(p, path, sizeof(path))) != 0) {
		long l = (long)strlen(path);
		int book = book_for_new();
		if (l < 4 || strcasecmp(path + l - 4, ".VCF")) {
			alert(1, "[1][EContact takes vCards (.vcf):|%s isn't one.][ OK ]", path);
			continue;
		}
		if (book < 0) {
			alert(1, "[1][Add an account first: the contacts go into one of its address books.][ OK ]");
			return;
		}
		import_file(book, path);
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

/* alerts (common/ui/alert.c): Hebrew letters where the font has them */
static void alert_heb(char *line, long n)
{
	if (opt.hebfont != HEB_ATARI)
		heb_font_map(line, n, opt.hebfont);
}

/* EContact (the program) crashed and CRASH.LOG is written
   (common/atari/crash.c): say so and leave GEM tidy. Only the AES and
   VDI: the heap may be what went wrong */
static void crash_quit(const char *what)
{
	char a[200];
	graf_mouse(ARROW, 0);
	snprintf(a, sizeof(a), "[3][EContact has crashed|(%s).|What happened is in|CRASH.LOG, next to ECONTACT.PRG.][ Quit ]", what);
	form_alert(1, a);
	if (win_h > 0) {
		wind_close(win_h);
		wind_delete(win_h);
	}
	v_clsvwk(vdi_h);
	appl_exit();
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
	twin_init("ECONTACT|ECONTST");
	if (!is_acc && twin_handover(av_command())) {
		appl_exit();		/* ECONTACT.ACC runs, and has it */
		return 0;
	}
	if (is_acc)
		menu_id = menu_register(ap_id, "  EContact");
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	if (is_acc)
		font_boot(boot_font[0] ? (short)atoi(boot_font) : 1);	/* before the first event call */
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
	crash_install(workdir, "EContact", app_version);	/* the program only */
	crash_cleanup = crash_quit;
	alert_hebrew = alert_heb;
	crash_report = conn_log_append;	/* the report in ECONTACT.LOG too */
	help_init();			/* memory the help viewer and EMail may read */
	av_init("ECONTACT|ECONTST");	/* the desktop's AV server, if one runs yet */
	st_ram_init();
	dlg_after = win_frame_refresh;
	if (!is_acc && !screen_check("EContact", scr_w)) {	/* not in ST low */
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	if (is_acc) {
		acc_heap();
		/* nothing read until START= says (the guidelines, 2.2) */
		bg_init(bg_parse(boot_start));
	}
	/* EMail and ECal asking about people (the guidelines, 4.1) */
	answer_init();
	av_people_hook = people_got;
	busy_message = busy_hold;
	if (!is_acc) {
		job_memory(0);
		/* the menu bar (the accessory has the toolbar's menu) */
		if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h) == 0)
			has_menu = menu_bar(mb.tree, 1) != 0;
		if (av_command()[0])
			av_start(av_command());	/* a .vcf the desktop opens with EContact */
		else
			open_window();
		menu_update();
	}
	crash_offer("EContact", "next to ECONTACT.PRG");

	while (!quit_now) {
		short flags = MU_MESAG | MU_TIMER, which;
		unsigned long wait = sync_pending ? 150 : card_photo_pending() || answer_busy() ? 100 : 1000;
		if (is_acc)
			wait = bg_timer(bg_pending(), wait);
		if (win_h > 0)
			flags |= MU_KEYBD | MU_BUTTON;
		conn_stop = 0;		/* a stop is for one operation */
		/* a key typed while busy comes first, as if typed now */
		if (win_h > 0 && busy_key(&ev))
			which = MU_KEYBD;
		else if (nheld) {
			/* one that came while the background refresh waited */
			memcpy(msg, held[0], sizeof(held[0]));
			memmove(held[0], held[1], sizeof(held[0]) * --nheld);
			which = MU_MESAG;
		} else
			which = evnt_multi_(flags, is_acc ? 0x102 : 0x101, 3, 0, wait, msg, &ev);
		if (win_iconified)	/* an icon takes no keys or clicks (taken, not left for later) */
			which &= ~(MU_KEYBD | MU_BUTTON);
		if (is_acc)
			bg_tick();
		aes_profile_tag("idle");
		answer_poll(data_ready);
		if (pick_new)
			pick_start();
		if (which & MU_MESAG) {
			const char *va;
			aes_profile_tag(msg[0] == WM_REDRAW ? "redraw" : "message");
			switch (msg[0]) {
			case AC_OPEN:
				/* from the Desk menu (or another program, with -1) */
				if (msg[4] == menu_id || msg[4] == -1) {
					aes_profile_tag("open");
					if (twin_pass(""))	/* ECONTACT.PRG runs: its window to the front */
						break;
					av_init("ECONTACT|ECONTST");	/* a desktop started since */
					open_window();
				}
				break;
			case MN_SELECTED:
				if (has_menu) {
					command(mbar_command(&mb, msg[4]));
					menu_tnormal(mb.tree, msg[3], 1);
				}
				break;
			case AC_CLOSE:
				twin_reset();		/* another program in front */
				if (picking)
					pick_finish(0, 0);	/* the window goes with it */
				if (win_h <= 0 && data_ready && (quiet_syncing() || books_loading()))
					quiet_abort();
				/* the program in front started or ended: the AES took
				   our window, if it was open. The files are noted only
				   then: on single TOS this comes when ECONTACT.PRG ends
				   too, after it changed them */
				if (msg[3] == menu_id && win_h > 0) {
					av_window(win_h, 0);
					win_h = -1;
					settings_flush();
					files_left();
				}
				break;
			case AP_TERM:
				if (!is_acc)
					quit_now = 1;
				win_close();
				break;
			default: {
				DROP d;
				/* .vcf files dropped: imported, as VA_START's */
				switch (dd_message(msg, DD_WANT_FILES, &d)) {
				case DD_FILES: av_start(d.data); break;
				case DD_NONE:
					switch (av_message(msg, &va)) {
					case AV_START:
						if (!(is_acc && twin_pass(va)))
							av_start(va);
						break;
					case AV_PEOPLE: people_got(va, msg[1]); break;
					case AV_DONE: break;
					default: win_message(msg);
					}
				}
				dd_done(&d);
			}
			}
		}
		if ((which & MU_KEYBD) && win_h > 0) {
			short scan = KEY_SCAN(ev.kreturn);
			if (scan == 0x44)
				set_hebrew_kbd(!hebrew_kbd);
			else if (scan == 0x62) {
				help_key_pressed();
			}
			else if (!crash_test(scan))	/* test builds: F1-F4 crash */
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
		if (which & (MU_KEYBD | MU_BUTTON | MU_MESAG))
			menu_update();
		if ((which & MU_TIMER) && settings_dirty && pf_ms() - settings_at >= 3000UL)
			settings_flush();
		if ((which & MU_TIMER) && win_h > 0)
			card_photo_tick();
		if ((which & MU_TIMER) && sync_pending && win_h > 0) {
			sync_pending = 0;
			refresh(0, 0);
		}
		/* the accessory's work with its window closed, a slice at a time */
		if (bg_pending() && bg_may_work()) {
			aes_profile_tag("bg-load");
			bg_slice();
			do
				bg_step();
			while (bg_pending() && bg_more_step());
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
	crash_remove();
	av_exit();
	if (has_menu)
		menu_bar(mb.tree, 0);
	mbar_free(&mb);
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
