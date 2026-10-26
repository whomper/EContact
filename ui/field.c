/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * field.c - dialogs drawn by EContact itself.
 *
 * GEM's own text fields show text left to right only, so a Hebrew title
 * would come out backwards. Here every text field keeps its text in
 * logical order and shows it through the bidi algorithm (bidi.c), with
 * the cursor where a reader expects it; F10 switches to the Israeli
 * keyboard as everywhere else in EContact, ECal and EMail. The rest is
 * ordinary: buttons, tick boxes, pop-up menus, a row of colours, a
 * notes box of several lines.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ui.h"
#include "bidi.h"
#include "util.h"
#include "clip.h"
#include "plat.h"
#include "conn.h"
#include "dialog.h"

static GRECT box;		/* the dialog on screen */
static short rh;		/* pixels per row */
static short pad;
static short depth;		/* dialogs open (one can open another: Custom repeat) */
void (*dlg_after)(void);	/* called when the last dialog closes (window frame) */

#define FX(f) (box.x + pad + (f)->x * cw)
#define FY(f) (box.y + pad + (f)->y * rh)

/* the dialog being run, for what depends on its section */
static DLG *cur_dlg;

/* ---- a dialog in a window of the AES's (the guidelines, 3.5) ----
 * Where the AES runs several programs at once (MiNT, MagiC) a dialog
 * opens in a window (common/ui/dialog.c dlg_open_area): the AES draws the
 * frame and the title, so the window shows the box less its own frame
 * and title bar (cut_x, cut_top), and box follows the window when it is
 * moved. Everything is drawn through dlg_cut: the piece the AES asked
 * for, or nothing while another program's window is over the dialog. On
 * TOS and EmuTOS it is drawn over the screen as before. */
static short dlg_win;		/* the dialog in front is in a window */
static short dlg_wh;		/* that window */
static short cut_x, cut_top;	/* the box's frame and title, left out there */
static void (*cur_paint)(void);	/* the whole dialog drawn (clip_dlg clips it) */

/* the dialogs under the one in front, as they were when it opened: the
   AES may ask for a piece of any of them */
#define LEVELS 4
static struct level {
	GRECT box;
	short rh, pad, win, wh, cx, ctop;
	DLG *d;
	void (*paint)(void);
} lv[LEVELS];
static short nlv;		/* open */
static char lv_name[LEVELS][80];

static void level_get(struct level *l)
{
	l->box = box;
	l->rh = rh;
	l->pad = pad;
	l->win = dlg_win;
	l->wh = dlg_wh;
	l->cx = cut_x;
	l->ctop = cut_top;
	l->d = cur_dlg;
	l->paint = cur_paint;
}

static void level_set(const struct level *l)
{
	box = l->box;
	rh = l->rh;
	pad = l->pad;
	dlg_win = l->win;
	dlg_wh = l->wh;
	cut_x = l->cx;
	cut_top = l->ctop;
	cur_dlg = l->d;
	cur_paint = l->paint;
}

/* the part of the box that shows */
static void box_shown(GRECT *r)
{
	*r = box;
	if (dlg_win) {
		r->x += cut_x;
		r->w -= 2 * cut_x;
		r->y += cut_top;
		r->h -= cut_top + cut_x;
	}
}

/* the VDI's clip set to a (the box when 0), within what shows and may be
   drawn now; 0: nothing of it */
