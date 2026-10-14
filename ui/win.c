/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * win.c - EContact's window, laid out as the Mac's Contacts: the groups
 * on the left, the list of people in the middle with the search field
 * over it, the chosen person's card on the right; Refresh and the menu
 * at the top right, the status in the info line.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "bidi.h"

short win_h = -1;
GRECT work, r_tool, r_side, r_list, r_card, r_search;

#define KIND (NAME | CLOSER | FULLER | MOVER | SIZER | INFO | UPARROW | DNARROW | VSLIDE)

static char title[80], info[140];

enum { B_SIDE, B_REFRESH, B_MENU, NBUT };
static GRECT but[NBUT];
static const char *const blabel[NBUT] = { " ", "Refresh", "\xF0" };

void win_title(void)
{
	snprintf(title, sizeof(title), " Contacts ");
	if (win_h > 0)
		wind_set_str(win_h, WF_NAME, title);
}

/* after a dialog: the AES doesn't always draw the window's frame again
   where a dialog stood over it */
void win_frame_refresh(void)
{
	if (win_h > 0) {
		wind_set_str(win_h, WF_NAME, title);
		wind_set_str(win_h, WF_INFO, info);
	}
}

/* GEM draws the info line left to right as it is given: Hebrew in it
   goes in visual order, and into the font's letters */
void win_status(const char *s)
{
	char logical[sizeof(info)];
	short n;
	snprintf(logical, sizeof(logical), " %s", s);
	n = (short)strlen(logical);
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (bidi_has_rtl(logical, n)) {
		bidi_visual(logical, n, 0, info);
		info[n] = 0;
		heb_font_map(info, n, opt.hebfont);
	} else {
		str_copy(info, logical, sizeof(info));
	}
	if (win_h > 0)
		wind_set_str(win_h, WF_INFO, info);
}

