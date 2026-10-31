/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * win.c - EContact's window, laid out as the Mac's Contacts: the groups
 * on the left, the list of people in the middle with the search field
 * over it, the chosen person's card on the right; Refresh and the menu
 * at the top right, the status in the info line.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "../src/plat.h"
#include "bidi.h"
#include "iconify.h"
#include "av.h"
#include "clip.h"
#include "dialog.h"
#include <tos.h>

short win_h = -1;
GRECT work, r_tool, r_side, r_list, r_card, r_search;

/* the window's own slider and arrows are the card's, the pane at its
   right edge (the guidelines 5.1: the AES keeps the column anyway); the
   list in the middle has its own bar beside it (list.c, panebar.c) */
#define KIND (NAME | CLOSER | FULLER | MOVER | SIZER | INFO | VSLIDE | UPARROW | DNARROW)

static char title[80], info[140];

enum { B_SIDE, B_REFRESH, B_MENU, NBUT };
static GRECT but[NBUT];
static const char *const blabel[NBUT] = { " ", "Refresh", "\xF0" };
static const char *const pick_label[NBUT] = { " ", "Cancel", "Choose" };

static const char *label(int i)
{
	return picking ? pick_label[i] : blabel[i];
}

short win_iconified;	/* shown as EContact's icon (WM_ICONIFY) */
/* the search field has the focus: clicked or typed into, until a click
   elsewhere in the window */
static short search_focus;
short win_hold;

/* with the iconifier when the AES can iconify (common/ui/iconify.c) */
static short kind(void)
{
	return icon_kind(KIND);
}