static int clip_dlg(const GRECT *a)
{
	GRECT c, s;
	c = a ? *a : box;
	box_shown(&s);
	if (!rect_and(&c, &s) || !dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return 0;
	clip_on(&c);
	return 1;
}

/* in a window: box where the window has it now */
static void box_follow(void)
{
	short x, y, w, h;
	if (!dlg_win || !dlg_in_window())
		return;
	dlg_area(&x, &y, &w, &h);
	box.x = x - cut_x;
	box.y = y - cut_top;
}

/* a piece of dialog k drawn when the AES asks (dlg_open_area's draw);
   the clip is dlg_cut's */
static short opening;		/* box_open: its caller draws it */

static void piece_at(short k)
{
	struct level now;
	if (k >= nlv || (opening && k == nlv - 1))
		return;
	if (k == nlv - 1) {
		box_follow();
		cur_paint();
		return;
	}
	level_get(&now);
	level_set(&lv[k]);
	box_follow();
	cur_paint();
	level_get(&lv[k]);
	level_set(&now);
}

static void piece0(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	piece_at(0);
}

static void piece1(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	piece_at(1);
}

static void piece2(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	piece_at(2);
}

static void piece3(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	piece_at(3);
}

static void (*const pieces[LEVELS])(short, short, short, short) = { piece0, piece1, piece2, piece3 };

/* messages that came for EContact while a dialog window was open: on TOS
   the AES's pipe keeps them (the dialog doesn't ask for messages), here
   they are kept, a window's redraws as one, and sent back when the last
   dialog closes */
#define NHELD 16
static short held[NHELD][8], nheld;

static void hold_msg(const short *m)
{
	short k;
	if (m[0] == MN_SELECTED) {
		menu_drop(m);
		return;
	}
	if (m[0] == WM_REDRAW)
		for (k = 0; k < nheld; k++)
			if (held[k][0] == WM_REDRAW && held[k][3] == m[3]) {
				short x2 = held[k][4] + held[k][6], y2 = held[k][5] + held[k][7];
				if (m[4] + m[6] > x2)
					x2 = m[4] + m[6];
				if (m[5] + m[7] > y2)
					y2 = m[5] + m[7];
				if (m[4] < held[k][4])
					held[k][4] = m[4];
				if (m[5] < held[k][5])
					held[k][5] = m[5];
				held[k][6] = x2 - held[k][4];
				held[k][7] = y2 - held[k][5];
				return;
			}
	if (nheld < NHELD)
		memcpy(held[nheld++], m, sizeof(held[0]));
}

static short pg_open;

/* the window drawn while a dialog window is over it (dlg_redraw_hook):
   in its own font; kept for later while Sync Contact Photos runs, which
   may be letting the contacts go */
static void win_behind(const short *msg)
{
	short f;
	if (pg_open) {
		hold_msg(msg);
		return;
	}
	f = font_sys_out();
	win_message((short *)msg);
	font_sys_back(f);
}

void dlg_window_init(void)
{
	dlg_title = "EContact";
	dlg_redraw_hook = win_behind;
}

/* evnt_multi_ for a dialog of ours: in a window dlg_evnt, the messages
   that aren't the dialog's kept (hold_msg), box following a move */
static short evnt_dlg(short flags, short clicks, short mask, short state, unsigned long ms,
		      short *m, AEVENT *e)
{
#ifdef AES_PROFILE
	extern void prof_turn_end(void);
	prof_turn_end();		/* the speed rig: a dialog's stretch ends here too */
#endif
	if (!dlg_win)
		return evnt_multi_(flags, clicks, mask, state, ms, m, e);
	for (;;) {
		dlg_evnt(flags | MU_MESAG, clicks, mask, state, ms, m, e);
		box_follow();
		if (e->which & MU_MESAG) {
			hold_msg(m);
			e->which &= ~MU_MESAG;
		}
		if (e->which)
			return e->which;
	}
}

/* the dialog's window title: its name, and "עב" while the Hebrew keyboard
   is on (drawn in our own title bar over the screen) */
static void win_name(const char *title)
{
	char *n = lv_name[nlv - 1];
	snprintf(n, sizeof(lv_name[0]), "%s%s", title ? title : "", hebrew_kbd ? "  \xD3\xC3" : "");
	if (dlg_win && dlg_wh > 0)
		wind_set_str(dlg_wh, WF_NAME, n);
}

/* before a dialog sets box and the rest: the one under it kept */
static void level_save(void)
{
	if (nlv)
		level_get(&lv[nlv - 1]);
}

/* a new dialog at box (set), drawn by paint: over the screen with the
   screen and mouse kept (TOS, EmuTOS: as it always was), or in a window,
   its frame (cx) and title bar (ctop) left to the AES's; the caller then
   draws it */
static void box_open(const char *title, void (*paint)(void), short cx, short ctop)
{
	short x = 0, y = 0, w, h, k, d;
	k = nlv < LEVELS ? nlv++ : LEVELS - 1;	/* deeper than EContact goes: the last reused */
	cur_paint = paint;
	cut_x = cx;
	cut_top = ctop;
	dlg_wh = 0;
	dlg_win = (short)dlg_windowed();
	win_name(title);
	if (!dlg_win) {
		wind_update(BEG_UPDATE);
		wind_update(3);		/* BEG_MCTRL: the dialog owns the mouse */
		form_dial(FMD_START, box.x, box.y, box.w + 2, box.h + 2);
		return;
	}
	w = box.w - 2 * cx;
	h = box.h - ctop - cx;
	dlg_name(lv_name[k]);
	opening = 1;			/* drawn by the caller */
	dlg_open_area(pieces[k], &x, &y, &w, &h);
	opening = 0;
	if (!dlg_in_window()) {
		/* no window to be had: over the screen, as on TOS */
		dlg_win = 0;
		form_dial(FMD_START, box.x, box.y, box.w + 2, box.h + 2);
		return;
	}
	wind_get(0, WF_TOP, &dlg_wh, &d, &d, &d);
	box_follow();
}

static void box_close(void)
{
	if (!dlg_windowed()) {
		form_dial(FMD_FINISH, box.x, box.y, box.w + 2, box.h + 2);
		wind_update(2);
		wind_update(END_UPDATE);
	} else {
		if (!dlg_win)	/* no window was had: the whole box given back */
			form_dial(FMD_FINISH, box.x, box.y, box.w + 2, box.h + 2);
		dlg_close();
	}
	if (--nlv > 0)
		level_set(&lv[nlv - 1]);
	if (!dlg_in_window()) {
		short k;
		for (k = 0; k < nheld; k++)
			appl_write(appl_id(), 16, held[k]);
		nheld = 0;
	}
}

/* The AES's 3D look (the guidelines 5.6), where it draws one and it is
   on (DIALOGS3D=), in 16 colours or more: the dialog on the AES's grey
   (LWHITE), its buttons raised as the AES's exit buttons are (white
   above and left, dark grey below and right, pressed in when clicked),
   its pop-ups raised too, its fields, tick boxes and lists left white.
   Elsewhere the flat look, as before */
#define LWHITE 8
#define LBLACK 9
static int d3;
static short bg;		/* the dialog's ground: white, or LWHITE */

int dlg_look3d(void)
{
	return dlg_3d_ok() && dlg_3d_on && ncolors >= 16;
}

static void look_init(void)
{
	d3 = dlg_look3d();
	bg = d3 ? LWHITE : 0;
}

/* a line drawn dimmed: the light grey is the ground in 3D */
static short dim_line(void)
{
	return d3 ? LBLACK : grey_color();
}

/* a raised edge inside a frame of k lines: two round a button, as the
   AES's exit buttons have, one round a pop-up; in: pressed */
static void bevel(const GRECT *r, short k, int in)
{
	short x1 = r->x + k, y1 = r->y + k, x2 = r->x + r->w - 1 - k, y2 = r->y + r->h - 1 - k;
	line_col(x1, y1, x2, y1, in ? LBLACK : 0);
	line_col(x1, y1, x1, y2, in ? LBLACK : 0);
	line_col(x1, y2, x2, y2, in ? 0 : LBLACK);
	line_col(x2, y1, x2, y2, in ? 0 : LBLACK);
}

static int shown_now(const FIELD *f)
{
	return !cur_dlg || !cur_dlg->npages || !f->page || f->page == cur_dlg->page;
}

static int focusable(const FIELD *f)
{
	if (!shown_now(f))
		return 0;
	return (f->type == FT_TEXT || f->type == FT_PASS || f->type == FT_MULTI) && !(f->flags & FF_DISABLED);
}

static void field_rect(const FIELD *f, GRECT *r)
{
	r->x = FX(f) - 2;
	r->y = FY(f) - 2;
	r->w = f->w * cw + 4;
	r->h = (f->type == FT_MULTI ? f->h : 1) * ch + 4;
	if (f->type == FT_BUTTON || f->type == FT_POPUP) {
		r->y--;
		r->h += 2;
	}
}

/* the shown text of a password field */
static void shown(const FIELD *f, char *out, int size)
{
	int n = (int)strlen(f->buf), i;
	if (f->type != FT_PASS || (f->flags & FF_SHOWN)) {
		str_copy(out, f->buf, size);
		return;
	}
	if (n > size - 1)
		n = size - 1;
	for (i = 0; i < n; i++)
		out[i] = '*';
	out[n] = 0;
}

/* x offset (pixels from the field's left) of the cursor at logical
   index c of the visible text s[0..n) */
static short cursor_x(const char *s, short n, short c, short w, short rtl)
{
	char vis[BIDI_MAX];
	short pos[BIDI_MAX];
	unsigned char odd[BIDI_MAX];
	short xoff = rtl ? (w - n) * cw : 0;
	if (n <= 0)
		return rtl ? w * cw - 1 : 0;
	bidi_visual_map(s, n, rtl, vis, pos, odd);
	if (c < n)
		return xoff + (odd[c] ? (pos[c] + 1) * cw - 1 : pos[c] * cw);
	return xoff + (odd[n - 1] ? pos[n - 1] * cw : (pos[n - 1] + 1) * cw);
}

static short index_at(const char *s, short n, short px, short w, short rtl)
{
	char vis[BIDI_MAX];
	short pos[BIDI_MAX], i, vc;
	unsigned char odd[BIDI_MAX];
	short xoff = rtl ? (w - n) * cw : 0;
	if (n <= 0)
		return 0;
	bidi_visual_map(s, n, rtl, vis, pos, odd);
	vc = (px - xoff) / cw;
	if (px < xoff)
		return rtl ? n : 0;
	if (vc >= n)
		return rtl ? 0 : n;
	for (i = 0; i < n; i++)
		if (pos[i] == vc)
			return odd[i] ? i + (((px - xoff) % cw) < cw / 2 ? 1 : 0)
				      : i + (((px - xoff) % cw) >= cw / 2 ? 1 : 0);
	return n;
}

static short field_rtl(const FIELD *f, const char *s, short n)
{
	if (f->flags & FF_LTR)
		return 0;
	if (!n)
		return hebrew_kbd;
	return line_rtl(s, n);
}

/* the lines of a notes box */
static long multi_lines(const FIELD *f, long *br, long max)
{
	return wrap_text(f->buf, (long)strlen(f->buf), f->w, br, max);
}

static void draw_text_field(FIELD *f, int focus)
{
	GRECT r;
	char s[256];
	field_rect(f, &r);
	fill(&r, 0);
	frame(&r, (f->flags & FF_DISABLED) ? dim_line() : 1);
	if (focus) {
		GRECT r2 = r;
		r2.x--;
		r2.y--;
		r2.w += 2;
		r2.h += 2;
		frame(&r2, 1);
	}
	if (f->type == FT_MULTI) {
		long br[64], nl = multi_lines(f, br, 64), len = (long)strlen(f->buf), i, curl = 0;
		for (i = 0; i < nl; i++)
			if (br[i] <= f->cur)
				curl = i;
		if (curl < f->first)
			f->first = (short)curl;
		if (curl >= f->first + f->h)
			f->first = (short)(curl - f->h + 1);
		for (i = f->first; i < nl && i < f->first + f->h; i++) {
			long a = br[i], b = i + 1 < nl ? br[i + 1] : len, k;
			short rtl;
			while (b > a && (f->buf[b - 1] == '\n'))
				b--;
			k = b - a;
			if (k > f->w)
				k = f->w;
			rtl = field_rtl(f, f->buf + a, (short)k);
			text_at(FX(f), FY(f) + (short)(i - f->first) * ch, f->buf + a, k, f->w, rtl ? TX_RIGHT : TX_LTR);
			if (focus && i == curl) {
				short cx = cursor_x(f->buf + a, (short)k, (short)(f->cur - a > k ? k : f->cur - a), f->w, rtl);
				short y = FY(f) + (short)(i - f->first) * ch;
				line_col(FX(f) + cx, y, FX(f) + cx, y + ch - 1, 1);
			}
		}
		if (focus && !nl)
			line_col(FX(f) + (hebrew_kbd ? f->w * cw - 1 : 0), FY(f), FX(f) + (hebrew_kbd ? f->w * cw - 1 : 0), FY(f) + ch - 1, 1);
		return;
	}
	shown(f, s, sizeof(s));
	{
		short n = (short)strlen(s), vis, rtl;
		if (f->cur > n)
			f->cur = n;
		/* scroll so the cursor shows */
		if (f->cur < f->first)
			f->first = f->cur;
		if (f->cur > f->first + f->w - 1)
			f->first = f->cur - f->w + 1;
		if (f->first > n)
			f->first = n;
		if (f->first < 0)
			f->first = 0;
		vis = n - f->first > f->w ? f->w : n - f->first;
		rtl = field_rtl(f, s + f->first, vis);
		if (vis > 0)
			text_at(FX(f), FY(f), s + f->first, vis, f->w,
				(rtl ? TX_RIGHT : TX_LTR) | ((f->flags & FF_DISABLED) ? TX_DIM : 0));
		if (focus) {
			short cx = cursor_x(s + f->first, vis, f->cur - f->first, f->w, rtl);
			line_col(FX(f) + cx, FY(f), FX(f) + cx, FY(f) + ch - 1, 1);
		}
	}
}

/* ---- the eye beside a password field, as in EMail: open while the
   password is hidden, crossed out while it is shown ---- */

static const char *const eye_art[16] = {
	"................",
	"................",
	"................",
	".....######.....",
	"...##......##...",
	"..#....##....#..",
	".#....####....#.",
	"#.....####.....#",
	".#....####....#.",
	"..#....##....#..",
	"...##......##...",
	".....######.....",
	"................",
	"................",
	"................",
	"................",
};

static void eye_rect(const FIELD *f, GRECT *r)
{
	r->x = FX(f) + (f->w + 1) * cw;
	r->y = FY(f) + (ch - (ch >= 16 ? 16 : 8)) / 2;
	r->w = 16;
	r->h = ch >= 16 ? 16 : 8;
}

static void draw_eye(const FIELD *f)
{
	static unsigned short bits[16];
	short colors[2] = { 1, 0 }, rows = ch >= 16 ? 16 : 8, r, c, pxy[8];
	MFDB src, dst;
	GRECT e;
	for (r = 0; r < rows; r++) {
		const char *a = eye_art[rows == 16 ? r : r * 2 + 1];
		unsigned short v = 0;
		for (c = 0; c < 16; c++)
			v = (unsigned short)((v << 1) | (a[c] == '#'));
		if (f->flags & FF_SHOWN)	/* a stroke from bottom left to top right */
			v |= (unsigned short)((0x8000 >> ((rows - 1 - r) * 16 / rows)) | (0x4000 >> ((rows - 1 - r) * 16 / rows)));
		bits[r] = v;
	}
	eye_rect(f, &e);
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = st_ram(sizeof(bits));
	memcpy(src.fd_addr, bits, sizeof(bits));
	src.fd_w = 16;
	src.fd_h = rows;
	src.fd_wdwidth = 1;
	src.fd_nplanes = 1;
	colors[1] = bg;		/* on the dialog's ground */
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = 15;
	pxy[3] = rows - 1;
	pxy[4] = e.x;
	pxy[5] = e.y;
	pxy[6] = e.x + 15;
	pxy[7] = e.y + rows - 1;
	vrt_cpyfm(vdi_h, 1, pxy, &src, &dst, colors);
}

static int eye_hit(const FIELD *f, short mx, short my)
{
	GRECT e;
	if (f->type != FT_PASS)
		return 0;
	eye_rect(f, &e);
	e.x -= 2;
	e.y -= 2;
	e.w += 4;
	e.h += 4;
	return rect_hit(&e, mx, my);
}

static void draw_field(DLG *d, short i)
{
	FIELD *f = &d->f[i];
	GRECT r;
	short dis = (f->flags & FF_DISABLED) ? (d3 ? TX_GREY : TX_DIM) : 0;	/* (the pale grey is the ground) */
	switch (f->type) {
	case FT_LABEL:
	case FT_HEAD:
		text_at(FX(f), FY(f), f->label, (long)strlen(f->label), f->w ? f->w : (short)strlen(f->label),
			(f->type == FT_HEAD ? TX_BOLD : 0) | (line_rtl(f->label, (long)strlen(f->label)) ? TX_RIGHT : TX_LTR) | dis);
		break;
	case FT_TEXT:
	case FT_PASS:
	case FT_MULTI:
		draw_text_field(f, d->focus == i);
		if (f->type == FT_PASS)
			draw_eye(f);
		break;
	case FT_BUTTON:
		field_rect(f, &r);
		fill(&r, bg);
		frame(&r, dis ? dim_line() : 1);
		if (d3) {
			GRECT in = r;
			in.x++;
			in.y++;
			in.w -= 2;
			in.h -= 2;
			frame(&in, dis ? dim_line() : 1);
			bevel(&r, 2, 0);
		}
		if (f->flags & FF_DEFAULT) {
			GRECT r2 = r;
			r2.x--;
			r2.y--;
			r2.w += 2;
			r2.h += 2;
			frame(&r2, 1);
		}
		text_at(FX(f), FY(f), f->label, (long)strlen(f->label), f->w, TX_CENTER | TX_LTR | dis |
			((f->flags & FF_DEFAULT) ? TX_BOLD : 0));
		break;
	case FT_CHECK: {
		GRECT b;
		b.x = FX(f);
		b.y = FY(f) + 1;
		b.w = 2 * cw - 4;
		b.h = ch - 2;
		fill(&b, 0);
		frame(&b, dis ? dim_line() : 1);
		if (f->val) {
			line_col(b.x + 2, b.y + b.h / 2, b.x + b.w / 2 - 1, b.y + b.h - 3, 1);
			line_col(b.x + b.w / 2 - 1, b.y + b.h - 3, b.x + b.w - 3, b.y + 2, 1);
			line_col(b.x + 2, b.y + b.h / 2 + 1, b.x + b.w / 2 - 1, b.y + b.h - 2, 1);
			line_col(b.x + b.w / 2 - 1, b.y + b.h - 2, b.x + b.w - 3, b.y + 3, 1);
		}
		text_at(FX(f) + 2 * cw, FY(f), f->label, (long)strlen(f->label), f->w - 2, TX_LTR | dis);
		break;
	}
	case FT_POPUP: {
		const char *l = f->val >= 0 && f->val < f->nitems ? f->items[f->val] : "";
		field_rect(f, &r);
		fill(&r, bg);
		frame(&r, dis ? dim_line() : 1);
		if (d3) {
			bevel(&r, 1, 0);	/* raised, as the family's are */
		} else {
			/* a shadow says "menu" */
			line_col(r.x + 1, r.y + r.h, r.x + r.w, r.y + r.h, 1);
			line_col(r.x + r.w, r.y + 1, r.x + r.w, r.y + r.h, 1);
		}
		while (*l == ' ')
			l++;
		text_at(FX(f) + cw / 2, FY(f), l, (long)strlen(l), f->w - 2,
			(line_rtl(l, (long)strlen(l)) ? 0 : TX_LTR) | dis);
		text_at(FX(f) + (f->w - 1) * cw, FY(f), "\x02", 1, 1, TX_LTR | dis);
		break;
	}
	case FT_COLOR: {
		short k;
		for (k = 0; k < NCOLORS; k++) {
			GRECT b;
			PEN p;
			pen_color(k, &p);
			b.x = FX(f) + k * 3 * cw;
			b.y = FY(f) - 1;
			b.w = 2 * cw;
			b.h = ch + 2;
			fill(&b, bg);
			b.x += 2;
			b.y += 2;
			b.w -= 4;
			b.h -= 4;
			if (ncolors >= 16)
				fill(&b, p.color);
			else
				fill_pat(&b, p.color, p.interior, p.style == 2 ? 4 : p.style);
			b.x -= 2;
			b.y -= 2;
			b.w += 4;
			b.h += 4;
			frame(&b, f->val == k ? 1 : bg);
			if (f->val == k) {
				b.x--;
				b.y--;
				b.w += 2;
				b.h += 2;
				frame(&b, 1);
			}
		}
		break;
	}
	}
}

/* ---- sections ---- */

static void draw_all(DLG *d);

static void section_rect(DLG *d, short k, GRECT *r)
{
	r->x = box.x + 5;
	r->y = box.y + pad + (1 + 2 * k) * rh - 3;
	r->w = d->side * cw + pad - 6;
	r->h = ch + 6;
}

static short section_at(DLG *d, short mx, short my)
{
	short k;
	for (k = 0; k < d->npages; k++) {
		GRECT r;
		section_rect(d, k, &r);
		r.y -= rh / 2;		/* the space between the names counts too */
		r.h += rh;
		if (rect_hit(&r, mx, my))
			return k + 1;
	}
	return 0;
}

/* the list of sections, the line beside it, and the chosen one's name
   over a rule */
static void draw_sections(DLG *d)
{
	short k, lx = box.x + pad + d->side * cw + cw / 2;
	short hx = box.x + pad + (d->side + 2) * cw, hy = box.y + pad + rh;
	for (k = 0; k < d->npages; k++) {
		GRECT r;
		const char *s = d->pages[k];
		section_rect(d, k, &r);
		fill(&r, k + 1 == d->page ? 1 : bg);
		text_at(r.x + cw, r.y + 3, s, (long)strlen(s), d->side, TX_LTR | (k + 1 == d->page ? TX_INVERSE : 0));
	}
	line_col(lx, box.y + ch + 6, lx, box.y + box.h - 4, 1);
	if (d->page >= 1 && d->page <= d->npages) {
		const char *s = d->pages[d->page - 1];
		text_at(hx, hy, s, (long)strlen(s), d->w - d->side - 2, TX_LTR | TX_BOLD);
		line_col(hx, hy + ch + 3, box.x + box.w - pad, hy + ch + 3, 1);
	}
}

static void set_page(DLG *d, short p)
{
	short i;
	d->page = p;
	d->focus = -1;
	for (i = 0; i < d->n; i++)
		if (focusable(&d->f[i])) {
			d->focus = i;
			break;
		}
	draw_all(d);
}

static void draw_all(DLG *d)
{
	GRECT t;
	short i;
	char title[80];
	look_init();
	if (!clip_dlg(0))
		return;
	graf_mouse(M_OFF, 0);
	fill(&box, bg);
	if (!dlg_win) {
		/* in a window the AES's frame and title bar are its own */
		GRECT in = box;
		frame(&box, 1);
		in.x += 2;
		in.y += 2;
		in.w -= 4;
		in.h -= 4;
		frame(&in, 1);
		t.x = box.x + 3;
		t.y = box.y + 3;
		t.w = box.w - 6;
		t.h = ch + 2;
		fill(&t, 1);
		snprintf(title, sizeof(title), "%s", d->title ? d->title : "");
		text_at(t.x, t.y + 1, title, (long)strlen(title), t.w / cw, TX_INVERSE | TX_BOLD | TX_CENTER |
			(line_rtl(title, (long)strlen(title)) ? 0 : TX_LTR));
		if (hebrew_kbd)
			text_at(t.x + t.w - 4 * cw, t.y + 1, "\xD3\xC3", 2, 2, TX_INVERSE);	/* "עב": Hebrew keyboard */
	}
	if (d->npages)
		draw_sections(d);
	for (i = 0; i < d->n; i++)
		if (shown_now(&d->f[i]))
			draw_field(d, i);
	graf_mouse(M_ON, 0);
}

void dlg_redraw_field(DLG *d, short i)
{
	GRECT r;
	FIELD *f = &d->f[i];
	if (!clip_dlg(0))
		return;
	graf_mouse(M_OFF, 0);
	field_rect(f, &r);
	if (f->type == FT_LABEL || f->type == FT_HEAD || f->type == FT_CHECK || f->type == FT_COLOR) {
		r.x = FX(f) - 2;
		r.w = (f->type == FT_COLOR ? NCOLORS * 3 : f->w ? f->w : (short)strlen(f->label)) * cw + 6;
		r.y = FY(f) - 3;
		r.h = ch + 6;
		rect_and(&r, &box);
		fill(&r, bg);
	} else {
		r.x -= 2;
		r.y -= 2;
		r.w += 5;
		r.h += 5;
		fill(&r, bg);
	}
	draw_field(d, i);
	graf_mouse(M_ON, 0);
}

static short next_focus(DLG *d, short from, short dir)
{
	short i = from, k;
	for (k = 0; k < d->n; k++) {
		i += dir;
		if (i < 0)
			i = d->n - 1;
		if (i >= d->n)
			i = 0;
		if (focusable(&d->f[i]))
			return i;
	}
	return from;
}

static void set_focus(DLG *d, short i)
{
	short old = d->focus;
	d->focus = i;
	if (old >= 0 && old != i)
		dlg_redraw_field(d, old);
	if (i >= 0)
		dlg_redraw_field(d, i);
}

static void press_effect(DLG *d, short i)
{
	GRECT r;
	field_rect(&d->f[i], &r);
	if (!clip_dlg(0))
		return;
	graf_mouse(M_OFF, 0);
	if (d3) {
		bevel(&r, 2, 1);
		evnt_timer_(90);
		bevel(&r, 2, 0);
		graf_mouse(M_ON, 0);
		return;
	}
	set_wrmode(3);
	fill(&r, 1);
	evnt_timer_(90);
	fill(&r, 1);
	set_wrmode(1);
	graf_mouse(M_ON, 0);
}

/* ^C ^X ^V (common/ui/clip.c). With nothing selected, the field, and in a
   field of several lines the line the cursor is in; nothing is copied out
   of a password. A paste goes in as far as the field takes it, its first
   line only unless the field has more */
static int clip_field(DLG *d, FIELD *f, int what)
{
	char t[1200], *p;
	short n = (short)strlen(f->buf), s = 0, e = n;
	if (f->type == FT_MULTI) {
		for (s = f->cur; s > 0 && f->buf[s - 1] != '\n'; s--)
			;
		for (e = f->cur; e < n && f->buf[e] != '\n'; e++)
			;
	}
	if (what == CLIP_PASTE) {
		clip_get(t, sizeof(t));
		if (f->type != FT_MULTI)
			clip_line(t);
		for (p = t; *p && n < f->max - 1; p++) {
			unsigned char a = *p == '\t' ? ' ' : (unsigned char)*p;
			if ((a < 32 && a != '\n') || ((f->flags & FF_DIGITS) && (a < '0' || a > '9')) ||
			    (f->type == FT_PASS && a >= 0x80))
				continue;
			memmove(f->buf + f->cur + 1, f->buf + f->cur, n - f->cur + 1);
			f->buf[f->cur++] = (char)a;
			n++;
		}
	} else {
		if (f->type == FT_PASS || !clip_put(f->buf + s, e - s) || what != CLIP_CUT)
			return 1;
		if (f->type == FT_MULTI && e < n)
			e++;			/* the line goes with its end */
		memmove(f->buf + s, f->buf + e, n - e + 1);
		f->cur = s;
		f->first = 0;
	}
	dlg_redraw_field(d, (short)(f - d->f));
	return 1;
}

/* typing into a text field; 1 if the key was taken */
static int edit_key(DLG *d, FIELD *f, short kstate, short key)
{
	short scan = KEY_SCAN(key), n = (short)strlen(f->buf);
	unsigned char a = key_char(kstate, key);
	short rtl = field_rtl(f, f->buf, n);
	if (key_ctrl(kstate, key) && clip_key(key))
		return clip_field(d, f, clip_key(key));
	switch (scan) {
	case 0x0e:		/* Backspace */
		if (f->cur > 0) {
			memmove(f->buf + f->cur - 1, f->buf + f->cur, n - f->cur + 1);
			f->cur--;
		}
		break;
	case 0x53:		/* Delete */
		if (f->cur < n)
			memmove(f->buf + f->cur, f->buf + f->cur + 1, n - f->cur);
		break;
	case 0x4b:		/* left: on screen, so backwards in Hebrew */
	case 0x4d:
		/* with Shift: to that end (the guidelines 5.4); TOS gives Shift+arrow
		   '4' and '6', so a key typed while busy, Shift let go since, is
		   known too */
		if ((kstate & (K_LSHIFT | K_RSHIFT)) || (key & 0xff)) {
			f->cur = (scan == 0x4b) != (rtl != 0) ? 0 : n;
		} else if ((scan == 0x4b) != (rtl != 0)) {
			if (f->cur > 0)
				f->cur--;
		} else if (f->cur < n) {
			f->cur++;
		}
		break;
	case 0x47:		/* Clr/Home: start, with Shift the end */
		f->cur = (kstate & (K_LSHIFT | K_RSHIFT)) ? n : 0;
		break;
	case 0x61:		/* Undo: empty the field */
		f->buf[0] = 0;
		f->cur = 0;
		f->first = 0;
		break;
	case 0x48:
	case 0x50:
		if (f->type == FT_MULTI) {
			long br[64], nl = multi_lines(f, br, 64), i, line = 0;
			for (i = 0; i < nl; i++)
				if (br[i] <= f->cur)
					line = i;
			if (scan == 0x48 && line > 0)
				f->cur = (short)(br[line - 1] + (f->cur - br[line] < br[line] - br[line - 1] ? f->cur - br[line] : br[line] - br[line - 1] - 1));
			else if (scan == 0x50 && line + 1 < nl) {
				long next_end = line + 2 < nl ? br[line + 2] - 1 : n;
				f->cur = (short)(br[line + 1] + (f->cur - br[line]));
				if (f->cur > next_end)
					f->cur = (short)next_end;
			} else
				return 0;
			break;
		}
		return 0;
	case 0x1c:
	case 0x72:
		if (f->type != FT_MULTI || (kstate & K_CTRL))
			return 0;
		a = '\n';
		/* fall through */
	default:
		if (a < 32 && a != '\n')
			return 0;
		if ((f->flags & FF_DIGITS) && (a < '0' || a > '9'))
			return 1;	/* a number field: taken, and ignored */
		if (n >= f->max - 1)
			return 1;	/* full */
		if (f->type == FT_PASS && a >= 0x80)
			return 1;
		memmove(f->buf + f->cur + 1, f->buf + f->cur, n - f->cur + 1);
		f->buf[f->cur++] = (char)a;
	}
	dlg_redraw_field(d, (short)(f - d->f));
	return 1;
}

static int click_field(DLG *d, short i, short mx, short my)
{
	FIELD *f = &d->f[i];
	if (f->type == FT_MULTI) {
		long br[64], nl = multi_lines(f, br, 64), line = f->first + (my - FY(f)) / ch;
		long len = (long)strlen(f->buf), a, b;
		if (line >= nl)
			line = nl - 1;
		if (line < 0) {
			f->cur = (short)len;
		} else {
			a = br[line];
			b = line + 1 < nl ? br[line + 1] : len;
			while (b > a && f->buf[b - 1] == '\n')
				b--;
			if (b - a > f->w)
				b = a + f->w;
			f->cur = (short)(a + index_at(f->buf + a, (short)(b - a), mx - FX(f), f->w,
						      field_rtl(f, f->buf + a, (short)(b - a))));
		}
	} else {
		char s[256];
		short n, vis;
		shown(f, s, sizeof(s));
		n = (short)strlen(s);
		vis = n - f->first > f->w ? f->w : n - f->first;
		f->cur = f->first + index_at(s + f->first, vis, mx - FX(f), f->w, field_rtl(f, s + f->first, vis));
	}
	set_focus(d, i);
	return 1;
}

static short field_at(DLG *d, short mx, short my)
{
	short i;
	for (i = d->n - 1; i >= 0; i--) {
		FIELD *f = &d->f[i];
		GRECT r;
		if (f->type == FT_LABEL || f->type == FT_HEAD || !shown_now(f))
			continue;
		if (eye_hit(f, mx, my))
			return i;
		field_rect(f, &r);
		if (f->type == FT_CHECK) {
			r.w = ((short)strlen(f->label) + 2) * cw + 4;
		} else if (f->type == FT_COLOR) {
			r.w = NCOLORS * 3 * cw;
		}
		if (rect_hit(&r, mx, my))
			return i;
	}
	return -1;
}

static short find_flag(DLG *d, short flag)
{
	short i;
	for (i = 0; i < d->n; i++)
		if (d->f[i].type == FT_BUTTON && (d->f[i].flags & flag) && !(d->f[i].flags & FF_DISABLED) &&
		    shown_now(&d->f[i]))
			return i;
	return -1;
}

void dlg_redraw(DLG *d)
{
	draw_all(d);
}

static void paint_run(void)
{
	draw_all(cur_dlg);
}

short dlg_run(DLG *d)
{
	short result = -2, i, mx, my, mb, ks, m[8];
	AEVENT e;
	font_sys_push();
	level_save();			/* the dialog under this one */
	cur_dlg = d;
	if (d->npages && (d->page < 1 || d->page > d->npages))
		d->page = 1;
	pad = cw + 2;
	rh = ch + 6;
	box.w = d->w * cw + 2 * pad;
	box.h = d->h * rh + 2 * pad;
	if (box.w > desk_w)
		box.w = desk_w;
	if (box.h > desk_h)
		box.h = desk_h;
	box.x = desk_x + (desk_w - box.w) / 2;
	box.y = desk_y + (desk_h - box.h) / 2;
	pad += 0;
	/* the title bar takes the first row */
	for (i = 0; i < d->n; i++) {
		d->f[i].cur = d->f[i].buf ? (short)strlen(d->f[i].buf) : 0;
		d->f[i].first = 0;
		if (d->f[i].type == FT_MULTI)
			d->f[i].cur = 0;
	}
	if (d->focus < 0 || d->focus >= d->n || !focusable(&d->f[d->focus]))
		d->focus = next_focus(d, -1, 1);
	if (d->focus >= 0 && !focusable(&d->f[d->focus]))
		d->focus = -1;
	depth++;
	graf_mouse(ARROW, 0);
	/* the screen and mouse the dialog's (BEG_UPDATE, BEG_MCTRL), or a
	   window of the AES's less the frame and title bar */
	box_open(d->title, paint_run, 3, ch + 5);
	draw_all(d);
	while (result == -2) {
		evnt_dlg(MU_KEYBD | MU_BUTTON, 0x102, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			short scan = KEY_SCAN(e.kreturn);
			FIELD *f = d->focus >= 0 ? &d->f[d->focus] : 0;
			if (scan == 0x44) {			/* F10: the Hebrew keyboard */
				set_hebrew_kbd(!hebrew_kbd);
				win_name(d->title);
				draw_all(d);
			} else if (scan == 0x01 || scan == 0x61) {
				/* Esc, Undo: as Cancel, in a text field too (the
				   guidelines, 3.5); without a Cancel, no button at
				   all: nothing is done */
				result = find_flag(d, FF_CANCEL);
				if (result < 0)
					result = -1;
			} else if (scan == 0x0f || ((scan == 0x48 || scan == 0x50) && (!f || f->type != FT_MULTI))) {
				short dir = (scan == 0x48 || (scan == 0x0f && (e.kstate & (K_LSHIFT | K_RSHIFT)))) ? -1 : 1;
				if (d->focus >= 0)
					set_focus(d, next_focus(d, d->focus, dir));
			} else if (f && edit_key(d, f, e.kstate, e.kreturn)) {
				/* typed */
			} else if (scan == 0x1c || scan == 0x72) {	/* Return */
				short b = find_flag(d, FF_DEFAULT);
				if (b >= 0) {
					press_effect(d, b);
					result = b;
				}
			}
		}
		if ((e.which & MU_BUTTON) && d->npages) {
			short k = section_at(d, e.mx, e.my);
			if (k) {
				if (k != d->page)
					set_page(d, k);
				e.which &= ~MU_BUTTON;
				do {
					graf_mkstate(&mx, &my, &mb, &ks);
				} while (mb & 3);
			}
		}
		if (e.which & MU_BUTTON) {
			short hit = field_at(d, e.mx, e.my);
			if (hit >= 0 && !(d->f[hit].flags & FF_DISABLED)) {
				FIELD *f = &d->f[hit];
				switch (f->type) {
				case FT_TEXT:
				case FT_PASS:
				case FT_MULTI:
					if (eye_hit(f, e.mx, e.my)) {
						/* show or hide the password; the dialog stays */
						f->flags ^= FF_SHOWN;
						dlg_redraw_field(d, hit);
						break;
					}
					click_field(d, hit, e.mx, e.my);
					break;
				case FT_BUTTON:
					press_effect(d, hit);
					result = hit;
					break;
				case FT_CHECK:
					f->val = !f->val;
					dlg_redraw_field(d, hit);
					if (d->changed)
						d->changed(d, hit);
					break;
				case FT_POPUP: {
					GRECT r;
					short k;
					field_rect(f, &r);
					/* the popup draws over the dialog: it saves nothing, so redraw after */
					k = popup(r.x, r.y + r.h, f->items, f->nitems);
					draw_all(d);
					if (k >= 0 && k != f->val) {
						f->val = k;
						dlg_redraw_field(d, hit);
						if (d->changed)
							d->changed(d, hit);
					}
					break;
				}
				case FT_COLOR: {
					short k = (e.mx - FX(f)) / (3 * cw);
					if (k >= 0 && k < NCOLORS) {
						f->val = k;
						dlg_redraw_field(d, hit);
						if (d->changed)
							d->changed(d, hit);
					}
					break;
				}
				}
			}
			do {
				graf_mkstate(&mx, &my, &mb, &ks);
			} while (mb & 3);
		}
	}
	clip_off();
	/* back to the dialog underneath: its place, and the caller repaints it */
	box_close();
	font_sys_pop();
	if (--depth == 0 && dlg_after)
		dlg_after();
	return result;
}

/* ---------------- a list to pick from ---------------- */

/* the list being picked from, for its drawing (pick_paint) */
static const char *pk_title;
static const char *const *pk_items;
static short pk_n, pk_sel, pk_first, pk_rows, pk_w;

static void pick_list(GRECT *list)
{
	list->x = box.x + cw;
	list->y = box.y + 2 * (ch + 2);
	list->w = box.w - 2 * cw;
	list->h = pk_rows * (ch + 2);
}

static void pick_paint(void)
{
	GRECT list;
	short i, w = pk_w;
	if (!clip_dlg(0))
		return;
	pick_list(&list);
	graf_mouse(M_OFF, 0);
	fill(&box, bg);
	if (!dlg_win) {		/* in a window the AES's frame and title */
		frame(&box, 1);
	}
	fill(&list, 0);
	if (!dlg_win)
		text_at(box.x + cw, box.y + 4, pk_title, (long)strlen(pk_title), w, TX_BOLD | TX_LTR);
	frame(&list, 1);
	for (i = 0; i < pk_rows && pk_first + i < pk_n; i++) {
		GRECT r;
		const char *s = pk_items[pk_first + i];
		r.x = list.x + 1;
		r.y = list.y + i * (ch + 2) + 1;
		r.w = list.w - 2;
		r.h = ch + 1;
		fill(&r, pk_first + i == pk_sel ? 1 : 0);
		text_at(r.x + cw, r.y, s, (long)strlen(s), w,
			(pk_first + i == pk_sel ? TX_INVERSE : 0) | (line_rtl(s, (long)strlen(s)) ? 0 : TX_LTR));
	}
	text_at(box.x + cw, list.y + list.h + 4, "\x01\x02 Return: choose  Esc: cancel", 29, w, TX_GREY | TX_LTR);
	graf_mouse(M_ON, 0);
}

short dlg_pick(const char *title, const char *const *items, short n, short sel)
{
	short rows = (desk_h - 8 * ch) / (ch + 2), w = 10, i, first, res = -2, m[8], drawn;
	GRECT list;
	AEVENT e;
	if (rows > n)
		rows = n;
	if (rows < 3)
		rows = 3;
	for (i = 0; i < n; i++)
		if ((short)strlen(items[i]) > w)
			w = (short)strlen(items[i]);
	if ((short)strlen(title) > w)
		w = (short)strlen(title);
	w += 4;
	if (sel < 0)
		sel = 0;
	first = sel - rows / 2;
	level_save();
	box.w = w * cw + 4 * cw;
	box.h = (rows + 3) * (ch + 2) + ch;
	box.x = desk_x + (desk_w - box.w) / 2;
	box.y = desk_y + (desk_h - box.h) / 2;
	pk_title = title;
	pk_items = items;
	pk_n = n;
	pk_rows = rows;
	pk_w = w;
	look_init();
	drawn = 0;
	while (res == -2) {
		short mx, my, mb, ks;
		if (first > n - rows)
			first = n - rows;
		if (first < 0)
			first = 0;
		pk_first = first;
		pk_sel = sel;
		if (!drawn)
			box_open(title, pick_paint, 1, 2 * (ch + 2) - cw);
		drawn = 1;
		pick_paint();
		evnt_dlg(MU_KEYBD | MU_BUTTON, 0x102, 3, 0, 0, m, &e);
		pick_list(&list);
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn);
			unsigned char a = KEY_ASCII(e.kreturn);
			if (sc == 0x01 || sc == 0x61)		/* Esc, Undo: none */
				res = -1;
			else if (sc == 0x1c || sc == 0x72)
				res = sel;
			else if (sc == 0x48 && sel > 0) {
				sel--;
				if (sel < first)
					first = sel;
			} else if (sc == 0x50 && sel < n - 1) {
				sel++;
				if (sel >= first + rows)
					first = sel - rows + 1;
			} else if (a >= 'A') {
				/* jump to the next item starting with that letter */
				for (i = 1; i <= n; i++) {
					short k = (sel + i) % n;
					const char *s = items[k];
					while (*s == ' ')
						s++;
					if ((*s | 32) == (a | 32)) {
						sel = k;
						first = sel - rows / 2;
						break;
					}
				}
			}
		}
		if (e.which & MU_BUTTON) {
			if (rect_hit(&list, e.mx, e.my)) {
				short k = first + (e.my - list.y) / (ch + 2);
				if (k < n) {
					if (k == sel && e.breturn >= 2)
						res = sel;
					sel = k;
					if (e.breturn >= 2)
						res = sel;
				}
			} else if (!rect_hit(&box, e.mx, e.my)) {
				res = -1;
			} else if (e.my < list.y) {
				first -= rows - 1;
			} else if (e.my > list.y + list.h) {
				first += rows - 1;
			}
			do {
				graf_mkstate(&mx, &my, &mb, &ks);
			} while (mb & 3);
		}
	}
	clip_off();
	box_close();
	return res;
}

