/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * list.c - the people in the middle column, as on the Mac: sorted by
 * last name under grey letter headings, the first name plain and the
 * last name bold, the chosen one in a blue bar; only those of the group
 * chosen on the left and those matching the search.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "ui.h"
#include "../src/plat.h"
#include "panebar.h"

short src_kind = SRC_ALL, src_idx;
char search_text[64];
char sel_uid[80];
short sel_book = -1;
CONTACT **listed;
int nlisted;

static char src_name[64];	/* the group chosen, by name: indexes move */
static short src_book;
static int dirty = 1;

typedef struct {
	short contact;		/* index in shown, -1 for a letter heading */
	char letter;
} LROW;
static LROW *rows;
static int nrows, maxrows, top;
static short rh;		/* pixels per row */

/* the search the list was last made for: typing on narrows those it
   found, rather than looking through everyone again (the guidelines'
   Speed). Anything else that makes the list again forgets it */
static char narrow_from[64];
static int narrow_ok;

void list_invalidate(void)
{
	dirty = 1;
	narrow_ok = 0;
}


/* the sidebar's choice of group, kept by name */
static void resolve_source(void)
{
	int i;
	if (src_kind == SRC_GROUP) {
		if (src_idx >= 0 && src_idx < ngroups && groups[src_idx].book == src_book &&
		    !strcmp(groups[src_idx].name, src_name))
			return;
		for (i = 0; i < ngroups; i++)
			if (groups[i].book == src_book && !strcmp(groups[i].name, src_name)) {
				src_idx = (short)i;
				return;
			}
		src_kind = SRC_ALL;		/* the group is gone */
	}
	if (src_kind == SRC_BOOK && (src_idx < 0 || src_idx >= nbooks))
		src_kind = SRC_ALL;
}

/* called by the sidebar */
void list_source(short kind, short idx)
{
	src_kind = kind;
	src_idx = idx;
	if (kind == SRC_GROUP && idx >= 0 && idx < ngroups) {
		str_copy(src_name, groups[idx].name, sizeof(src_name));
		src_book = groups[idx].book;
	}
	top = 0;
	dirty = 1;
}

static int wanted(const CONTACT *c)
{
	if (c->group)
		return 0;
	if (src_kind == SRC_BOOK && c->book != src_idx)
		return 0;
	if (src_kind == SRC_GROUP && !group_has(&groups[src_idx], c))
		return 0;
	return contact_matches(c, search_text);
}

static char heading_of(const CONTACT *c)
{
	unsigned char k = (unsigned char)c->sortkey[0];
	if (k >= 'A' && k <= 'Z')
		return (char)k;
	if (k >= 0xC2 && k <= 0xDC)
		return (char)k;		/* a Hebrew letter: its own heading */
	return '#';
}

static void add_row(short contact, char letter)
{
	if (nrows >= maxrows) {
		int max = maxrows ? maxrows * 2 : 128;
		LROW *n = realloc(rows, max * sizeof(LROW));
		if (!n)
			return;
		rows = n;
		maxrows = max;
	}
	rows[nrows].contact = contact;
	rows[nrows].letter = letter;
	nrows++;
}

/* more typed after the text the list was made for, nothing else changed
   since (list_invalidate): what matches now is among those it found. Not
   after digits: a phone number's match leaves its dashes out */
static int narrows(void)
{
	int n = (int)strlen(narrow_from), k;
	if (!narrow_ok || !listed || !n || strncmp(search_text, narrow_from, n) || !search_text[n])
		return 0;
	for (k = 0; k < n; k++)
		if (isdigit((unsigned char)narrow_from[k]) || narrow_from[k] == '+')
			return 0;
	return 1;
}

/* the rows for what is listed: a heading for each letter, only for the
   whole list (not a search's few) */
static void make_rows(void)
{
	int i;
	char last = 0;
	nrows = 0;
	str_copy(narrow_from, search_text, sizeof(narrow_from));
	narrow_ok = 1;
	for (i = 0; i < nlisted; i++) {
		char h = heading_of(listed[i]);
		if (h != last && !search_text[0])
			add_row(-1, h);
		last = h;
		add_row((short)i, 0);
	}
	if (top > nrows - 1)
		top = nrows > 0 ? nrows - 1 : 0;
}

