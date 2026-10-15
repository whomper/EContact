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
#include "ui.h"

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

void list_invalidate(void)
{
	dirty = 1;
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

static void rebuild(void)
{
	int i;
	char last = 0;
	dirty = 0;
	resolve_source();
	free(listed);
	listed = malloc((npeople + 1) * sizeof(CONTACT *));
	nlisted = 0;
	nrows = 0;
	rh = ch + 2;
	if (!listed)
		return;
	for (i = 0; i < npeople; i++)
		if (wanted(people[i]))
			listed[nlisted++] = people[i];
	for (i = 0; i < nlisted; i++) {
		char h = heading_of(listed[i]);
		/* headings only for the whole list, not for a search's few */
		if (h != last && !search_text[0])
			add_row(-1, h);
		last = h;
		add_row((short)i, 0);
	}
	if (top > nrows - 1)
		top = nrows > 0 ? nrows - 1 : 0;
}

static void ensure(void)
{
	if (dirty)
		rebuild();
}

CONTACT *sel_contact(void)
{
	CONTACT *c;
	if (!sel_uid[0])
		return 0;
	c = contact_by_uid(sel_uid, sel_book);
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

void list_slider(long *size, long *pos)
{
	short vis;
	ensure();
	vis = visible_rows();
	if (nrows <= vis) {
		*size = 1000;
		*pos = 0;
		return;
	}
	*size = (long)vis * 1000 / nrows;
	*pos = (long)top * 1000 / (nrows - vis);
}

static void clamp_top(void)
{
	short vis = visible_rows();
	if (top > nrows - vis)
		top = nrows - vis;
	if (top < 0)
		top = 0;
}

void list_scroll(int n)
{
	int old = top;
	ensure();
	top += n;
	clamp_top();
	if (top != old)
		win_redraw(&r_list);
}

void list_slide(short pos)
{
	int old = top, vis;
	ensure();
	vis = visible_rows();
	top = nrows > vis ? (int)((long)pos * (nrows - vis) / 1000) : 0;
	clamp_top();
	if (top != old)
		win_redraw(&r_list);
}

void list_scroll_to_sel(void)
{
	int r = sel_row(), vis = visible_rows();
	if (r < 0)
		return;
	/* its heading too, when it is the first under one */
	if (r > 0 && rows[r - 1].contact < 0 && r - 1 < top)
		top = r - 1;
	else if (r < top)
		top = r;
	else if (r >= top + vis)
		top = r - vis + 1;
	clamp_top();
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
	short y, cols = r_list.w / cw - 2, srow;
	CONTACT *sel;
	if (!rect_and(&r, clip))
		return;
	ensure();
	clip_on(&r);
	fill(&r, 0);
	if (!nrows) {
		const char *s = search_text[0] ? "No Results" : naccts ? "No Contacts" : "No accounts yet";
		short n = (short)strlen(s);
		text_at(r_list.x + (r_list.w - n * cw) / 2, r_list.y + r_list.h / 3, s, n, n, TX_LTR | TX_GREY);
		if (!naccts) {
			const char *t = "\xF0 > Add account...";
			text_at(r_list.x + cw, r_list.y + r_list.h / 3 + 2 * ch, t, (long)strlen(t), cols, TX_LTR | TX_GREY);
		}
		clip_on(clip);
		return;
	}
	vis = visible_rows();
	clamp_top();
	sel = sel_contact();
	srow = -1;
	y = r_list.y + 2;
	for (i = top; i < nrows && i < top + vis + 1; i++, y += rh) {
		LROW *lr = &rows[i];
		if (lr->contact < 0) {
			char s[2];
			s[0] = lr->letter;
			s[1] = 0;
			text_at(r_list.x + cw, y + 1, s, 1, 1, TX_BOLD | TX_GREY | TX_LTR);
			line_col(r_list.x + 3 * cw, y + rh / 2, r_list.x + r_list.w - cw, y + rh / 2, grey_color());
			continue;
		}
		{
			CONTACT *c = listed[lr->contact];
			short flags = 0;
			if (c == sel) {
				GRECT b;
				b.x = r_list.x + 2;
				b.y = y;
				b.w = r_list.w - 5;
				b.h = rh;
				fill(&b, sel_color());
				flags = TX_INVERSE;
				srow = (short)i;
			}
			draw_name(c, r_list.x + cw, y + 1, cols, flags);
			if (opt.me[0] && c->uid && !strcmp(c->uid, opt.me))
				text_at(r_list.x + r_list.w - 4 * cw, y + 1, "me", 2, 2, TX_LTR | (flags ? flags : TX_GREY));
		}
	}
	(void)srow;
	clip_on(clip);
}

/* the chosen one changed: its row and the old one, and the card */
void select_contact(CONTACT *c)
{
	if (c) {
		str_copy(sel_uid, c->uid ? c->uid : "", sizeof(sel_uid));
		sel_book = c->book;
	} else {
		sel_uid[0] = 0;
		sel_book = -1;
	}
	list_scroll_to_sel();
	win_redraw(&r_list);
	win_redraw(&r_card);
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
		char name[60] = "";
		busy(0);
		if (!dlg_ask("New Group", "Name:", name, 40) || !name[0])
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
	if (!rh)
		return 1;
	i = top + (my - r_list.y - 2) / rh;
	if (my < r_list.y + 2 || i < 0 || i >= nrows || rows[i].contact < 0)
		return 1;
	{
		CONTACT *c = listed[rows[i].contact];
		if (c != sel_contact())
			select_contact(c);
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
	if (kstate & (K_CTRL | K_ALT))
		return 0;
	switch (scan) {
	case 0x48: list_step(-1); return 1;			/* up */
	case 0x50: list_step(1); return 1;			/* down */
	case 0x47:						/* Clr/Home: the first, Shift: the last */
		ensure();
		top = 0;
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