/* len: what buf holds (the guidelines 5.4: a field holds more than it
   shows); the field shows 30 at most and scrolls */
int dlg_ask(const char *title, const char *label, char *buf, short len)
{
	FIELD f[4];
	DLG d;
	short show = len < 30 ? len : 30, w = show + (short)strlen(label) + 4, r;
	if (w < 36)
		w = 36;
	memset(f, 0, sizeof(f));
	memset(&d, 0, sizeof(d));
	f[0].type = FT_LABEL;
	f[0].x = 0;
	f[0].y = 2;
	f[0].label = label;
	f[1].type = FT_TEXT;
	f[1].x = (short)strlen(label) + 1;
	f[1].y = 2;
	f[1].w = show < w - f[1].x - 1 ? show : w - f[1].x - 1;
	f[1].buf = buf;
	f[1].max = len + 1;
	f[2].type = FT_BUTTON;
	f[2].flags = FF_CANCEL;
	f[2].x = w - 22;
	f[2].y = 4;
	f[2].w = 10;
	f[2].label = "Cancel";
	f[3].type = FT_BUTTON;
	f[3].flags = FF_DEFAULT;
	f[3].x = w - 10;
	f[3].y = 4;
	f[3].w = 10;
	f[3].label = "OK";
	d.title = title;
	d.w = w;
	d.h = 5;
	d.f = f;
	d.n = 4;
	d.focus = 1;
	r = dlg_run(&d);
	return r == 3;
}