/* The search's matching as a job, a slice a turn (search_step from the
   event loop): 621 contacts took an 8 MHz ST 0.3 s. What is listed stays
   as it was till it is done, and a key typed meanwhile starts it again */
static CONTACT **sj;		/* those found so far */
static int sj_on, sj_n, sj_i, sj_of, sj_listed;

static void search_cancel(void)
{
	free(sj);
	sj = 0;
	sj_on = 0;
}

static void rebuild(void)
{
	int i, k;
	nrows = 0;
	rh = ch + 2;
	search_cancel();	/* made whole now */
	/* the program reading its books (main.c, in slices): nothing listed
	   until they are all read and sorted, then made again */
	if (books_loading()) {
		nlisted = 0;
		return;
	}
	dirty = 0;
	resolve_source();
	if (narrows()) {
		for (i = k = 0; i < nlisted; i++)
			if (wanted(listed[i]))
				listed[k++] = listed[i];
		nlisted = k;
	} else {
		free(listed);
		listed = malloc((npeople + 1) * sizeof(CONTACT *));
		nlisted = 0;
		if (!listed)
			return;
		for (i = 0; i < npeople; i++)
			if (wanted(people[i]))
				listed[nlisted++] = people[i];
	}
	make_rows();
}

static void ensure(void)
{
	if (dirty)
		rebuild();
}

/* the search's text changed (win.c): its matching begun as a job, or
   the list made whole at the next look when it is due anyway */
void list_search_changed(void)
{
	search_cancel();
	if (dirty || !listed || books_loading()) {
		dirty = 1;
		return;
	}
	resolve_source();
	sj_listed = narrows();
	sj_of = sj_listed ? nlisted : npeople;
	sj = malloc((sj_of + 1) * sizeof(CONTACT *));
	if (!sj) {
		dirty = 1;
		return;
	}
	sj_n = sj_i = 0;
	sj_on = 1;
}

int list_search_pending(void)
{
	return sj_on;
}

/* the job's contacts while more() says so (some at least); 1: done, the
   list is what was found */
int list_search_step(int (*more)(void))
{
	int k = 0;
	if (!sj_on)
		return 0;
	while (sj_i < sj_of && (k++ < 8 || !more || more())) {
		CONTACT *c = sj_listed ? listed[sj_i] : people[sj_i];
		if (wanted(c))
			sj[sj_n++] = c;
		sj_i++;
	}
	if (sj_i < sj_of)
		return 0;
	free(listed);
	listed = sj;
	nlisted = sj_n;
	sj = 0;
	sj_on = 0;
	rh = ch + 2;
	make_rows();
	return 1;
}

/* the chosen one, found again only when another is chosen or the people
   changed (people_serial counts up before any CONTACT goes): its uid
   compared with 621 others took an 8 MHz ST 12 ms, and it is asked for
   at every turn and in every part of a drawing */
CONTACT *sel_contact(void)
{
	static CONTACT *c;
	static unsigned long serial;
	static char uid[sizeof(sel_uid)];
	static short book = -2;
	if (!sel_uid[0])
		return 0;
	if (c && serial == people_serial && book == sel_book && !strcmp(uid, sel_uid))
		return c;
	c = contact_by_uid(sel_uid, sel_book);
	serial = people_serial;
	book = sel_book;
	str_copy(uid, sel_uid, sizeof(uid));
	return c;
}

static int sel_row(void)
{
	int i;
	CONTACT *c = sel_contact();
	ensure();
	if (!c)
		return -1;
	for (i = 0; i < nrows; i++)
		if (rows[i].contact >= 0 && listed[rows[i].contact] == c)
			return i;
	return -1;
}

static short visible_rows(void)
{
	short n = (r_list.h - 4) / (rh ? rh : ch + 2);
	return n < 1 ? 1 : n;
}

static void clamp_top(void);
static int list_scroll_to(int t);

/* The list's own scroll bar, at the right of its column (Erez, 20 Oct
   2026: the window's slider, at the far right by the card, moved the
   list in the middle): the family's pane bar (common/ui/panebar.c, the
   guidelines 5.1), AES objects in the AES's own look, as wide as its
   slider column. The window is drawn off the screen and copied; the bar
   is drawn on the screen after the copy (list_bar_draw, from win.c) */
static PANEBAR pb;

/* the bar, its right edge on the column's dividing line, with the
   list's numbers as they are now */