void win_layout(void)
{
	short th = ch + 8, sw, lw, x, i, bw;
	if (win_h <= 0)
		return;
	wind_get(win_h, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	r_tool = work;
	r_tool.h = th;
	/* as the Mac divides it: a fifth for the groups, the people in
	   about a third of the rest, the card the most */
	sw = opt.sidebar ? 20 * cw : 0;
	if (sw > work.w / 5)
		sw = work.w / 5;
	lw = 30 * cw;
	if (lw > (work.w - sw) * 2 / 5)
		lw = (work.w - sw) * 2 / 5;
	r_side.x = work.x;
	r_side.y = work.y + th;
	r_side.w = sw;
	r_side.h = work.h - th;
	r_list.x = work.x + sw;
	r_list.y = work.y + th;
	r_list.w = lw;
	r_list.h = work.h - th;
	r_card.x = r_list.x + lw;
	r_card.y = work.y + th;
	r_card.w = work.w - sw - lw;
	r_card.h = work.h - th;
	/* the sidebar button on the left; Refresh and the menu on the right */
	x = work.x + 4;
	but[B_SIDE].x = x;
	but[B_SIDE].y = work.y + 3;
	but[B_SIDE].w = 3 * cw;
	but[B_SIDE].h = ch + 2;
	x = work.x + work.w - 4;
	for (i = B_MENU; i >= B_REFRESH; i--) {
		bw = ((short)strlen(blabel[i]) + 2) * cw;
		x -= bw;
		but[i].x = x;
		but[i].y = work.y + 3;
		but[i].w = bw;
		but[i].h = ch + 2;
		x -= cw;
	}
	/* the search field over the list, as on the Mac */
	r_search.x = r_list.x + cw / 2;
	if (r_search.x < but[B_SIDE].x + but[B_SIDE].w + cw)
		r_search.x = but[B_SIDE].x + but[B_SIDE].w + cw;
	r_search.w = r_list.x + r_list.w - cw / 2 - r_search.x;
	r_search.y = work.y + 3;
	r_search.h = ch + 2;
	list_invalidate();
}

void win_sliders(void)
{
	long size, pos;
	if (win_h <= 0)
		return;
	list_slider(&size, &pos);
	wind_set(win_h, WF_VSLSIZE, (short)(size < 30 ? 30 : size > 1000 ? 1000 : size), 0, 0, 0);
	wind_set(win_h, WF_VSLIDE, (short)(pos > 1000 ? 1000 : pos < 0 ? 0 : pos), 0, 0, 0);
}

static void draw_button(int i)
{
	GRECT r = but[i];
	const char *l = blabel[i];
	short n = (short)strlen(l);
	fill(&r, 0);
	frame(&r, 1);
	if (i == B_SIDE) {
		/* a window with its list on the left, the list filled while shown */
		GRECT w = r, pane;
		w.x += cw / 2;
		w.y += 3;
		w.w -= cw;
		w.h -= 6;
		frame(&w, 1);
		pane = w;
		pane.w = w.w / 3;
		if (opt.sidebar)
			fill(&pane, 1);
		line_col(pane.x + pane.w, w.y, pane.x + pane.w, w.y + w.h - 1, 1);
		return;
	}
	text_at(r.x + (r.w - n * cw) / 2, r.y + 1, l, n, n, TX_LTR);
}

/* the search field: a rounded box, a magnifying glass, the text or
   "Search" in grey, and a clear button while there is text */
void win_search_draw(void)
{
	GRECT r = r_search, in;
	short n = (short)strlen(search_text), cols, gx, gy;
	if (r.w < 6 * cw)
		return;
	fill(&r, 0);
	frame(&r, ncolors >= 16 ? grey_color() : 1);
	/* the corners rounded off */
	{
		GRECT c;
		c.w = c.h = 1;
		c.x = r.x;
		c.y = r.y;
		fill(&c, 0);
		c.x = r.x + r.w - 1;
		fill(&c, 0);
		c.y = r.y + r.h - 1;
		fill(&c, 0);
		c.x = r.x;
		fill(&c, 0);
	}
	/* the glass: a ring and a handle */
	gx = r.x + cw / 2 + 3;
	gy = r.y + r.h / 2 - 1;
	{
		short rr = ch >= 16 ? 3 : 2, k;
		short col = text_grey(0);
		for (k = -rr; k <= rr; k++) {
			short d = rr - (k < 0 ? -k : k) / 2;
			line_col(gx - d, gy + k, gx - d, gy + k, col);
			line_col(gx + d, gy + k, gx + d, gy + k, col);
		}
		line_col(gx - rr + 1, gy - rr, gx + rr - 1, gy - rr, col);
		line_col(gx - rr + 1, gy + rr, gx + rr - 1, gy + rr, col);
		line_col(gx + rr, gy + rr, gx + rr + 2, gy + rr + 2, col);
	}
	in = r;
	in.x += 2 * cw;
	in.w -= 3 * cw;
	cols = in.w / cw;
	if (!n) {
		text_at(in.x, r.y + 1, "Search", 6, cols, TX_LTR | TX_GREY);
	} else {
		const char *s = search_text;
		if (n > cols - 1) {
			s += n - (cols - 1);
			n = cols - 1;
		}
		text_at(in.x, r.y + 1, s, n, cols, line_rtl(s, n) ? TX_RIGHT : TX_LTR);
		/* the cursor, and the clear button at the right */
		if (!line_rtl(s, n))
			line_col(in.x + n * cw, r.y + 2, in.x + n * cw, r.y + r.h - 3, 1);
		{
			short cx = r.x + r.w - cw - 2, cy = r.y + r.h / 2, k = ch >= 16 ? 3 : 2;
			disc(cx, cy, k + 2, k + 2, text_grey(1));
			line_col(cx - k + 1, cy - k + 1, cx + k - 1, cy + k - 1, 0);
			line_col(cx - k + 1, cy + k - 1, cx + k - 1, cy - k + 1, 0);
		}
	}
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = r_tool;
	int i;
	if (!rect_and(&r, clip))
		return;
	fill(&r, 0);
	for (i = 0; i < NBUT; i++)
		draw_button(i);
	win_search_draw();
	/* the name of what is shown, between the search and the buttons */
	{
		char t[80];
		short x = r_card.x + cw, w = (but[B_REFRESH].x - cw - x) / cw;
		if (src_kind == SRC_GROUP && src_idx < ngroups)
			str_copy(t, groups[src_idx].name, sizeof(t));
		else if (src_kind == SRC_BOOK && src_idx < nbooks)
			str_copy(t, books[src_idx].name, sizeof(t));
		else
			str_copy(t, "All Contacts", sizeof(t));
		if (w > 0)
			text_at(x, r.y + 4, t, (long)strlen(t), w, TX_BOLD | (line_rtl(t, (long)strlen(t)) ? 0 : TX_LTR));
	}
	hline(work.x, work.x + work.w - 1, work.y + r_tool.h - 1);
}

static void draw_all(GRECT *clip)
{
	GRECT r;
	clip_on(clip);
	draw_toolbar(clip);
	r = r_side;
	if (r.w && rect_and(&r, clip)) {
		side_draw(clip);
		clip_on(clip);
		vline(r_side.x + r_side.w - 1, r_side.y, r_side.y + r_side.h - 1);
	}
	r = r_list;
	if (rect_and(&r, clip)) {
		list_draw(&r);
		clip_on(clip);
		vline(r_list.x + r_list.w - 1, r_list.y, r_list.y + r_list.h - 1);
	}
	r = r_card;
	if (rect_and(&r, clip))
		card_draw(&r);
	clip_off();
}

void win_redraw(GRECT *area)
{
	GRECT box, full;
	if (win_h <= 0)
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	full = work;
	if (area && !rect_and(&full, area)) {
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		GRECT d = box;
		if (rect_and(&d, &full))
			draw_all(&d);
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	win_sliders();
}

short win_popup(short x, short y, const char *const *labels, short n)
{
	short k = popup(x, y, labels, n);
	win_redraw(&popup_area);
	return k;
}

/* where the window goes so its work area starts on a 16-pixel boundary:
   VDI draws byte-aligned text and fills faster */
static short snap_x(short x)
{
	short wx, wy, ww, wh;
	wind_calc(WC_WORK, KIND, x, 100, 400, 200, &wx, &wy, &ww, &wh);
	x -= (wx & 15);
	if (x < desk_x)
		x += 16;
	return x;
}

void win_open(void)
{
	short x, y, w, h;
	if (win_h > 0) {
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
		return;
	}
	win_h = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (win_h <= 0) {
		win_h = -1;
		alert(1, "[3][No window left for Contacts:|close one and try again.][ OK ]");
		return;
	}
	win_title();
	wind_set_str(win_h, WF_INFO, info);
	x = opt.win_x;
	y = opt.win_y;
	w = opt.win_w;
	h = opt.win_h;
	if (w < 50 * cw || h < 14 * ch || x < desk_x || y < desk_y ||
	    x + w > desk_x + desk_w + 8 || y + h > desk_y + desk_h + 8) {
		x = desk_x + (desk_w > 80 * cw ? desk_w / 20 : 0);
		y = desk_y + (desk_h > 30 * ch ? ch : 0);
		w = desk_w - 2 * (x - desk_x);
		h = desk_h - 2 * (y - desk_y);
	}
	{
		short sx = snap_x(x);
		if (sx + w > desk_x + desk_w)
			w = desk_x + desk_w - sx;
		x = sx;
	}
	wind_open(win_h, x, y, w, h);
	win_layout();
	win_sliders();
}

static void remember_place(void)
{
	short x, y, w, h;
	if (win_h <= 0)
		return;
	wind_get(win_h, WF_CURRXYWH, &x, &y, &w, &h);
	if (x != opt.win_x || y != opt.win_y || w != opt.win_w || h != opt.win_h) {
		opt.win_x = x;
		opt.win_y = y;
		opt.win_w = w;
		opt.win_h = h;
		settings_later();
	}
}

/* the toolbar and the card's buttons act on the first click */
int win_instant(short mx, short my)
{
	return rect_hit(&r_tool, mx, my) || card_instant(mx, my);
}

void win_close(void)
{
	if (win_h <= 0)
		return;
	remember_place();
	closing_down(quit_now || !is_acc);
	wind_close(win_h);
	wind_delete(win_h);
	win_h = -1;
}

void win_message(short *msg)
{
	GRECT r;
	if (msg[3] != win_h || win_h <= 0)
		return;
	switch (msg[0]) {
	case WM_REDRAW:
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		win_redraw(&r);
		break;
	case WM_TOPPED:
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		if (!is_acc)
			quit_now = 1;
		win_close();
		break;
	case WM_BOTTOMED:
		wind_set(win_h, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_FULLED: {
		short x, y, w, h, fx, fy, fw, fh;
		wind_get(win_h, WF_CURRXYWH, &x, &y, &w, &h);
		wind_get(win_h, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		if (x == fx && y == fy && w == fw && h == fh)
			wind_get(win_h, WF_PREVXYWH, &fx, &fy, &fw, &fh);
		wind_set(win_h, WF_CURRXYWH, fx, fy, fw, fh);
		win_layout();
		win_redraw(0);
		remember_place();
		break;
	}
	case WM_SIZED:
	case WM_MOVED: {
		short w = msg[6] < 44 * cw ? 44 * cw : msg[6], h = msg[7] < 12 * ch ? 12 * ch : msg[7];
		short sx = snap_x(msg[4]);
		if (sx + w > desk_x + desk_w)
			w = desk_x + desk_w - sx;
		wind_set(win_h, WF_CURRXYWH, sx, msg[5], w, h);
		win_layout();
		if (msg[0] == WM_SIZED)
			win_redraw(0);
		remember_place();
		break;
	}
	case WM_ARROWED:
		/* 0 page up, 1 page down, 2 line up, 3 line down */
		switch (msg[4]) {
		case 0: list_scroll(-(r_list.h / (ch + 2)) + 1); break;
		case 1: list_scroll(r_list.h / (ch + 2) - 1); break;
		case 2: list_scroll(-1); break;
		case 3: list_scroll(1); break;
		}
		break;
	case WM_VSLID:
		list_slide(msg[4]);
		break;
	}
}

static void toggle_sidebar(void)
{
	opt.sidebar = !opt.sidebar;
	settings_later();
	win_layout();
	win_redraw(0);
}

/* the search changed: the list again, the card if the choice went */
static void search_changed(void)
{
	list_invalidate();
	list_fix_selection();
	status_updated();		/* "2 of 21 contacts" */
	{
		GRECT r = r_search;
		r.w++;
		r.h++;
		win_redraw(&r);
	}
	win_redraw(&r_list);
	win_redraw(&r_card);
}

int win_mouse(short mx, short my, short button, short clicks, short kstate)
{
	int i;
	if (win_h <= 0 || !rect_hit(&work, mx, my))
		return 0;
	if (rect_hit(&r_tool, mx, my)) {
		for (i = 0; i < NBUT; i++)
			if (rect_hit(&but[i], mx, my))
				break;
		switch (i) {
		case B_SIDE: toggle_sidebar(); break;
		case B_REFRESH: cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT)); break;
		case B_MENU: cmd_menu(but[B_MENU].x, but[B_MENU].y + but[B_MENU].h); break;
		default:
			/* the clear button in the search field */
			if (rect_hit(&r_search, mx, my) && search_text[0] && mx >= r_search.x + r_search.w - 2 * cw) {
				search_text[0] = 0;
				search_changed();
			}
		}
		return 1;
	}
	if (r_side.w && rect_hit(&r_side, mx, my))
		return side_click(mx, my, clicks, button);
	if (rect_hit(&r_list, mx, my))
		return list_click(mx, my, clicks, button, kstate);
	if (rect_hit(&r_card, mx, my))
		return card_click(mx, my, clicks, button);
	return 0;
}

/* typing goes into the search field, as on the Mac */
static int search_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char a = key_char(kstate, key);
	int n = (int)strlen(search_text);
	if (kstate & (K_CTRL | K_ALT))
		return 0;
	if (scan == 0x0e) {			/* Backspace */
		if (!n)
			return 1;
		search_text[n - 1] = 0;
	} else if (scan == 0x01) {		/* Esc */
		if (!n)
			return 0;
		search_text[0] = 0;
	} else if (a >= ' ' && a != 0x7f && scan != 0x72 && scan != 0x1c && scan != 0x0f) {
		if (a == ' ' && !n)
			return 0;
		if (n >= (int)sizeof(search_text) - 1)
			return 1;
		search_text[n] = (char)a;
		search_text[n + 1] = 0;
	} else {
		return 0;
	}
	search_changed();
	return 1;
}

int win_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	if (kstate & K_CTRL) {
		switch (scan) {
		case 0x31: cmd_new_contact(); return 1;			/* ^N */
		case 0x13: cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT)); return 1;	/* ^R */
		case 0x21:						/* ^F: a new search */
			search_text[0] = 0;
			search_changed();
			status("Type to search: names, companies, emails, phones, notes");
			return 1;
		case 0x12:						/* ^E: edit */
			if (sel_contact())
				editor_open(sel_contact());
			return 1;
		case 0x22:						/* ^G: a new group */
			cmd_new_group(-1);
			return 1;
		case 0x11:						/* ^W */
		case 0x10:						/* ^Q */
			if (!is_acc)
				quit_now = 1;
			win_close();
			return 1;
		case 0x1f:						/* Shift+^S; ^S is Save */
			if (!(kstate & (K_LSHIFT | K_RSHIFT)))
				return 0;
			toggle_sidebar();
			return 1;
		}
	}
	if (list_key(kstate, key))
		return 1;
	return search_key(kstate, key);
}