void win_title(void)
{
	snprintf(title, sizeof(title), " EContact ");
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

/* The buffer holds the whole work area as it was last drawn: then what
   the AES asks for again (WM_REDRAW: a dialog, an alert or another
   window gone from over it) is copied from the buffer, not drawn again
   (Settings' area took an 8 MHz ST 1 s to draw: speed.py, 26 Oct 2026).
   Every change is drawn through it (win_redraw) and a scroll moves it
   too; a new layout, a move or the window closed forget it. */
static int obuf_whole;

/* A part drawn later, a band a turn (win_redraw_later: the card when
   another contact is chosen, 0.24 s of an ST's time): into the buffer,
   then copied when it is all there; the screen keeps what it shows till
   then. Forgotten with the layout. The whole window once the program's
   books are read (win_redraw_bands: 1.2 s of an ST's) is drawn a pane
   at a time, a line of it a band (a line across the window was 120 ms),
   each copied when drawn (later_prog), and the list's bar at the end:
   TOS 1.04's objc_draw takes 50 ms for any piece of it. A part asked for
   meanwhile is drawn again only when its pane was drawn already. */
static GRECT later;
static short later_y;
static int later_on, later_whole, later_prog;
static short later_bh, part_list;	/* (win_redraw_bands) a band's height, the list's part */
#define PARTS 6
static GRECT part[PARTS];
static short nparts, part_i;

void win_layout(void)
{
	short th = ch + 8, sw, lw, x, i, bw;
	obuf_whole = 0;
	later_on = 0;
	if (win_h <= 0 || win_iconified)
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
	/* on 8-pixel steps, as the work area is (snap_x): every pane's text
	   on a byte, where TOS draws it some six times as fast (the
	   guidelines' Speed: text on a byte) */
	sw &= ~7;
	lw &= ~7;
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
	x = work.x + 8;
	but[B_SIDE].x = x;
	but[B_SIDE].y = work.y + 3;
	but[B_SIDE].w = 3 * cw;
	but[B_SIDE].h = ch + 2;
	x = (work.x + work.w - 4) & ~7;	/* (the labels on a byte) */
	for (i = B_MENU; i >= B_REFRESH; i--) {
		bw = ((short)strlen(label(i)) + 2) * cw;
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

static void draw_button(int i)
{
	GRECT r = but[i];
	const char *l = label(i);
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
   "Search" in grey, and a clear button while there is text; with the
   focus a black frame and the text cursor */
void win_search_draw(void)
{
	GRECT r = r_search, in;
	short n = (short)strlen(search_text), cols, gx, gy;
	if (r.w < 6 * cw)
		return;
	fill(&r, 0);
	frame(&r, ncolors >= 16 && !search_focus ? grey_color() : 1);
	if (search_focus) {
		GRECT f = r;
		f.x++;
		f.y++;
		f.w -= 2;
		f.h -= 2;
		frame(&f, 1);
	}
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
	in.x = (r.x + 2 * cw + 7) & ~7;	/* on a byte */
	in.w = r.x + r.w - cw - in.x;
	cols = in.w / cw;
	if (!n) {
		text_at(in.x + (search_focus ? cw / 2 : 0), r.y + 1, "Search", 6, cols, TX_LTR | TX_GREY);
		if (search_focus)
			line_col(in.x, r.y + 2, in.x, r.y + r.h - 3, 1);
	} else {
		const char *s = search_text;
		if (n > cols - 1) {
			s += n - (cols - 1);
			n = cols - 1;
		}
		text_at(in.x, r.y + 1, s, n, cols, line_rtl(s, n) ? TX_RIGHT : TX_LTR);
		/* the cursor, and the clear button at the right */
		if (search_focus && !line_rtl(s, n))
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
			text_at(x, r_tool.y + 4, t, (long)strlen(t), w, TX_BOLD | (line_rtl(t, (long)strlen(t)) ? 0 : TX_LTR));
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

/* ---- drawing off the screen (ECal's win.c) ----
 * The window is many small fills, lines and texts; drawn straight onto
 * the screen, a slow machine shows them appearing one by one, and every
 * step of scrolling the list blanked it and drew it again (Erez's video,
 * 22 Oct 2026). So the window is drawn into a screen-sized buffer and
 * copied to the screen at once. Two ways to draw there:
 *  - an off-screen bitmap (EdDI v_opnbm: NVDI 3 and later, fVDI...): a
 *    VDI workstation of its own over our buffer; the drawing code's
 *    vdi_h is that one while it draws. Safe under any AES.
 *  - with TOS's own VDI, which draws at the logical screen base: the
 *    buffer is made the logical screen (Setscreen) while drawing, the
 *    shown screen staying. Only under single-tasking TOS on the machine's
 *    own video, nothing else being able to draw meanwhile.
 * Either is used only when a test at start-up finds a line drawn that
 * way really in the buffer; otherwise the window is drawn as before. */
static void *obuf;		/* the buffer, NULL: draw straight on the screen */
static MFDB obuf_mfdb;
static short bm_h;		/* the off-screen bitmap's workstation, 0: none */
static const char *drawing = "straight on the screen";

const char *win_drawing(void)
{
	return drawing;
}

static long xbios_setscreen(void *log)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w #-1,-(%%sp)\n\tmove.l #-1,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
			 "move.w #5,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(log) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

static void *xbios_base(short fn)	/* 2 Physbase, 3 Logbase */
{
	return (void *)trap14_w(fn);
}

/* EdDI's off-screen bitmaps (VDI 100 and 101, sub-opcode 1), not in
   common/'s bindings */
static short bc[12], bin[20], bout[57], bpin[4], bpout[12];
static void *bpb[5] = { bc, bin, bpin, bout, bpout };

static void vdi_bm(void)
{
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
			 : : "g"((long)bpb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

static short v_opnbm(short screen, MFDB *m)
{
	short i;
	memset(bc, 0, sizeof(bc));
	for (i = 0; i < 10; i++)
		bin[i] = 1;
	bin[10] = 2;			/* raster coordinates */
	bin[11] = m->fd_w - 1;
	bin[12] = m->fd_h - 1;
	for (i = 13; i < 20; i++)
		bin[i] = 0;		/* the screen's pixel size and format */
	bc[0] = 100;
	bc[3] = 20;
	bc[5] = 1;
	bc[6] = screen;
	bc[7] = (short)((unsigned long)m >> 16);
	bc[8] = (short)((unsigned long)m & 0xffff);
	vdi_bm();
	return bc[6] != screen ? bc[6] : 0;
}

static void v_clsbm(short h)
{
	memset(bc, 0, sizeof(bc));
	bc[0] = 101;
	bc[5] = 1;
	bc[6] = h;
	vdi_bm();
}

/* a short black line at the top left, drawn with handle h: does it land
   in the buffer b? */
static int lands_in(unsigned char *b, short h, int setscreen)
{
	void *screen = xbios_base(3);
	short keep = vdi_h, i, changed = 0;
	GRECT r;
	for (i = 0; i < 64; i++)
		b[i] = 0x55;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	if (setscreen)
		xbios_setscreen(b);
	vdi_h = h;
	draw_forget();
	r.x = 0;
	r.y = 0;
	r.w = 16;
	r.h = 1;
	clip_on(&r);
	fill(&r, 1);
	clip_off();
	vdi_h = keep;
	draw_forget();
	if (setscreen)
		xbios_setscreen(screen);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	for (i = 0; i < 2 * planes && i < 64; i++)
		changed |= b[i] != 0x55;
	return changed;
}

/* at start-up, before the accessory's first event call (its memory):
   free, what was free at boot for the accessory (0: the program). The
   accessory takes the buffer only when it is a sixteenth of that at
   most: on a 1 MB ST in ST high 32 KB, the memory being the contacts'
   (Erez) */
void win_buffer_init(long free)
{
	long size = (long)((scr_w + 15) / 16) * 2 * planes * scr_h;
	unsigned char *p, *b;
	int multitasking = get_cookie(0x4D694E54L) || get_cookie(0x4D616758L);	/* MiNT, MagiC */
#ifdef WIN_DIRECT
	(void)free;			/* the speed rig's other way: straight on the screen */
	(void)multitasking;
	return;
#endif
	if (planes < 1 || size > 640L * 1024 || (free && size > free / 16)) {
		drawing = "straight on the screen (no memory for a buffer)";
		return;
	}
	p = Mxalloc(size + 256, 0);			/* ST RAM, where the blitter reaches */
	if ((long)p == -32 || (long)p == 0)		/* no Mxalloc: plain Malloc is ST RAM */
		p = (unsigned char *)trap1_wl(0x48, size + 256);
	if ((long)p <= 0)
		return;
	b = (unsigned char *)(((unsigned long)p + 255) & ~255UL);
	memset(&obuf_mfdb, 0, sizeof(obuf_mfdb));
	obuf_mfdb.fd_addr = b;
	obuf_mfdb.fd_w = (scr_w + 15) & ~15;
	obuf_mfdb.fd_h = scr_h;
	obuf_mfdb.fd_wdwidth = (scr_w + 15) / 16;
	obuf_mfdb.fd_nplanes = planes;
	if (get_cookie(0x45644449L)) {			/* EdDI */
		MFDB m = obuf_mfdb;
		short h = v_opnbm(vdi_h, &m);
		if (h > 0 && lands_in(b, h, 0)) {
			bm_h = h;
			win_buffer_fonts();
			obuf = b;
			drawing = "off the screen, in an EdDI bitmap";
			return;
		}
		if (h > 0)
			v_clsbm(h);
	}
	if (!multitasking && xbios_base(3) == xbios_base(2) && lands_in(b, vdi_h, 1)) {
		obuf = b;
		drawing = "off the screen, through the logical screen";
		return;
	}
	trap1_wl(0x49, (long)p);			/* Mfree */
}

/* the program's end: the bitmap's workstation closed (the buffer goes
   with the program's memory) */
void win_buffer_end(void)
{
	if (bm_h)
		v_clsbm(bm_h);
	bm_h = 0;
	obuf = 0;
}

/* the GDOS fonts loaded on the window's workstation since: on the
   bitmap too */
void win_buffer_fonts(void)
{
	static int loaded;
	if (bm_h && !loaded && font_gdos_loaded()) {
		vst_load_fonts(bm_h, 0);
		loaded = 1;
	}
}

/* each piece of the window in full shown: copied from the buffer, the
   list's bar drawn over it (AES objects: on the screen, 5.1) */
static void copy_pieces(const GRECT *full)
{
	GRECT box;
	MFDB scr;
	memset(&scr, 0, sizeof(scr));
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		GRECT d = box;
		if (rect_and(&d, full)) {
			short pxy[8];
			pxy[0] = pxy[4] = d.x;
			pxy[1] = pxy[5] = d.y;
			pxy[2] = pxy[6] = d.x + d.w - 1;
			pxy[3] = pxy[7] = d.y + d.h - 1;
			/* NVDI clips raster copies too (TOS's VDI doesn't):
			   the piece's own, not one left by a dialog */
			clip_on(&d);
			vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &scr);	/* S_ONLY */
			list_bar_draw(&d);
		}
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	clip_off();
}

/* the buffer is drawn in now (TOS's way: only while its logical screen
   is the one shown) */
static int buffered(void)
{
	return obuf && (bm_h || xbios_base(3) == xbios_base(2));
}

void win_redraw(GRECT *area)
{
	GRECT box, full;
	if (win_h <= 0 || win_hold)
		return;
	if (win_iconified) {
		icon_window(area);	/* over the small window, not the old work area */
		return;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	full = work;
	if (area && !rect_and(&full, area)) {
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	if (buffered()) {
		/* all of it off the screen, then each visible piece copied */
		if (bm_h) {
			short keep = vdi_h;
			vdi_h = bm_h;
			draw_forget();
			font_reselect();	/* the window's font, on the bitmap too */
			draw_all(&full);
			vdi_h = keep;
			draw_forget();
		} else {
			void *screen = xbios_base(3);
			xbios_setscreen(obuf);
			draw_all(&full);
			xbios_setscreen(screen);
		}
		if (later_on && full.x <= later.x && full.y <= later.y && full.x + full.w >= later.x + later.w &&
		    full.y + full.h >= later.y + later.h) {
			later_on = 0;	/* drawn now, all of it */
			obuf_whole = later_whole;
		}
		if (full.x == work.x && full.y == work.y && full.w == work.w && full.h == work.h)
			obuf_whole = 1;
		copy_pieces(&full);
	} else {
		wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h) {
			GRECT d = box;
			if (rect_and(&d, &full)) {
				draw_all(&d);
				list_bar_draw(&d);
			}
			wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	win_slider();		/* the card's height is known once it is drawn */
}

/* the window moved, its size the same: its picture moved in the buffer
   too, so what the move brings into view is copied (when both places
   are on the screen: the buffer is the screen's size) */
static void buffer_moved(const GRECT *was)
{
	short pxy[8];
	if (!buffered() || was->w != work.w || was->h != work.h ||
	    was->x < 0 || was->y < 0 || was->x + was->w > scr_w || was->y + was->h > scr_h ||
	    work.x < 0 || work.y < 0 || work.x + work.w > scr_w || work.y + work.h > scr_h)
		return;
	pxy[0] = was->x;
	pxy[1] = was->y;
	pxy[2] = was->x + was->w - 1;
	pxy[3] = was->y + was->h - 1;
	pxy[4] = work.x;
	pxy[5] = work.y;
	pxy[6] = work.x + work.w - 1;
	pxy[7] = work.y + work.h - 1;
	wind_update(BEG_UPDATE);
	clip_off();
	vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &obuf_mfdb);	/* S_ONLY */
	wind_update(END_UPDATE);
	obuf_whole = 1;
}

static void copy_band(const GRECT *b);

/* while the window is drawn a pane at a time (win_redraw_bands): what is
   drawn of it from the buffer, the rest white until its band comes, the
   list's bar at the end (the whole copied with the bar took an 8 MHz ST
   250 ms) */
static void uncover_bands(const GRECT *full)
{
	GRECT box, d;
	short k;
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		d = box;
		if (rect_and(&d, full)) {
			clip_on(&d);
			fill(&d, 0);
		}
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	clip_off();
	for (k = 0; k <= part_i && k < nparts; k++) {
		d = part[k];
		if (k == part_i)
			d.h = later_y - d.y;
		if (d.h > 0 && rect_and(&d, full))
			copy_band(&d);
	}
}

/* what the AES asks for again: from the buffer when it holds it all, or
   what is drawn of it while it is drawn a pane at a time */
static void win_uncover(GRECT *area)
{
	GRECT full = work;
	int bands = later_on && later_prog;
	if (win_h <= 0 || win_hold || win_iconified || !(obuf_whole || bands) || !buffered()) {
		win_redraw(area);
		return;
	}
	if (!rect_and(&full, area))
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	if (bands)
		uncover_bands(&full);
	else
		copy_pieces(&full);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

/* area drawn into the buffer, nothing copied (the buffer is in use) */
static void draw_off(GRECT *area)
{
	if (bm_h) {
		short keep = vdi_h;
		vdi_h = bm_h;
		draw_forget();
		font_reselect();
		draw_all(area);
		vdi_h = keep;
		draw_forget();
	} else {
		void *screen = xbios_base(3);
		xbios_setscreen(obuf);
		draw_all(area);
		xbios_setscreen(screen);
	}
}

void win_redraw_later(const GRECT *area)
{
	GRECT a = work;
	if (win_h <= 0 || win_hold || win_iconified || !buffered()) {
		win_redraw((GRECT *)area);
		return;
	}
	if (!rect_and(&a, area))
		return;
	if (later_on && later_prog) {
		short k;
		for (k = part_i; k < nparts; k++) {
			GRECT o = part[k];
			if (rect_and(&o, &a) && o.x == a.x && o.y == a.y && o.w == a.w && o.h == a.h &&
			    (k > part_i || a.y >= later_y))
				return;		/* within what is still to come */
		}
		if (nparts < PARTS) {
			part[nparts++] = a;
			return;
		}
	}
	if (!later_on) {
		later_whole = obuf_whole;
		later_prog = 0;
	} else {
		/* another part too (a search's list and card): both, from the
		   top again (nothing of them is on the screen yet) */
		short x2 = later.x + later.w > a.x + a.w ? later.x + later.w : a.x + a.w;
		short y2 = later.y + later.h > a.y + a.h ? later.y + later.h : a.y + a.h;
		a.x = later.x < a.x ? later.x : a.x;
		a.y = later.y < a.y ? later.y : a.y;
		a.w = x2 - a.x;
		a.h = y2 - a.y;
	}
	later = a;
	later_y = a.y;
	later_on = 1;
	obuf_whole = 0;		/* (the buffer has the new part's bands) */
}

void win_redraw_bands(void)
{
	const GRECT *pane[4];
	short k;
	later_on = 0;
	win_redraw_later(&work);
	if (!later_on)
		return;
	later_prog = 1;
	later_whole = 1;	/* (all of it in the buffer at the end) */
	pane[0] = &r_tool;
	pane[1] = &r_side;
	pane[2] = &r_list;
	pane[3] = &r_card;
	nparts = part_i = 0;
	part_list = -1;
	for (k = 0; k < 4; k++) {
		GRECT o = *pane[k];
		if (rect_and(&o, &work)) {
			if (k == 2)
				part_list = nparts;
			part[nparts++] = o;
		}
	}
	later_y = nparts ? part[0].y : later.y + later.h;
	later_bh = ch;
}

/* The accessory's window as it opens, its books read: drawn a pane at a
   time as win_redraw_bands does (in one piece it was 1.27 s of an 8 MHz
   ST's: speed.py, 27 Oct 2026), the AES's WM_REDRAW meanwhile showing
   what is drawn (uncover_bands). Without the buffer the WM_REDRAW draws
   it whole. */
void win_open_bands(void)
{
	if (win_h <= 0 || win_hold || win_iconified || !buffered())
		return;
	obuf_whole = 0;
	win_redraw_bands();
}

int win_later_pending(void)
{
	return later_on;
}

/* the whole window drawn a pane at a time (the accessory's opening, the
   program's start), not a part of it later: for AES_PROFILE's notes */
int win_later_whole(void)
{
	return later_on && later_prog;
}

/* a band drawn in the buffer, on the screen: each piece but the list's
   bar, which keeps what it showed */
static void blit(const GRECT *d)
{
	MFDB scr;
	short pxy[8];
	if (d->w <= 0 || d->h <= 0)
		return;
	memset(&scr, 0, sizeof(scr));
	pxy[0] = pxy[4] = d->x;
	pxy[1] = pxy[5] = d->y;
	pxy[2] = pxy[6] = d->x + d->w - 1;
	pxy[3] = pxy[7] = d->y + d->h - 1;
	clip_on((GRECT *)d);
	vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &scr);	/* S_ONLY */
}

static void copy_band(const GRECT *b)
{
	GRECT box, bar;
	list_bar_area(&bar);
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		GRECT d = box, o;
		if (rect_and(&d, b)) {
			o = d;
			if (!bar.w || !rect_and(&o, &bar)) {
				blit(&d);
			} else {
				GRECT p = d;
				p.h = o.y - d.y;		/* above the bar */
				blit(&p);
				p.y = o.y + o.h;		/* under it */
				p.h = d.y + d.h - p.y;
				blit(&p);
				p = o;				/* beside it */
				p.x = d.x;
				p.w = o.x - d.x;
				blit(&p);
				p.x = o.x + o.w;
				p.w = d.x + d.w - p.x;
				blit(&p);
			}
		}
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	clip_off();
}

/* bands of it while more() says so (one at least); copied when done */
void win_later_slice(int (*more)(void))
{
	short band = 2 * ch;
	if (!later_on)
		return;
	if (win_h <= 0 || win_iconified || !buffered()) {
		later_on = 0;
		return;
	}
	if (later_prog) {
		GRECT av;
		if (part_i >= nparts) {
			/* all drawn: the bar, in a turn of its own */
			GRECT bar;
			later_on = 0;
			obuf_whole = later_whole;
			list_bar_area(&bar);
			win_bars(&bar);
			win_slider();
			return;
		}
		wind_update(BEG_UPDATE);
		graf_mouse(M_OFF, 0);
		do {
			GRECT *p = &part[part_i], b = *p;
			unsigned long t = pf_ms();
			b.y = later_y;
			b.h = part_i ? later_bh : p->h;	/* the toolbar's buttons in one */
			if (part_i == part_list)
				b.h = list_band(b.y, b.h);
			if (b.h > p->y + p->h - later_y)
				b.h = p->y + p->h - later_y;
			card_avatar_area(&av);
			if (av.h && b.y < av.y + av.h && b.y + b.h > av.y && av.y + av.h > b.y + b.h &&
			    b.x < av.x + av.w && b.x + b.w > av.x)
				b.h = (av.y + av.h < p->y + p->h ? av.y + av.h : p->y + p->h) - b.y;
			draw_off(&b);
			copy_band(&b);
			later_y += b.h;
			/* much of a band's cost is the band's, whatever its
			   height: twice as many lines while a band is quick, half
			   when it isn't (a band of a line each was 2.9 s of an
			   8 MHz ST's for the window, in one piece 1.27 s); one
			   line in a new pane */
			t = pf_ms() - t;
			if (t <= 60 && later_bh < 8 * ch)
				later_bh *= 2;
			else if (t > 120 && later_bh > ch)
				later_bh /= 2;
			if (later_y >= p->y + p->h && ++part_i < nparts) {
				later_y = part[part_i].y;
				later_bh = ch;
			}
		} while (part_i < nparts && (!more || more()));
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	do {
		GRECT b = later, av;
		b.y = later_y;
		b.h = later.y + later.h - later_y < band ? later.y + later.h - later_y : band;
		/* a band that cuts the round picture takes the rest of it:
		   its filled circle costs the VDI the same in any part */
		card_avatar_area(&av);
		if (av.h && b.y < av.y + av.h && b.y + b.h > av.y && av.y + av.h > b.y + b.h)
			b.h = (av.y + av.h < later.y + later.h ? av.y + av.h : later.y + later.h) - b.y;
		draw_off(&b);
		later_y += b.h;
	} while (later_y < later.y + later.h && (!more || more()));
	if (later_y >= later.y + later.h) {
#ifdef AES_PROFILE
		unsigned long t0 = tos_ms();
		char m[48];
#endif
		later_on = 0;
		copy_pieces(&later);
		obuf_whole = later_whole;
#ifdef AES_PROFILE
		snprintf(m, sizeof(m), "LATERCOPY %lu ms\n", tos_ms() - t0);
		nf_debug(m);
#endif
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	if (!later_on)
		win_slider();
}

void win_later_finish(void)
{
	while (later_on)
		win_later_slice(0);
}

static short sl_size = -1, sl_pos = -1;	/* the window's slider as set */

void win_slider(void)
{
	short total, shown, top, size, pos;
	if (win_h <= 0 || win_iconified)
		return;
	card_scroll_info(&total, &shown, &top);
	if (total <= shown || shown <= 0) {
		size = 1000;
		pos = 0;
	} else {
		size = (short)((long)shown * 1000 / total);
		pos = (short)((long)top * 1000 / (total - shown));
	}
	if (size < 1)
		size = 1;
	if (size != sl_size)
		wind_set(win_h, WF_VSLSIZE, size, 0, 0, 0);
	if (pos != sl_pos)
		wind_set(win_h, WF_VSLIDE, pos, 0, 0, 0);
	sl_size = size;
	sl_pos = pos;
}

/* the content of a moves up by dy pixels, down when dy < 0: copied on
   the screen when all of a shows, then only the strip that comes in is
   drawn; otherwise a is drawn again (ECal's win_scroll) */
void win_scroll(const GRECT *a, short dy)
{
	GRECT box, strip = *a;
	short ady = dy < 0 ? -dy : dy, whole = 0;
	if (win_h <= 0 || win_iconified || win_hold)
		return;
	if (later_on) {
		GRECT o = later;
		if (rect_and(&o, a))
			win_later_finish();	/* the screen shows it before it moves */
	}
	wind_update(BEG_UPDATE);
	if (ady < a->h) {
		wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h && !whole) {
			whole = box.x <= a->x && box.y <= a->y && box.x + box.w >= a->x + a->w &&
				box.y + box.h >= a->y + a->h;
			wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
	}
	if (whole) {
		MFDB scr;
		short pxy[8];
		memset(&scr, 0, sizeof(scr));
		pxy[0] = pxy[4] = a->x;
		pxy[2] = pxy[6] = a->x + a->w - 1;
		pxy[1] = a->y + (dy > 0 ? ady : 0);
		pxy[3] = pxy[1] + a->h - ady - 1;
		pxy[5] = a->y + (dy > 0 ? 0 : ady);
		pxy[7] = pxy[5] + a->h - ady - 1;
		graf_mouse(M_OFF, 0);
		clip_on((GRECT *)a);	/* NVDI clips raster copies */
		/* (the VDI's copy: common's scr_rows takes the CPU only for a
		   box 384 pixels wide or more on a 68000, and the list is 240 at
		   most; by the CPU its 192 made the arrow's line 215 ms on an ST
		   against 145: the speed rig, 31 Oct 2026) */
		vro_cpyfm(vdi_h, 3, pxy, &scr, &scr);	/* S_ONLY */
		if (obuf_whole)
			vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &obuf_mfdb);	/* the buffer as the screen */
		clip_off();
		graf_mouse(M_ON, 0);
		strip.h = ady;
		if (dy > 0)
			strip.y = a->y + a->h - ady;
	}
	wind_update(END_UPDATE);
	win_redraw(&strip);
}

void win_bars(const GRECT *a)
{
	GRECT box;
	if (win_h <= 0 || win_iconified || win_hold)
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		GRECT d = box;
		if (rect_and(&d, a))
			list_bar_draw(&d);
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
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
	win_h = wind_create(kind(), desk_x, desk_y, desk_w, desk_h);
	win_iconified = 0;
	search_focus = 0;
	if (win_h <= 0) {
		win_h = -1;
		alert(1, "[3][No window left for EContact:|close one and try again.][ OK ]");
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
	sl_size = sl_pos = -1;		/* a new window's slider */
	wind_open(win_h, x, y, w, h);
	if (is_acc)		/* the desktop drops files on it (VA_DRAGACCWIND) */
		av_window(win_h, 1);
	win_layout();
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

/* the toolbar, the card's buttons and the list's scroll bar act on the
   first click */
int win_instant(short mx, short my)
{
	return rect_hit(&r_tool, mx, my) || card_instant(mx, my) || list_bar_hit(mx, my);
}

void win_close(void)
{
	if (win_h <= 0)
		return;
	if (!win_iconified)	/* the place was kept when it was iconified */
		remember_place();
	closing_down(quit_now || !is_acc);
	if (is_acc)
		av_window(win_h, 0);
	wind_close(win_h);
	wind_delete(win_h);
	win_h = -1;
	obuf_whole = 0;
	later_on = 0;
}

void win_message(short *msg)
{
	GRECT r;
	if (msg[3] != win_h || win_h <= 0)
		return;
	if (later_on && (msg[0] == WM_MOVED || msg[0] == WM_SIZED || msg[0] == WM_FULLED ||
			 msg[0] == WM_ICONIFY || msg[0] == WM_ALLICONIFY))
		win_later_finish();	/* where the window is now, before it goes */
	switch (msg[0]) {
	case WM_REDRAW:
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		win_uncover(&r);
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
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (win_iconified)
			break;
		remember_place();
		win_iconified = 1;
		wind_set(win_h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_UNICONIFY:
		if (!win_iconified)
			break;
		wind_set(win_h, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		win_iconified = 0;
		win_layout();
		win_redraw(0);
		break;
	case WM_FULLED: {
		short x, y, w, h, fx, fy, fw, fh;
		if (win_iconified)
			break;
		wind_get(win_h, WF_CURRXYWH, &x, &y, &w, &h);
		wind_get(win_h, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		{
			/* full with the work area on a byte, as snap_x puts it
			   everywhere */
			short sx = snap_x(fx);
			fw -= sx - fx;
			fx = sx;
		}
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
		if (win_iconified) {	/* the icon goes where it is put, as it is */
			wind_set(win_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
		if (sx + w > desk_x + desk_w)
			w = desk_x + desk_w - sx;
		{
			GRECT was = work;
			int whole = obuf_whole;
			wind_set(win_h, WF_CURRXYWH, sx, msg[5], w, h);
			win_layout();
			if (msg[0] == WM_SIZED)
				win_redraw(0);
			else if (whole)
				buffer_moved(&was);
		}
		remember_place();
		break;
	}
	case WM_ARROWED: {
		/* the window's arrows and track: the card's; the mouse wheel,
		   which some AESes send so, over the list: the list's. 0 page
		   up, 1 page down, 2 line up, 3 line down */
		short mx, my, mb, ks;
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!rect_hit(&r_list, mx, my)) {
			card_arrowed(msg[4]);
			break;
		}
		switch (msg[4]) {
		case 0: list_scroll(-(r_list.h / (ch + 2)) + 1); break;
		case 1: list_scroll(r_list.h / (ch + 2) - 1); break;
		case 2: list_scroll(-1); break;
		case 3: list_scroll(1); break;
		}
		break;
	}
	case WM_VSLID:
		card_slid(msg[4]);
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

static void search_changed(void);

static void search_redraw(void)
{
	GRECT r = r_search;
	r.w++;
	r.h++;
	win_redraw(&r);
}

/* the focus to the search field (1) or away from it (0) */
static void search_focus_set(short on)
{
	if (search_focus == on)
		return;
	search_focus = on;
	search_redraw();
}

void win_search_set(void)
{
	search_changed();
}

/* the search changed: the list again, the card if the choice went */
/* the chosen one when the search began: the card again only if another
   is chosen when it is done */
static char search_was[sizeof(sel_uid)];
static short search_was_book;

/* the search's result: the list in bands from the event loop (the next
   key meanwhile), the card only when another is chosen */
static void search_done(void)
{
	list_fix_selection();
	status_updated();		/* "2 of 21 contacts" */
	win_redraw_later(&r_list);
	if (strcmp(search_was, sel_uid) || search_was_book != sel_book)
		win_redraw_later(&r_card);
}

static void search_changed(void)
{
	if (!list_search_pending()) {
		str_copy(search_was, sel_uid, sizeof(search_was));
		search_was_book = sel_book;
	}
	list_search_changed();		/* its matching a slice a turn (win_search_step) */
	search_redraw();
	if (!list_search_pending())
		search_done();
}

/* from the event loop: a slice of the search's matching */
void win_search_step(int (*more)(void))
{
	if (list_search_step(more))
		search_done();
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
		case B_REFRESH:
			if (picking)
				pick_end(0);
			else
				cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT));
			break;
		case B_MENU:
			if (picking)
				pick_end(1);
			else
				cmd_menu(but[B_MENU].x, but[B_MENU].y + but[B_MENU].h);
			break;
		default:
			/* a click in the search field gives it the focus; the
			   clear button at its right takes the text out */
			if (rect_hit(&r_search, mx, my)) {
				search_focus_set(1);
				if (search_text[0] && mx >= r_search.x + r_search.w - 2 * cw) {
					search_text[0] = 0;
					search_changed();
				}
			} else {
				search_focus_set(0);
			}
		}
		return 1;
	}
	search_focus_set(0);
	if (r_side.w && rect_hit(&r_side, mx, my))
		return side_click(mx, my, clicks, button);
	if (rect_hit(&r_list, mx, my))
		return list_click(mx, my, clicks, button, kstate);
	if (rect_hit(&r_card, mx, my))
		return card_click(mx, my, clicks, button);
	return 0;
}

/* ^X ^V on the search field (^C copies the contact chosen) */
static int search_clip(int what)
{
	char t[sizeof(search_text)];
	int n = (int)strlen(search_text);
	char *p;
	if (what == CLIP_CUT) {
		if (!n || !clip_put(search_text, n))
			return 1;
		search_text[0] = 0;
	} else {
		clip_get(t, sizeof(t));
		for (p = clip_line(t); *p && n < (int)sizeof(search_text) - 1; p++)
			if ((unsigned char)*p >= ' ' && (*p != ' ' || n))
				search_text[n++] = *p;
		search_text[n] = 0;
	}
	search_focus = 1;
	search_changed();
	return 1;
}

/* typing goes into the search field, as on the Mac. Esc empties it, as
   a one-line field of a window (the guidelines, 3.5; Erez, 27 Oct 2026),
   and does nothing else: it never closes the window */
static int search_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char a = key_char(kstate, key);
	int n = (int)strlen(search_text);
	if (key_ctrl(kstate, key) && (clip_key(key) == CLIP_CUT || clip_key(key) == CLIP_PASTE))
		return search_clip(clip_key(key));
	if (dlg_esc(key) && !dlg_undo(key)) {	/* Esc, ^[ */
		if (!n)
			return 1;
		search_text[0] = 0;
	} else if (key_ctrl(kstate, key) || (kstate & K_ALT)) {
		return 0;
	} else if (scan == 0x0e) {		/* Backspace */
		if (!n)
			return 1;
		search_text[n - 1] = 0;
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
	search_focus = 1;
	search_changed();
	return 1;
}

/* ^F: a new search */
void win_find(void)
{
	search_text[0] = 0;
	search_focus = 1;
	search_changed();
	status("Type to search: names, companies, emails, phones, notes");
}

int win_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	/* key_ctrl: a shortcut typed while EContact was busy comes with the
	   Control key already let go (the guidelines, 3.2) */
	if (key_ctrl(kstate, key)) {
		switch (scan) {
		case 0x31: cmd_new_contact(); return 1;			/* ^N */
		case 0x13: cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT)); return 1;	/* ^R */
		case 0x21: win_find(); return 1;			/* ^F: a new search */
		case 0x33: dlg_settings(); return 1;			/* ^, */
		case 0x12:						/* ^E: edit */
			if (sel_contact() && list_chosen() < 2)
				editor_open(sel_contact());
			return 1;
		case 0x1e:						/* ^A: everyone shown */
			if (!picking)
				list_select_all();
			return 1;
		case 0x22:						/* ^G: a new group */
			cmd_new_group(-1);
			return 1;
		case 0x16:						/* ^U: closes */
		case 0x10:						/* ^Q */
			if (!is_acc)
				quit_now = 1;
			win_close();
			return 1;
		case 0x11:	/* ^W: the next window of ours; there is one (3.2) */
			return 1;
		case 0x1f:						/* Shift+^S; ^S is Save */
			if (!(kstate & (K_LSHIFT | K_RSHIFT)))
				return 0;
			toggle_sidebar();
			return 1;
		case 0x2e:						/* ^C: the contacts chosen */
			if (sel_contact())
				cmd_copy_chosen();
			else if (search_text[0])
				cmd_copy(search_text);
			return 1;
		}
	}
	if (picking && !key_ctrl(kstate, key) && !(kstate & K_ALT)) {
		switch (scan) {
		case 0x1c: case 0x72: pick_end(1); return 1;	/* Return: Choose */
		}
		/* Undo cancels, and Esc once the search field is empty: Esc
		   empties it first, as a dialog's field (3.5) */
		if (dlg_cancel_key(key, search_text[0] != 0)) {
			pick_end(0);
			return 1;
		}
	}
	/* in the search field Delete takes what is after the cursor: there
	   is nothing (the cursor is at the end), and never the contact */
	if (search_focus && scan == 0x53 && !key_ctrl(kstate, key) && !(kstate & K_ALT))
		return 1;
	if (list_key(kstate, key)) {
		/* up, down and Clr/Home go on in the list, as on the Mac */
		if (scan == 0x48 || scan == 0x50 || scan == 0x47)
			search_focus_set(0);
		return 1;
	}
	return search_key(kstate, key);
}