static PANEBAR *bar(void)
{
	pb.r = r_list;
	pb.r.w = pb_width();
	pb.r.x = r_list.x + r_list.w - pb.r.w;
	pb.total = nrows;
	pb.shown = visible_rows();
	pb.top = top;
	pb.across = 0;
	return &pb;
}

/* the rows' width, left of the bar */
static short rows_w(void)
{
	return r_list.w - pb_width();
}

/* the height of a band of the list from y, h or a little more: to where
   a row ends (win.c win_redraw_bands; a row two bands cut was drawn in
   each) */
short list_band(short y, short h)
{
	short r0 = r_list.y + 2, end = y + h;
	if (rh <= 0 || end <= r0)
		return h;
	end = r0 + (short)((end - r0 + rh - 1) / rh) * rh;
	return end - y;
}

/* on the bar: a click there acts at once (no wait for a second) */
int list_bar_hit(short mx, short my)
{
	return rect_hit(&bar()->r, mx, my);
}

void list_bar_draw(const GRECT *clip)
{
	GRECT r;
	ensure();
	clamp_top();
	r = bar()->r;
	if (rect_and(&r, clip))
		pb_draw(&pb, &r);
}

/* where the bar is (w 0: none) */
void list_bar_area(GRECT *r)
{
	ensure();
	clamp_top();
	*r = bar()->r;
}

static int bar_scroll(long n)
{
	return list_scroll((int)n);
}

static int bar_goto(long t)
{
	return list_scroll_to((int)t);
}

static void clamp_top(void)
{
	short vis = visible_rows();
	if (top > nrows - vis)
		top = nrows - vis;
	if (top < 0)
		top = 0;
}

/* the first row shown becomes t: the rows on the screen are moved and
   only those that come in drawn (win_scroll), and of the bar only where
   the slider was and is (all of it took an 8 MHz ST 70 ms); 0 when
   nothing moved */
static int list_scroll_to(int t)
{
	int old = top;
	GRECT a;
	ensure();
	top = t;
	clamp_top();
	if (top == old)
		return 0;
	a = r_list;
	a.y += 2;		/* the rows, under the list's top margin */
	a.h -= 2;
	a.w = rows_w();
	if (a.h > 0 && a.w > 0) {
		long dy = (long)(top - old) * (rh ? rh : ch + 2);
		win_scroll(&a, dy >= a.h || dy <= -a.h ? a.h : (short)dy);
	}
	/* the bar only when its slider moved: a row of 600 is half a pixel
	   of the track, and TOS 1.04's objc_draw takes an ST 50 ms for the
	   slider's part. A raised slider's edge stands out of the track too,
	   over the arrow's foot at the top: pb_moved stops at the track
	   (common's panebar.c), so a few pixels more, within the bar */
	{
		PANEBAR o = *bar();
		short ty, th, sy, sh, ny, nh;
		o.top = old;
		pb_slider(&o, &ty, &th, &sy, &sh);
		pb_slider(&pb, &ty, &th, &ny, &nh);
		if (sy == ny && sh == nh)
			return 1;
	}
	a = pb_moved(&pb, old);
	a.y -= 3;
	a.h += 6;
	rect_and(&a, &pb.r);
	win_bars(&a);
	return 1;
}

int list_scroll(int n)
{
	ensure();
	return list_scroll_to(top + n);
}

/* the top that shows the chosen one, or -1 when it isn't listed */
static int top_for_sel(void)
{
	int r = sel_row(), vis = visible_rows(), t = top;
	if (r < 0)
		return -1;
	/* its heading too, when it is the first under one */
	if (r > 0 && rows[r - 1].contact < 0 && r - 1 < t)
		t = r - 1;
	else if (r < t)
		t = r;
	else if (r >= t + vis)
		t = r - vis + 1;
	return t;
}

void list_scroll_to_sel(void)
{
	int t = top_for_sel();
	if (t < 0)
		return;
	top = t;
	clamp_top();
}

/* row r's strip on the screen, if it is shown */
static int row_area(int r, GRECT *a)
{
	if (r < top || r > top + visible_rows())
		return 0;
	a->x = r_list.x;
	a->y = r_list.y + 2 + (short)(r - top) * rh;
	a->w = rows_w();
	a->h = rh;
	return 1;
}