/* ---------------- progress ----------------
 * A box for long work (Sync Contact Photos): what is being done, a bar,
 * the time left, and Cancel. The work calls prog_update between steps;
 * a step (a photo can take half a minute) calls pf_idle while it waits
 * for the network and conn_tick as data comes in or a picture is
 * decoded, and the box looks at the mouse then, every 50 ms at most.
 * GEM keeps no clicks for later: a click is seen while the button is
 * down, and a click lasts about a tenth of a second. Cancel or Esc stops
 * the network (conn_stop) and the step, and says "Stopping...". */

enum { PG_LINE1, PG_LINE2, PG_CANCEL, PG_N };
static FIELD pg_f[PG_N];
static DLG pg_d;
static char pg_l1[60], pg_l2[60];
static long pg_done, pg_total = 1;	/* the bar as drawn */
static long pg_shown = -1;
static int pg_stop;
static void (*pg_outer_idle)(void), (*pg_outer_tick)(void);
static unsigned long pg_looked;

static void prog_stopping(void)
{
	if (pg_stop)
		return;
	pg_stop = 1;
	conn_stop = 1;
	str_copy(pg_l2, "Stopping...", sizeof(pg_l2));
	dlg_redraw_field(&pg_d, PG_LINE2);
}

/* Cancel or Esc since last looked, without waiting for them; other
   keys and clicks are the box's, and dropped */
