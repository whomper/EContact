/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sidebar.c - the list on the left, as on the Mac: All Contacts, then
 * each account with its address books and their groups. A click shows
 * those people in the list; the right button has the rest (new group,
 * rename, email the whole group, the account's settings).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"

void list_source(short kind, short idx);

typedef struct {
	short y;
	short kind;		/* -1 an account's name, SRC_ALL, SRC_BOOK, SRC_GROUP */
	short idx;		/* the account, book or group */
} SROW;
#define MAXROWS (MAXACCT + MAXBOOK + 120)
static SROW srow[MAXROWS];
static short nrows, row_h;

static int books_of(int a)
{
	int i, n = 0;
	for (i = 0; i < nbooks; i++)
		n += books[i].acct == a;
	return n;
}

static void side_layout(void)
{
	short y = r_side.y + ch / 2, a, b, g;
	row_h = ch + 3;
	nrows = 0;
	srow[nrows].y = y;
	srow[nrows].kind = SRC_ALL;
	srow[nrows].idx = 0;
	nrows++;
	y += row_h + ch / 2;
	for (a = 0; a < naccts && nrows < MAXROWS; a++) {
		srow[nrows].y = y;
		srow[nrows].kind = -1;
		srow[nrows].idx = a;
		nrows++;
		y += row_h;
		for (b = 0; b < nbooks && nrows < MAXROWS; b++) {
			if (books[b].acct != a)
				continue;
			srow[nrows].y = y;
			srow[nrows].kind = SRC_BOOK;
			srow[nrows].idx = b;
			nrows++;
			y += row_h;
			for (g = 0; g < ngroups && nrows < MAXROWS; g++) {
				if (groups[g].book != b)
					continue;
				srow[nrows].y = y;
				srow[nrows].kind = SRC_GROUP;
				srow[nrows].idx = g;
				nrows++;
				y += row_h;
			}
		}
		y += ch / 2;
	}
}

static void row_text(const SROW *s, char *out, int size)
{
	switch (s->kind) {
	case -1: str_copy(out, accts[s->idx].name, size); break;
	case SRC_ALL: str_copy(out, "All Contacts", size); break;
	case SRC_BOOK:
		if (books_of(books[s->idx].acct) == 1)
			snprintf(out, size, "All %s", accts[books[s->idx].acct].name);
		else
			str_copy(out, books[s->idx].name, size);
		break;
	case SRC_GROUP: str_copy(out, groups[s->idx].name, size); break;
	}
}

static int is_chosen(const SROW *s)
{
	if (s->kind == SRC_ALL)
		return src_kind == SRC_ALL;
	return s->kind == src_kind && s->idx == src_idx;
}

void side_draw(GRECT *clip)
{
	GRECT r = r_side;
	short i, cols = r_side.w / cw - 1;
	char t[80];
	if (!rect_and(&r, clip))
		return;
	side_layout();
	clip_on(&r);
	fill(&r, 0);
	for (i = 0; i < nrows; i++) {
		SROW *s = &srow[i];
		short x = r_side.x + cw / 2, flags = TX_RIGHT;
		row_text(s, t, sizeof(t));
		if (s->kind == -1) {
			text_at(x, s->y, t, (long)strlen(t), cols - 1, TX_BOLD | TX_RIGHT | TX_GREY);
			if (accts[s->idx].err[0])
				text_at(r_side.x + r_side.w - 2 * cw, s->y, "!", 1, 1, TX_BOLD | TX_LTR);
			continue;
		}
		if (s->kind == SRC_BOOK)
			x += cw;
		else if (s->kind == SRC_GROUP)
			x += 2 * cw;
		if (is_chosen(s)) {
			GRECT b;
			b.x = r_side.x + 3;
			b.y = s->y - 1;
			b.w = r_side.w - 7;
			b.h = row_h;
			if (ncolors >= 16) {
				fill(&b, text_grey(1));
			} else {
				fill(&b, 1);
				flags |= TX_INVERSE;
			}
		}
		if (s->kind == SRC_ALL)
			flags |= TX_BOLD;
		/* a group: a small mark of two heads before it */
		if (s->kind == SRC_GROUP) {
			short gx = x, gy = s->y + ch / 2, rr = ch >= 16 ? 2 : 1, col = (flags & TX_INVERSE) ? 0 : text_grey(0);
			disc(gx + 2, gy - rr, rr, rr, col);
			disc(gx + 6, gy - rr, rr, rr, col);
			line_col(gx, gy + rr + 1, gx + 8, gy + rr + 1, col);
			x += cw + cw / 2;
		}
		text_at(x, s->y, t, (long)strlen(t), (r_side.x + r_side.w - x) / cw - 1, flags);
		if (s->kind == SRC_BOOK && books[s->idx].readonly)
			text_at(r_side.x + r_side.w - 2 * cw, s->y, "\xBF", 1, 1, TX_LTR | TX_GREY);
	}
	if (!naccts) {
		text_at(r_side.x + cw / 2, srow[0].y + 3 * ch, "No accounts yet:", 16, cols, TX_LTR | TX_GREY);
		text_at(r_side.x + cw / 2, srow[0].y + 4 * ch, "\xF0 > Add account...", 18, cols, TX_LTR | TX_GREY);
	}
	clip_on(clip);
}

/* after a rename the list stays on the group, by its new name */
static void follow_group(const char *name, int book)
{
	int i;
	for (i = 0; i < ngroups; i++)
		if (groups[i].book == book && !strcmp(groups[i].name, name)) {
			list_source(SRC_GROUP, (short)i);
			return;
		}
}

static void choose(short kind, short idx)
{
	list_source(kind, idx);
	list_fix_selection();
	status_updated();
	win_redraw(&r_tool);
	win_redraw(&r_side);
	win_redraw(&r_list);
	win_redraw(&r_card);
}

/* every email of the group, for EMail's To: line */
static void mail_group(GROUP *g)
{
	SBUF b;
	int i;
	sb_init(&b);
	for (i = 0; i < npeople; i++)
		if (people[i]->nemail && group_has(g, people[i])) {
			char n[120];
			vcard_display_name(people[i], 0, n, sizeof(n));
			sb_printf(&b, "%s%s <%s>", b.len ? ", " : "", n, people[i]->email[0].value);
		}
	if (!b.len)
		alert(1, "[1][Nobody in \"%s\" has an email address.][ OK ]", g->name);
	else
		cmd_mail(b.s);
	sb_free(&b);
}

static void group_menu(short mx, short my, int gi)
{
	static const char *const items[] = { "  Send Email to Group", "  Export Group vCards...", "-",
					     "  Rename Group...", "  Delete Group...", "-", "  New Group..." };
	short k = win_popup(mx, my, items, 7);
	GROUP *g = &groups[gi];
	char name[64];
	switch (k) {
	case 0: mail_group(g); break;
	case 1: {
		/* the group chosen, then the menu's export takes what the list shows */
		choose(SRC_GROUP, (short)gi);
		cmd_export();
		break;
	}
	case 3:
		str_copy(name, g->name, sizeof(name));
		if (dlg_ask("Rename Group", "Name:", name, 40) && name[0] && strcmp(name, g->name)) {
			int book = g->book;
			busy(1);
			if (!group_rename(g, name))
				alert(1, "[1][Couldn't rename it:|%s][ OK ]", ab_err);
			busy(0);
			follow_group(name, book);
			after_change();
		}
		break;
	case 4:
		if (alert(2, "[2][Delete the group \"%s\"?|(The people in it stay.)][Delete|Cancel]", g->name) == 1) {
			busy(1);
			if (!group_delete(g))
				alert(1, "[1][Couldn't delete it:|%s][ OK ]", ab_err);
			busy(0);
			if (src_kind == SRC_GROUP)
				list_source(SRC_ALL, 0);
			after_change();
		}
		break;
	case 6: cmd_new_group(g->book); break;
	}
}

static void book_menu(short mx, short my, int b)
{
	const char *items[8];
	short act[8], n = 0, k;
	int a = books[b].acct;
	if (book_writable(&books[b])) {
		items[n] = "  New Group...";
		act[n++] = 1;
		items[n] = "  Import vCards...";
		act[n++] = 2;
		items[n] = opt.default_book == books[b].id ? "\x08 Default for New Contacts" : "  Default for New Contacts";
		act[n++] = 3;
	}
	items[n] = "  Export vCards...";
	act[n++] = 6;
	if (accts[a].kind != AK_LOCAL) {
		items[n] = "-";
		act[n++] = 0;
		items[n] = "  Refresh";
		act[n++] = 4;
	} else {
		items[n] = "-";
		act[n++] = 0;
		items[n] = "  Delete Address Book...";
		act[n++] = 5;
	}
	k = win_popup(mx, my, items, n);
	if (k < 0)
		return;
	switch (act[k]) {
	case 1: cmd_new_group(b); break;
	case 2: cmd_import(b); break;
	case 3:
		opt.default_book = books[b].id;
		store_save_settings();
		break;
	case 4:
		busy(1);
		if (sync_book(&books[b], 1) < 0)
			alert(1, "[1][%s][ OK ]", ab_err);
		busy(0);
		store_save_settings();
		after_change();
		break;
	case 5:
		if (alert(2, "[2][Delete \"%s\" and the %d contacts in it from this Atari?][Delete|Cancel]",
			  books[b].name, books[b].count) == 1) {
			int left = 0, i;
			book_delete(b);
			for (i = 0; i < nbooks; i++)
				left += books[i].acct == a;
			if (!left)
				acct_delete(a);
			list_source(SRC_ALL, 0);
			store_save_settings();
			after_change();
		}
		break;
	case 6:
		choose(SRC_BOOK, (short)b);
		cmd_export();
		break;
	}
}

static void acct_menu(short mx, short my, int a)
{
	static const char *const items[] = { "  Refresh", "  Account Settings...", "-", "  Delete Account..." };
	static const char *const local_items[] = { "  New Address Book...", "-", "  Delete Account..." };
	short k;
	if (accts[a].kind == AK_LOCAL) {
		k = win_popup(mx, my, local_items, 3);
		if (k == 0) {
			char name[40] = "";
			if (dlg_ask("New Address Book", "Name:", name, 30) && name[0]) {
				book_add(a, name);
				store_save_settings();
				after_change();
			}
			return;
		}
		if (k != 2)
			return;
		k = 3;
	} else {
		k = win_popup(mx, my, items, 4);
	}
	switch (k) {
	case 0:
		busy(1);
		if (sync_account(a, 1) < 0)
			alert(1, "[1][%s][ OK ]", ab_err);
		busy(0);
		after_change();
		break;
	case 1:
		dlg_account(a);
		after_change();
		break;
	case 3:
		if (alert(2, "[2][Delete the account \"%s\"|and its contacts from this|Atari? (Nothing is deleted|on the server.)][Delete|Cancel]",
			  accts[a].name) == 1) {
			acct_delete(a);
			list_source(SRC_ALL, 0);
			store_save_settings();
			after_change();
		}
		break;
	}
}

int side_click(short mx, short my, short clicks, short button)
{
	short i;
	side_layout();
	for (i = 0; i < nrows; i++) {
		SROW *s = &srow[i];
		if (my < s->y - 1 || my >= s->y - 1 + row_h)
			continue;
		if (s->kind == -1) {
			if (button & 2)
				acct_menu(mx, my, s->idx);
			else if (clicks == 2 && accts[s->idx].kind != AK_LOCAL) {
				dlg_account(s->idx);
				after_change();
			}
			return 1;
		}
		if (!is_chosen(s))
			choose(s->kind, s->idx);
		if (button & 2) {
			if (s->kind == SRC_GROUP)
				group_menu(mx, my, s->idx);
			else if (s->kind == SRC_BOOK)
				book_menu(mx, my, s->idx);
		} else if (clicks == 2 && s->kind == SRC_GROUP) {
			/* a double click renames, as on the Mac */
			char name[64];
			GROUP *g = &groups[s->idx];
			str_copy(name, g->name, sizeof(name));
			if (dlg_ask("Rename Group", "Name:", name, 40) && name[0] && strcmp(name, g->name)) {
				int book = g->book;
				busy(1);
				if (!group_rename(g, name))
					alert(1, "[1][Couldn't rename it:|%s][ OK ]", ab_err);
				busy(0);
				follow_group(name, book);
				after_change();
			}
		}
		return 1;
	}
	return 1;
}