/* "Erez" plain and "Yaary" bold: the part the list is sorted by in bold */
static void draw_name(const CONTACT *c, short x, short y, short cols, short flags)
{
	char plain[100], bold[100], all[200];
	const char *g = c->given, *m = c->middle, *f = c->family;
	short n;
	vcard_display_name(c, opt.last_first, all, sizeof(all));
	n = (short)strlen(all);
	if (c->company || !f || !*f || !g || !*g || line_rtl(all, n) || n > cols) {
		/* a company, one name only, Hebrew or too long: all of it, the
		   company and a lone name bold */
		short b = (c->company || !g || !*g || !f || !*f) ? TX_BOLD : 0;
		if (opt.sort_first && g && *g && f && *f)
			b = 0;
		text_at(x, y, all, n, cols, flags | b | (line_rtl(all, n) ? TX_RIGHT : TX_LTR));
		return;
	}
	if (opt.last_first) {
		snprintf(bold, sizeof(bold), "%s", f);
		snprintf(plain, sizeof(plain), ", %s%s%s", g, m && *m ? " " : "", m && *m ? m : "");
		text_at(x, y, bold, (long)strlen(bold), cols, flags | TX_LTR | (opt.sort_first ? 0 : TX_BOLD));
		n = (short)strlen(bold);
		text_at(x + n * cw, y, plain, (long)strlen(plain), cols - n, flags | TX_LTR | (opt.sort_first ? TX_BOLD : 0));
	} else {
		snprintf(plain, sizeof(plain), "%s%s%s ", g, m && *m ? " " : "", m && *m ? m : "");
		snprintf(bold, sizeof(bold), "%s", f);
		n = (short)strlen(plain);
		text_at(x, y, plain, n, cols, flags | TX_LTR | (opt.sort_first ? TX_BOLD : 0));
		text_at(x + n * cw, y, bold, (long)strlen(bold), cols - n, flags | TX_LTR | (opt.sort_first ? 0 : TX_BOLD));
	}
}

void list_draw(GRECT *clip)
{
	GRECT r = r_list;
	int i, vis;
	short rw = rows_w(), y, cols = rw / cw - 2, srow;
	CONTACT *sel;
	if (!rect_and(&r, clip))
		return;
	ensure();
	clip_on(&r);
	fill(&r, 0);
	clamp_top();
	if (!nrows) {
		const char *s = search_text[0] ? "No Results" : naccts ? "No Contacts" : "No accounts yet";
		short n = (short)strlen(s);
		text_at(r_list.x + (rw - n * cw) / 2, r_list.y + r_list.h / 3, s, n, n, TX_LTR | TX_GREY);
		if (!naccts) {
			const char *t = "\xF0 > Add account...";
			text_at(r_list.x + cw, r_list.y + r_list.h / 3 + 2 * ch, t, (long)strlen(t), cols, TX_LTR | TX_GREY);
		}
		clip_on(clip);
		return;
	}
	vis = visible_rows();
	sel = sel_contact();
	r.w = rw - (r.x - r_list.x);	/* the rows left of the bar */
	if (r.w <= 0) {
		clip_on(clip);
		return;
	}
	clip_on(&r);
	srow = -1;
	y = r_list.y + 2;
	for (i = top; i < nrows && i < top + vis + 1; i++, y += rh) {
		LROW *lr = &rows[i];
		/* only the rows in the area: a scroll draws the strip that
		   came in, and the bar, each a row or so */
		if (y + rh <= r.y || y >= r.y + r.h)
			continue;
		if (lr->contact < 0) {
			char s[2];
			s[0] = lr->letter;
			s[1] = 0;
			text_at(r_list.x + cw, y + 1, s, 1, 1, TX_BOLD | TX_GREY | TX_LTR | TX_WHITE);
			line_col(r_list.x + 3 * cw, y + rh / 2, r_list.x + rw - cw, y + rh / 2, grey_color());
			continue;
		}
		{
			CONTACT *c = listed[lr->contact];
			short flags = TX_WHITE;
			if (c == sel) {
				GRECT b;
				b.x = r_list.x + 2;
				b.y = y;
				b.w = rw - 4;
				b.h = rh;
				fill(&b, sel_color());
				flags = TX_INVERSE;
				srow = (short)i;
			}
			draw_name(c, r_list.x + cw, y + 1, cols - (picking ? 2 : 0), flags);
			if (picking && c->mark)
				text_at(r_list.x + rw - 2 * cw, y + 1, "\x08", 1, 1, TX_LTR | flags);	/* a tick */
			if (opt.me[0] && c->uid && !strcmp(c->uid, opt.me))
				text_at(r_list.x + rw - 4 * cw, y + 1, "me", 2, 2, TX_LTR | (flags ? flags : TX_GREY));
		}
	}
	(void)srow;
	clip_on(clip);
}