static void prog_poll(short ms)
{
	short m[8], mx, my, mb, ks;
	AEVENT e;
	pg_looked = pf_ms();
	for (;;) {
		evnt_dlg(MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, ms, m, &e);
		ms = 0;
		if (e.which & MU_KEYBD) {
			if (KEY_SCAN(e.kreturn) == 0x01 || KEY_SCAN(e.kreturn) == 0x61)	/* Esc, Undo */
				prog_stopping();
			continue;
		}
		if (e.which & MU_BUTTON) {
			if (field_at(&pg_d, e.mx, e.my) == PG_CANCEL) {
				press_effect(&pg_d, PG_CANCEL);
				prog_stopping();
			}
			do {
				graf_mkstate(&mx, &my, &mb, &ks);
			} while (mb & 3);
			continue;
		}
		break;
	}
}

/* pf_idle while the box is up: the network's waits, the decoder */
static void prog_idle(void)
{
	prog_poll(5);
}

/* conn_tick while the box is up: data coming in, a picture decoded */
static void prog_tick(void)
{
	if (pf_ms() - pg_looked >= 50)
		prog_poll(0);
}

static void prog_bar(long done, long total)
{
	GRECT bar, in, part;
	long w;
	pg_done = done;
	pg_total = total;
	bar.x = box.x + pad;
	bar.y = box.y + pad + 3 * rh;
	bar.w = pg_d.w * cw;
	bar.h = ch;
	if (!clip_dlg(0))
		return;
	in = bar;
	graf_mouse(M_OFF, 0);
	frame(&bar, 1);
	in.x++;
	in.y++;
	in.w -= 2;
	in.h -= 2;
	w = total > 0 ? (long)in.w * (done > total ? total : done) / total : 0;
	part = in;
	part.w = (short)w;
	if (part.w > 0)
		fill(&part, sel_color());
	part.x = in.x + (short)w;
	part.w = in.w - (short)w;
	if (part.w > 0)
		fill(&part, 0);
	graf_mouse(M_ON, 0);
}