/* the chosen one changed: its row and the old one, and the card */
void select_contact(CONTACT *c)
{
	int was = sel_row(), now, t;
	GRECT a;
	if (c) {
		str_copy(sel_uid, c->uid ? c->uid : "", sizeof(sel_uid));
		sel_book = c->book;
	} else {
		sel_uid[0] = 0;
		sel_book = -1;
	}
	/* the rows that stay are moved (win_scroll), only those that come
	   in drawn, then the old and the new chosen rows: each row's text is
	   two of TOS 1.04's slowest calls, 24 ms in bold on an ST */
	t = top_for_sel();
	if (t >= 0 && t != top)
		list_scroll_to(t);
	now = sel_row();
	if (was != now && row_area(was, &a))
		win_redraw(&a);
	if (row_area(now, &a))
		win_redraw(&a);
	win_redraw_later(&r_card);	/* a band a turn: keys meanwhile (a held Down) */
}

/* what is shown changed: the chosen one stays if it is still there,
   else the first one is chosen, as on the Mac */
void list_fix_selection(void)
{
	int i;
	ensure();
	if (sel_row() >= 0)
		return;
	sel_uid[0] = 0;
	sel_book = -1;
	top = 0;
	for (i = 0; i < nrows; i++)
		if (rows[i].contact >= 0) {
			CONTACT *c = listed[rows[i].contact];
			str_copy(sel_uid, c->uid ? c->uid : "", sizeof(sel_uid));
			sel_book = c->book;
			break;
		}
}

void list_step(int dir)
{
	int r = sel_row(), i;
	ensure();
	if (!nrows)
		return;
	if (r < 0) {
		/* nothing chosen: the first (or last) */
		for (i = dir > 0 ? 0 : nrows - 1; i >= 0 && i < nrows; i += dir > 0 ? 1 : -1)
			if (rows[i].contact >= 0) {
				select_contact(listed[rows[i].contact]);
				return;
			}
		return;
	}
	for (i = r + dir; i >= 0 && i < nrows; i += dir)
		if (rows[i].contact >= 0) {
			select_contact(listed[rows[i].contact]);
			return;
		}
}

static void add_to_group(CONTACT *c)
{
	static char lab[64][70];
	static const char *items[64];
	short idx[64];
	int i, n = 0, k;
	char uid[80];
	short book = c->book;
	for (i = 0; i < ngroups && n < 63; i++) {
		if (groups[i].book != c->book)
			continue;
		snprintf(lab[n], sizeof(lab[n]), "%s%s", group_has(&groups[i], c) ? "\x08 " : "  ", groups[i].name);
		items[n] = lab[n];
		idx[n++] = (short)i;
	}
	snprintf(lab[n], sizeof(lab[n]), "  New Group...");
	items[n] = lab[n];
	idx[n++] = -1;
	k = dlg_pick("Add to group", items, (short)n, 0);
	if (k < 0)
		return;
	str_copy(uid, c->uid ? c->uid : "", sizeof(uid));
	busy(1);
	if (idx[k] < 0) {
		char name[64] = "";
		busy(0);
		if (!dlg_ask("New Group", "Name:", name, sizeof(name) - 1) || !name[0])
			return;
		busy(1);
		k = group_new(book, name);
		if (k < 0) {
			busy(0);
			alert(1, "[1][Couldn't make the group:|%s][ OK ]", ab_err);
			after_change();
			return;
		}
		c = contact_by_uid(uid, book);
		if (c && !group_add(&groups[k], c))
			alert(1, "[1][Couldn't add it:|%s][ OK ]", ab_err);
	} else if (!group_has(&groups[idx[k]], c)) {
		if (!group_add(&groups[idx[k]], c))
			alert(1, "[1][Couldn't add it:|%s][ OK ]", ab_err);
	} else if (!group_remove(&groups[idx[k]], c)) {
		alert(1, "[1][Couldn't take it out:|%s][ OK ]", ab_err);
	}
	busy(0);
	after_change();
}