static void prog_paint(void)
{
	draw_all(&pg_d);
	prog_bar(pg_done, pg_total);
}

void prog_open(const char *title)
{
	short w = 46;
	memset(pg_f, 0, sizeof(pg_f));
	memset(&pg_d, 0, sizeof(pg_d));
	pg_l1[0] = pg_l2[0] = 0;
	pg_f[PG_LINE1].type = FT_LABEL;
	pg_f[PG_LINE1].x = 0;
	pg_f[PG_LINE1].y = 2;
	pg_f[PG_LINE1].w = w;
	pg_f[PG_LINE1].label = pg_l1;
	pg_f[PG_LINE2].type = FT_LABEL;
	pg_f[PG_LINE2].x = 0;
	pg_f[PG_LINE2].y = 4;
	pg_f[PG_LINE2].w = w - 12;
	pg_f[PG_LINE2].label = pg_l2;
	pg_f[PG_CANCEL].type = FT_BUTTON;
	pg_f[PG_CANCEL].flags = FF_CANCEL;
	pg_f[PG_CANCEL].x = w - 10;
	pg_f[PG_CANCEL].y = 4;
	pg_f[PG_CANCEL].w = 10;
	pg_f[PG_CANCEL].label = "Cancel";
	pg_d.title = title;
	pg_d.w = w;
	pg_d.h = 5;
	pg_d.f = pg_f;
	pg_d.n = PG_N;
	pg_d.focus = -1;
	font_sys_push();
	level_save();
	cur_dlg = &pg_d;
	pad = cw + 2;
	rh = ch + 6;
	box.w = w * cw + 2 * pad;
	box.h = pg_d.h * rh + 2 * pad;
	box.x = desk_x + (desk_w - box.w) / 2;
	box.y = desk_y + (desk_h - box.h) / 2;
	depth++;
	pg_open = 1;
	pg_shown = -1;
	pg_stop = 0;
	pg_outer_idle = pf_idle;
	pg_outer_tick = conn_tick;
	pf_idle = prog_idle;
	conn_tick = prog_tick;
	graf_mouse(ARROW, 0);
	pg_done = 0;
	pg_total = 1;
	box_open(title, prog_paint, 3, ch + 5);
	prog_paint();
}

int prog_update(long done, long total, const char *line1, const char *line2)
{
	if (!pg_open)
		return 1;
	if (pg_stop)
		return 0;
	if (line1 && strcmp(line1, pg_l1)) {
		str_copy(pg_l1, line1, sizeof(pg_l1));
		dlg_redraw_field(&pg_d, PG_LINE1);
	}
	if (line2 && strcmp(line2, pg_l2)) {
		str_copy(pg_l2, line2, sizeof(pg_l2));
		dlg_redraw_field(&pg_d, PG_LINE2);
	}
	if (total > 0 && done * 1000 / total != pg_shown) {
		pg_shown = done * 1000 / total;
		prog_bar(done, total);
	}
	/* Cancel or Esc since the last look; nothing waited for */
	prog_poll(0);
	return !pg_stop;
}

void prog_close(void)
{
	if (!pg_open)
		return;
	pg_open = 0;
	pf_idle = pg_outer_idle;
	conn_tick = pg_outer_tick;
	clip_off();
	box_close();
	font_sys_pop();
	if (--depth == 0 && dlg_after)
		dlg_after();
}