void contact_menu(short mx, short my, CONTACT *c)
{
	enum { A_EDIT, A_DEL, A_GROUP, A_OUT, A_MAIL, A_COPY, A_ME, A_EXPORT, A_NONE };
	const char *items[12];
	short act[12], n = 0, k;
	char out[80], uid[80];
	short book = c->book;
	int writable = book_writable(&books[c->book]);
	items[n] = "  Edit Card            ^E";
	act[n++] = A_EDIT;
	if (writable) {
		items[n] = "  Delete Card...      Del";
		act[n++] = A_DEL;
	}
	items[n] = "-";
	act[n++] = A_NONE;
	if (writable) {
		items[n] = "  Add to Group...";
		act[n++] = A_GROUP;
	}
	if (writable && src_kind == SRC_GROUP && src_idx < ngroups) {
		snprintf(out, sizeof(out), "  Remove from %.20s", groups[src_idx].name);
		items[n] = out;
		act[n++] = A_OUT;
	}
	if (c->nemail) {
		items[n] = "  Send Email";
		act[n++] = A_MAIL;
	}
	items[n] = "  Copy Name and Email";
	act[n++] = A_COPY;
	items[n] = "-";
	act[n++] = A_NONE;
	items[n] = opt.me[0] && c->uid && !strcmp(c->uid, opt.me) ? "\x08 My Card" : "  Make This My Card";
	act[n++] = A_ME;
	items[n] = "  Export vCard...";
	act[n++] = A_EXPORT;
	k = win_popup(mx, my, items, n);
	if (k < 0 || act[k] == A_NONE)
		return;
	str_copy(uid, c->uid ? c->uid : "", sizeof(uid));
	switch (act[k]) {
	case A_EDIT: editor_open(c); break;
	case A_DEL: cmd_delete_selected(); break;
	case A_GROUP: add_to_group(c); break;
	case A_OUT:
		busy(1);
		if (!group_remove(&groups[src_idx], c))
			alert(1, "[1][Couldn't take it out:|%s][ OK ]", ab_err);
		busy(0);
		after_change();
		break;
	case A_MAIL: cmd_mail_to(c, c->email[0].value); break;
	case A_COPY: cmd_copy_contact(c); break;
	case A_ME:
		str_copy(opt.me, uid, sizeof(opt.me));
		store_save_settings();
		win_redraw(&r_list);
		break;
	case A_EXPORT: {
		/* the one contact: select it first, then the menu's export takes it */
		CONTACT *cc = contact_by_uid(uid, book);
		if (cc)
			select_contact(cc);
		cmd_export();
		break;
	}
	}
}

int list_click(short mx, short my, short clicks, short button, short kstate)
{
	int i;
	ensure();
	if (mx >= r_list.x + rows_w()) {
		pb_click(bar(), mx, my, bar_scroll, bar_goto);
		return 1;
	}
	if (!rh)
		return 1;
	i = top + (my - r_list.y - 2) / rh;
	if (my < r_list.y + 2 || i < 0 || i >= nrows || rows[i].contact < 0)
		return 1;
	{
		CONTACT *c = listed[rows[i].contact];
		if (c != sel_contact())
			select_contact(c);
		if (picking && !(button & 2)) {
			/* choosing for EMail or ECal: a click ticks or unticks,
			   a double click chooses */
			if (clicks == 2) {
				c->mark = 1;
				pick_end(1);
			} else {
				c->mark = !c->mark;
				win_redraw(&r_list);
			}
			return 1;
		}
		if (button & 2)
			contact_menu(mx, my, sel_contact());
		else if (clicks == 2)
			editor_open(sel_contact());
	}
	(void)kstate;
	return 1;
}

int list_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	if (key_ctrl(kstate, key) || (kstate & K_ALT))
		return 0;
	switch (scan) {
	case 0x48: list_step(-1); return 1;			/* up */
	case 0x50: list_step(1); return 1;			/* down */
	case 0x47:						/* Clr/Home: the first, Shift: the last */
		ensure();
		sel_uid[0] = 0;
		list_step((kstate & (K_LSHIFT | K_RSHIFT)) ? -1 : 1);
		return 1;
	case 0x1c: case 0x72:					/* Return: edit */
		if (sel_contact())
			editor_open(sel_contact());
		return 1;
	case 0x53:						/* Delete */
		if (sel_contact())
			cmd_delete_selected();
		return 1;
	}
	return 0;
}
