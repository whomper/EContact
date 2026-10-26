/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * draw.c - drawing for EContact's window: rectangles, lines, the colour
 * (or pattern) of the avatars, and text. Text is kept in logical
 * order; lines with Hebrew go through bidi_visual() and, when the
 * paragraph is right-to-left, can be aligned to the right edge (from
 * EMail's draw.c).
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "ui.h"
#include "../src/photo.h"
#include "bidi.h"
#include "conn.h"

static short next_text_color = 1;

/* VDI attributes as last set: each call is a trap, and on an 8 MHz ST
   setting them again for every piece of text cost more than the text */
static short c_wr = -1, c_fint = -1, c_fsty = -1, c_fcol = -1, c_lcol = -1, c_tcol = -1, c_teff = -1, c_per = -1;

/* the attributes above are set again at the next use: after drawing
   with another VDI handle (win.c's off-screen bitmap) */
void draw_forget(void)
{
	c_wr = c_fint = c_fsty = c_fcol = c_lcol = c_tcol = c_teff = c_per = -1;
}

void set_wrmode(short m)
{
	if (m != c_wr)
		vswr_mode(vdi_h, c_wr = m);
}

static void set_fill(short interior, short style, short color)
{
	if (interior != c_fint)
		vsf_interior(vdi_h, c_fint = interior);
	if (interior >= 2 && style != c_fsty)
		vsf_style(vdi_h, c_fsty = style);
	if (color != c_fcol)
		vsf_color(vdi_h, c_fcol = color);
	if (c_per != 0)
		vsf_perimeter(vdi_h, c_per = 0);
}

static void set_lcolor(short c)
{
	if (c != c_lcol)
		vsl_color(vdi_h, c_lcol = c);
}

static void set_text(short color, short effects)
{
	if (color != c_tcol)
		vst_color(vdi_h, c_tcol = color);
	if (effects != c_teff)
		vst_effects(vdi_h, c_teff = effects);
}

static GRECT clip_now;		/* the clip as set, w 0: none */

void clip_on(GRECT *r)
{
	short pxy[4];
	clip_now = *r;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vs_clip(vdi_h, 1, pxy);
}

void clip_off(void)
{
	short pxy[4] = { 0, 0, 0, 0 };
	clip_now.w = 0;
	pxy[2] = scr_w - 1;
	pxy[3] = scr_h - 1;
	vs_clip(vdi_h, 0, pxy);
}

int rect_hit(const GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int rect_and(GRECT *a, const GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x, y1 = a->y > b->y ? a->y : b->y;
	short x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	short y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
	a->x = x1;
	a->y = y1;
	a->w = x2 - x1;
	a->h = y2 - y1;
	return a->w > 0 && a->h > 0;
}

/* Bitmaps the VDI copies to the screen must be in ST RAM: a blitter
   can't reach TT/Fast RAM, where the program itself is loaded when there
   is some. One small block, taken at start-up (an accessory can't take
   memory later); ST RAM is all there is when Mxalloc is missing. */
static unsigned short st_fallback[512];
static unsigned short *st_buf;

void st_ram_init(void)
{
	void *p = Mxalloc(sizeof(st_fallback), 0);
	st_buf = (long)p > 0 ? p : st_fallback;
}

void *st_ram(long n)
{
	if (!st_buf)
		st_ram_init();
	return n <= (long)sizeof(st_fallback) ? st_buf : 0;
}

/* small marks drawn on events: a loop for a repeating one, a bell for
   one with an alert; 16x16 for tall fonts, 8x8 for small ones */
static const unsigned short ic16[2][16] = {
	{ 0x0000, 0x07A0, 0x1FB0, 0x3038, 0x603C, 0x6020, 0xC000, 0xC003,
	  0xC003, 0xC003, 0x6006, 0x6006, 0x381C, 0x1FF8, 0x07E0, 0x0000 },
	{ 0x0000, 0x0180, 0x07E0, 0x0FF0, 0x0FF0, 0x0FF0, 0x1FF8, 0x1FF8,
	  0x1FF8, 0x3FFC, 0x3FFC, 0x7FFE, 0xFFFF, 0x0000, 0x03C0, 0x0180 }
};
static const unsigned short ic8[2][8] = {
	{ 0x3400, 0x4600, 0x8700, 0x8000, 0x8100, 0x4200, 0x3C00, 0x0000 },
	{ 0x1800, 0x3C00, 0x3C00, 0x7E00, 0x7E00, 0xFF00, 0x1800, 0x0000 }
};

short mark_w(void)
{
	return ch >= 14 ? 16 : 8;
}

void draw_mark(short x, short y, short n, int which, short color, const GRECT *clip)
{
	MFDB src, dst;
	short pxy[8], colors[2];
	unsigned short *bits = st_ram(32);
	/* the VDI's raster copy may not clip: only marks wholly inside */
	if (!bits || x < clip->x || y < clip->y || x + n > clip->x + clip->w || y + n > clip->y + clip->h)
		return;
	memcpy(bits, n == 16 ? ic16[which] : ic8[which], n * 2);
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = bits;
	src.fd_w = 16;
	src.fd_h = n;
	src.fd_wdwidth = 1;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = n - 1;
	pxy[3] = n - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + n - 1;
	pxy[7] = y + n - 1;
	colors[0] = color;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

void fill_pat(GRECT *r, short color, short interior, short style)
{
	short pxy[4];
	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	set_fill(interior, style, color);
	set_wrmode(interior >= 2 ? 2 : 1);	/* patterns over what is there */
	vr_recfl(vdi_h, pxy);
}

void fill(GRECT *r, short color)
{
	fill_pat(r, color, 1, 0);
}

void line_col(short x1, short y1, short x2, short y2, short color)
{
	short pxy[4];
	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	set_wrmode(1);
	set_lcolor(color);
	v_pline(vdi_h, 2, pxy);
}

void hline(short x1, short x2, short y)
{
	line_col(x1, y, x2, y, 1);
}

void vline(short x, short y1, short y2)
{
	line_col(x, y1, x, y2, 1);
}

void frame(GRECT *r, short color)
{
	short p[10];
	p[0] = r->x;
	p[1] = r->y;
	p[2] = r->x + r->w - 1;
	p[3] = r->y;
	p[4] = p[2];
	p[5] = r->y + r->h - 1;
	p[6] = r->x;
	p[7] = p[5];
	p[8] = r->x;
	p[9] = r->y;
	set_wrmode(1);
	set_lcolor(color);
	v_pline(vdi_h, 5, p);
}

/* an outline drawn in XOR: drawn twice it is gone (a dragged event) */
void xor_frame(GRECT *r)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	set_wrmode(3);
	set_lcolor(1);
	v_pline(vdi_h, 5, p);
	p[0] = p[6] = p[8] = r->x + 1;
	p[1] = p[3] = p[9] = r->y + 1;
	p[2] = p[4] = r->x + r->w - 2;
	p[5] = p[7] = r->y + r->h - 2;
	if (r->w > 4 && r->h > 4)
		v_pline(vdi_h, 5, p);
	set_wrmode(1);
}

short red_color(void)
{
	return ncolors >= 4 ? 2 : 1;
}

static short pens_ready;

/* the two greys for text and lines, found in the screen's own palette
   (pens_init); black where there are none */
static short grey_dark = 1, grey_light = 1;
static void pens_init(void);

short grey_color(void)
{
	if (ncolors < 16)
		return 1;
	text_grey(0);
	return grey_light != 1 ? grey_light : 8;
}

short text_grey(int pale)
{
	if (ncolors < 16)
		return 1;
	if (!pens_ready)
		pens_init();
	return pale ? grey_light : grey_dark;
}

/* the calendars' twelve colours, in VDI's 0-1000 RGB: saturated, so
   they stay bright on the screen's nearest colours */
static const short mac_rgb[NCOLORS][3] = {
	{ 1000, 156, 156 },
	{ 1000, 549, 0 },
	{ 1000, 843, 0 },
	{ 156, 745, 235 },
	{ 0, 431, 1000 },
	{ 588, 235, 862 },
	{ 588, 392, 196 },
	{ 549, 549, 549 },
	{ 1000, 392, 705 },
	{ 0, 705, 745 },
	{ 78, 117, 549 },
	{ 0, 431, 156 },
};
static short c16[NCOLORS] = { 2, 10, 6, 3, 4, 15, 14, 8, 7, 5, 12, 11 };

/* The standard palettes differ (the ST's index 9 is dark grey, the
   Falcon's dark red), and the user may have changed them: pick, for each
   of our colours, the nearest one the screen really has. */
static void pens_init(void)
{
	/* the most distinctive first: they get their nearest match, and the
	   others the nearest one still free, so no two calendar colours
	   look the same (on 16 colours purple, brown and grey all came
	   out the same grey) */
	static const short order[NCOLORS] = { 0, 2, 3, 4, 5, 9, 8, 10, 11, 1, 6, 7 };
	short j, i, n = ncolors > 256 || ncolors <= 0 ? 256 : ncolors, rgb[3];
	unsigned char taken[256];
	pens_ready = 1;
	if (ncolors < 16)
		return;
	memset(taken, 0, sizeof(taken));
	/* first the two greys for text, kept from the calendars' colours: a
	   real grey (red, green and blue alike) nearest each target, among
	   the sixteen system pens, as with 16 colours: with 256 a pen above
	   them came out green on Erez's Falcon (others' pictures set them) */
	{
		static const short want[2] = { 420, 690 };	/* dark, light (0-1000) */
		short g, ng = n < 16 ? n : 16;
		for (g = 0; g < 2; g++) {
			short pick = -1;
			long best = 0x7fffffffL;
			for (i = 2; i < ng; i++) {
				short lo, hi;
				long dist;
				if (taken[i])
					continue;
				vq_color(vdi_h, i, rgb);
				lo = rgb[0] < rgb[1] ? rgb[0] : rgb[1];
				lo = lo < rgb[2] ? lo : rgb[2];
				hi = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
				hi = hi > rgb[2] ? hi : rgb[2];
				if (hi - lo > 120 || hi < 200 || lo > 900)
					continue;	/* not a grey, or too near black or white
						   (NVDI's light grey is 851, TOS's 733) */
				dist = (long)(rgb[0] + rgb[1] + rgb[2]) / 3 - want[g];
				dist = dist < 0 ? -dist : dist;
				if (dist < best) {
					best = dist;
					pick = i;
				}
			}
			if (pick >= 0) {
				taken[pick] = 1;
				if (g)
					grey_light = pick;
				else
					grey_dark = pick;
			}
		}
		if (grey_dark == 1 && grey_light != 1)
			grey_dark = grey_light;	/* only one grey: for both */
		if (grey_light == 1 && grey_dark != 1)
			grey_light = grey_dark;	/* never black for a grey: a bar filled with it hid its text */
	}
	{
		/* in the log: the greys as the VDI said them (NVDI drew the
		   chosen address book's light grey bar black on a Falcon) */
		short d[3], l[3];
		char m[120];
		vq_color(vdi_h, grey_dark, d);
		vq_color(vdi_h, grey_light, l);
		snprintf(m, sizeof(m), "pens: %d colours (%d planes); grey dark %d (%d %d %d), light %d (%d %d %d)",
			 ncolors, planes, grey_dark, d[0], d[1], d[2], grey_light, l[0], l[1], l[2]);
		conn_log("EContact", "--", m, (long)strlen(m));
	}
	for (j = 0; j < NCOLORS; j++) {
		short k = order[j], pick = -1;
		long best = 0x7fffffffL;
		for (i = 2; i < n; i++) {
			long dr, dg, db, dist;
			if (taken[i])
				continue;
			vq_color(vdi_h, i, rgb);
			dr = rgb[0] - mac_rgb[k][0];
			dg = rgb[1] - mac_rgb[k][1];
			db = rgb[2] - mac_rgb[k][2];
			dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
			if (dist < best) {
				best = dist;
				pick = i;
			}
		}
		if (pick >= 0) {
			c16[k] = pick;
			taken[pick] = 1;
		}
	}
}

void pen_color(short k, PEN *p)
{
	/* 4 colours (white, black, red, green) and mono: patterns tell them apart */
	static const short c4[NCOLORS] = { 2, 2, 3, 3, 1, 1, 2, 1, 2, 3, 1, 3 };
	static const short int4[NCOLORS] = { 2, 3, 2, 2, 3, 3, 3, 3, 2, 3, 2, 3 };
	static const short sty4[NCOLORS] = { 2, 9, 3, 2, 7, 8, 1, 9, 6, 3, 4, 12 };
	static const short mono_int[NCOLORS] = { 2, 3, 2, 3, 2, 3, 3, 2, 3, 2, 3, 2 };
	static const short mono_sty[NCOLORS] = { 2, 7, 1, 8, 3, 9, 12, 4, 1, 6, 3, 5 };
	if (k < 0 || k >= NCOLORS)
		k = 4;
	if (!pens_ready)
		pens_init();
	if (ncolors >= 16) {
		p->color = c16[k];
		p->interior = 2;
		p->style = k == 2 ? 3 : 2;	/* yellow is pale: a little more */
	} else if (ncolors >= 4) {
		p->color = c4[k];
		p->interior = int4[k];
		p->style = sty4[k];
	} else {
		p->color = 1;
		p->interior = mono_int[k];
		p->style = mono_sty[k];
	}
}

short line_rtl(const char *s, long n)
{
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	return bidi_has_rtl(s, (short)n) && bidi_is_rtl(s, (short)n);
}

/* Some fonts have the Hebrew letters where ISO-8859-8 or DOS 862 put
   them; Settings says which, and the letters move there just before
   drawing (from EMail). */
static const unsigned char heb_order[27] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16,
	17, 18, 20, 22, 23, 24, 25, 26,
	15, 10, 13, 19, 21
};

void heb_font_map(char *s, long n, short where)
{
	long i;
	unsigned char base = where == HEB_ISO ? 0xE0 : 0x80;
	if (where != HEB_ISO && where != HEB_DOS)
		return;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0xC2 && c <= 0xDC)
			s[i] = (char)(base + heb_order[c - 0xC2]);
	}
}

static int has_heb(const char *s, long n)
{
	long i;
	for (i = 0; i < n; i++)
		if ((unsigned char)s[i] >= 0xC2 && (unsigned char)s[i] <= 0xDC)
			return 1;
	return 0;
}

static int is_heb(unsigned char c)
{
	return c >= 0xC2 && c <= 0xDC;
}

/* a vector font: the Hebrew letters by the font's own indices (NVDI's
   map mode 0), or in the system font, one cell each; the rest as usual */
static void runs_at(short x, short y, const char *s, short n)
{
	short i = 0;
	while (i < n) {
		short j = i, heb = is_heb((unsigned char)s[i]);
		while (j < n && is_heb((unsigned char)s[j]) == heb && j - i < 120)
			j++;
		if (!heb) {
			v_gtext_n(vdi_h, x + i * cw, y, s + i, j - i);
		} else if (heb_mode == HM_UNICODE) {
			unsigned short w[120];
			short k;
			for (k = i; k < j; k++)
				w[k - i] = heb_idx[(unsigned char)s[k] - 0xC2];
			vst_map_mode(vdi_h, 0);
			v_gtext_w(vdi_h, x + i * cw, y, w, j - i);
			vst_map_mode(vdi_h, 1);
		} else {
			/* the system font, each letter centred in its cell */
			short keep_cw = cw, keep_ch = ch, dw, dh, k;
			char c[BIDI_MAX];
			vst_font(vdi_h, 1);
			vst_height(vdi_h, keep_ch >= 16 ? 13 : 6, &dw, &dh);
			vst_alignment(vdi_h, 0, 5);
			memcpy(c, s + i, j - i);
			heb_font_map(c, j - i, opt.hebfont);
			for (k = 0; k < j - i; k++)
				v_gtext_n(vdi_h, x + (i + k) * keep_cw + (keep_cw - dw) / 2,
					  y + (keep_ch - dh) / 2, c + k, 1);
			font_reselect();
			cw = keep_cw;
			ch = keep_ch;
		}
		i = j;
	}
}

/* a few letters large, centred on cx, cy: the initials in the round
   picture. The system font at about h pixels high (the VDI doubles it
   where it has no bigger one); then the chosen font again */
void text_big(short cx, short cy, const char *s, short n, short h, short flags)
{
	short dw, dh, keep_cw = cw, keep_ch = ch;
	if (n <= 0)
		return;
	if (heb_mode != HM_MAP && has_heb(s, n)) {
		/* Hebrew drawn another way (runs_at): at the usual size */
		text_at(cx - n * cw / 2, cy - ch / 2, s, n, n, flags | TX_LTR);
		return;
	}
	vst_font(vdi_h, 1);
	/* vst_height takes the height above the baseline: 13 of the 8x16
	   font's 16 rows, 6 of the 8x8's 8 */
	vst_height(vdi_h, (short)(h * (keep_ch >= 16 ? 13 : 6) / (keep_ch >= 16 ? 16 : 8)), &dw, &dh);
	vst_alignment(vdi_h, 0, 5);
	set_wrmode(2);
	set_text((flags & TX_INVERSE) ? 0 : 1, (flags & TX_BOLD) ? 1 : 0);
	v_gtext_n(vdi_h, cx - n * dw / 2, cy - dh / 2, s, n);
	font_reselect();
	cw = keep_cw;
	ch = keep_ch;
}

void text_color(short color)
{
	next_text_color = color;
}

/* TOS's own VDI draws bold slowly (an ST in ST high: 24 ms for 18 letters,
   2.9 for them plain on a byte): there, on a white ground, the text is
   drawn plain and then ORed onto itself a pixel to the right, which is
   what its bold is (the same pixels in ST high, ST medium and a Falcon's
   16 colours: speed.py's bench, 26 Oct 2026). Not under NVDI or an EdDI
   driver (fast already, and drawing in a bitmap of its own) nor in
   another font than the system one, whose thickening may differ. */
static int smear_ok(void)
{
	static int ok = -1;
	if (ok < 0)
		ok = !get_cookie(0x4E564449L) && !get_cookie(0x45644449L);	/* NVDI, EdDI */
	return ok && font_is_system();
}

/* the cell x, y (n letters) ORed a pixel to the right; the VDI's raster
   copy doesn't clip: by hand, to the clip and the screen */
static void smear(short x, short y, short n)
{
	GRECT d;
	MFDB scr;
	short pxy[8];
	d.x = x + 1;
	d.y = y;
	d.w = n * cw;
	d.h = ch;
	if (clip_now.w && !rect_and(&d, &clip_now))
		return;
	{
		GRECT s;
		s.x = 0;
		s.y = 0;
		s.w = scr_w;
		s.h = scr_h;
		if (!rect_and(&d, &s) || d.x < 1)
			return;
	}
	memset(&scr, 0, sizeof(scr));
	pxy[0] = d.x - 1;
	pxy[1] = d.y;
	pxy[2] = d.x + d.w - 2;
	pxy[3] = d.y + d.h - 1;
	pxy[4] = d.x;
	pxy[5] = d.y;
	pxy[6] = d.x + d.w - 1;
	pxy[7] = d.y + d.h - 1;
	vro_cpyfm(vdi_h, 7, pxy, &scr, &scr);	/* S_OR_D */
}

void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	char vis[BIDI_MAX];
	const char *out = s;
	short i, color = next_text_color, effects;
	next_text_color = 1;
	if (cols <= 0 || n <= 0)
		return;
	/* the text up to a line end */
	{
		const char *nl = memchr(s, '\n', n);
		if (nl)
			n = nl - s;
	}
	if (n > cols)
		n = cols;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (n <= 0)
		return;
	if (bidi_has_rtl(s, (short)n)) {
		short rtl = (flags & TX_LTR) ? 0 : bidi_is_rtl(s, (short)n);
		bidi_visual(s, (short)n, rtl, vis);
		out = vis;
		if (rtl && (flags & TX_RIGHT))
			x += (cols - (short)n) * cw;
	}
	if (flags & TX_CENTER)
		x += (cols - (short)n) * cw / 2;
	/* the greys where the screen has them; disabled text is light grey,
	   or dotted (the light effect) without colours */
	effects = ((flags & TX_BOLD) ? 1 : 0) | ((flags & TX_UNDER) ? 8 : 0);
	if (color == 1 && (flags & (TX_GREY | TX_PALE | TX_DIM))) {
		short g = text_grey((flags & (TX_PALE | TX_DIM)) != 0);
		if (g != 1)
			color = g;
		else if (flags & TX_DIM)
			effects |= 2;
	}
	if (heb_mode != HM_MAP && has_heb(out, n)) {
		set_wrmode(2);
		set_text((flags & TX_INVERSE) ? 0 : color, effects);
		runs_at(x, y, out, (short)n);
		return;
	}
	if (opt.hebfont != HEB_ATARI && has_heb(out, n)) {
		if (out != vis)
			memcpy(vis, out, n);
		out = vis;
		heb_font_map(vis, n, opt.hebfont);
	}
	set_wrmode(2);
	/* (not when the clip cuts the text's start: the column left of the
	   clip would be ORed in as it is, not as the letter makes it) */
	if (effects == 1 && (flags & TX_WHITE) && !(flags & TX_INVERSE) && (!clip_now.w || clip_now.x <= x) &&
	    smear_ok()) {
		set_text(color, 0);
		for (i = 0; i < n; i += 120) {
			short k = n - i > 120 ? 120 : (short)(n - i);
			v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
		}
		smear(x, y, (short)n);
		return;
	}
	set_text((flags & TX_INVERSE) ? 0 : color, effects);
	for (i = 0; i < n; i += 120) {
		short k = n - i > 120 ? 120 : (short)(n - i);
		v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
	}
}

long wrap_text(const char *s, long n, short width, long *breaks, long max)
{
	long count = 0, i = 0;
	if (width < 4)
		width = 4;
	while (i <= n && count < max) {
		long start = i, end, sp = -1, j;
		breaks[count++] = start;
		for (j = start; j < n && s[j] != '\n' && j - start < width; j++)
			if (s[j] == ' ')
				sp = j;
		if (j >= n)
			break;
		if (s[j] == '\n') {
			i = j + 1;
			if (i == n)
				break;
			continue;
		}
		end = (sp > start) ? sp + 1 : j;
		i = end;
	}
	return count;
}

/* the selected row: the Mac's blue where there are colours, else black */
short sel_color(void)
{
	static short pick = -1;
	short i, rgb[3], n = ncolors > 256 ? 256 : ncolors;
	long best = 0x7fffffffL;
	if (ncolors < 16)
		return 1;
	if (pick >= 0)
		return pick;
	/* the screen's colour nearest the Mac's selection blue, dark enough
	   for white text on it */
	pick = 1;
	for (i = 2; i < n; i++) {
		long dr, dg, db, d;
		vq_color(vdi_h, i, rgb);
		if (rgb[2] < 600 || rgb[0] > 400 || rgb[1] > 650)
			continue;
		dr = rgb[0] - 120;
		dg = rgb[1] - 400;
		db = rgb[2] - 880;
		d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < best) {
			best = d;
			pick = i;
		}
	}
	return pick;
}

/* a filled ellipse, row by row (the round picture of a contact) */
void disc(short cx, short cy, short rx, short ry, short color)
{
	short y;
	if (ry < 1 || rx < 1)
		return;
	for (y = -ry; y <= ry; y++) {
		/* half its width on this row: rx * sqrt(1 - (y/ry)^2) */
		long t = (long)ry * ry - (long)y * y, s = 0, b = 1L << 20;
		GRECT r;
		t *= (long)rx * rx;
		t /= (long)ry * ry;
		while (b > t)
			b >>= 2;
		while (b) {
			if (t >= s + b) {
				t -= s + b;
				s = (s >> 1) + b;
			} else {
				s >>= 1;
			}
			b >>= 2;
		}
		r.x = cx - (short)s;
		r.y = cy + y;
		r.w = (short)(2 * s + 1);
		r.h = 1;
		fill(&r, color);
	}
}

/* ---------------- photos ----------------
 * A contact's picture (PHOTO_N x PHOTO_N, RGB 5-6-5) in an ellipse:
 * dithered (Floyd-Steinberg) to the colours the screen has, black and
 * white with fewer than 16, then one transparent copy of a one-plane
 * mask per colour used: no device formats to know, and white (the card
 * behind it) is left as it is. The dithered picture is kept while the
 * same one is shown, so a redraw is only the copies. */

#define PH_MAXW 160
#define PH_MAXH 128

#define PH_NONE 255			/* in ph_lut: not looked up yet */

static unsigned char ph_lut[4096];	/* RGB 4-4-4 -> pen */
static unsigned char ph_rgb[256][3];
static short ph_npal = -1;
static long ph_palsum;

/* the pen nearest RGB 4-4-4 colour i, kept in ph_lut. 16-bit
   differences: each square is one muls.w, and a pen out of the running
   is left after its first channel */
static short ph_pen(short i)
{
	short r = ((i >> 8) & 15) * 17, g = ((i >> 4) & 15) * 17, b = (i & 15) * 17, k, pick = 0;
	long best = 0x7fffffffL;
	for (k = 0; k < ph_npal; k++) {
		short dr = r - ph_rgb[k][0], dg = g - ph_rgb[k][1], db = b - ph_rgb[k][2];
		long d = (long)dr * dr * 3;
		if (d >= best)
			continue;
		d += (long)dg * dg * 4;
		if (d >= best)
			continue;
		d += (long)db * db * 2;
		if (d < best) {
			best = d;
			pick = k;
		}
	}
	ph_lut[i] = (unsigned char)pick;
	return pick;
}

static void photo_palette(void)
{
	/* (255 marks what isn't drawn: the last of 256 colours isn't used) */
	short n = ncolors >= 16 ? (ncolors > 255 ? 255 : ncolors) : 2, i, rgb[3];
	long sum = n;
	for (i = 0; i < n; i++) {
		if (ncolors >= 16) {
			vq_color(vdi_h, i, rgb);
		} else {
			rgb[0] = rgb[1] = rgb[2] = i ? 0 : 1000;	/* white, black */
		}
		ph_rgb[i][0] = (unsigned char)(rgb[0] * 255L / 1000);
		ph_rgb[i][1] = (unsigned char)(rgb[1] * 255L / 1000);
		ph_rgb[i][2] = (unsigned char)(rgb[2] * 255L / 1000);
		sum = sum * 31 + ph_rgb[i][0] * 7 + ph_rgb[i][1] * 3 + ph_rgb[i][2];
	}
	if (n == ph_npal && sum == ph_palsum)
		return;
	ph_npal = n;
	ph_palsum = sum;
	if (n == 2)
		return;			/* black and white: no table, a threshold */
	/* each colour's nearest pen is looked up when a photo first has it
	   (ph_pen): all 4096 against 255 pens took 18 s on a Falcon in 256
	   colours, and a photo has a few hundred of them */
	memset(ph_lut, PH_NONE, sizeof(ph_lut));
}

/* one pen's pixels of rows y0 to y1 (bits, wpl words a row) copied to
   the screen, transparent, where the photo shows (vis) */
static void ph_copy(unsigned short *bits, short wpl, const GRECT *vis, short cx, short cy, short rx,
                    short ry, short y0, short y1, short pen)
{
	short pxy[8], colors[2];
	MFDB src, dst;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = bits;
	src.fd_w = wpl * 16;
	src.fd_h = y1 - y0;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	pxy[0] = vis->x - (cx - rx);
	pxy[1] = 0;
	pxy[2] = pxy[0] + vis->w - 1;
	pxy[3] = y1 - y0 - 1;
	pxy[4] = vis->x;
	pxy[5] = cy - ry + y0;
	pxy[6] = vis->x + vis->w - 1;
	pxy[7] = cy - ry + y1 - 1;
	colors[0] = pen;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

/* The photo's pens, dithered: kept for the picture, size and palette
   they were made for. dither() makes them a few rows at a time when
   asked (more), so a photo shown for the first time doesn't hold the
   machine (an 8 MHz ST: 390 ms for 65 by 65 in ST high), or all at
   once; the rows are the same either way. */
static unsigned char *pens;
static short pw, ph, pserial = -1, pnpal;
static unsigned char used[256];
/* with many pens (256 colours), each pen's pixels in a list, so
   drawing a pen goes through its own pixels and not the whole
   picture once a pen: (y << 8) | x, pen by pen, from ph_first[pen] */
static unsigned short *at, first[257];
static short lists;
static long pcap, acap;		/* what pens and at hold: the picture's size, not the most */
/* dithering under way: the next row, -1 none; errors carried to the
   right and the next row */
static short dz_y = -1, dz_serial, dz_w, dz_h, dz_npal;
static short err[2][PH_MAXW + 2][3];
static short ge[2][PH_MAXW + 2];	/* black and white's one channel */
static short half[PH_MAXH];	/* the ellipse's half width on each row */
static unsigned char row[PH_MAXW];	/* the source column of each x */

static int dither(const unsigned short *px, int serial, short rx, short ry, int (*more)(void))
{
	short W, H, x, y, k;
	int grey, nq;
	if (rx > PH_MAXW / 2)
		rx = PH_MAXW / 2;
	if (ry > PH_MAXH / 2)
		ry = PH_MAXH / 2;
	W = 2 * rx + 1;
	H = 2 * ry + 1;
	photo_palette();
	if ((long)W * H + 4 > pcap) {
		free(pens);
		pcap = (long)W * H + 4;
		if (!(pens = malloc(pcap))) {
			pcap = 0;
			return 0;
		}
		pserial = -1;		/* dithered again into it */
		dz_y = -1;
	}
	if (serial == pserial && W == pw && H == ph && pnpal == ph_npal)
		return 1;
	grey = ph_npal == 2;	/* black and white: one channel */
	nq = grey ? 1 : 3;
	if (dz_y < 0 || serial != dz_serial || W != dz_w || H != dz_h || ph_npal != dz_npal) {
		/* begun: shifts and adds only, a 68000 multiplies and divides
		   32-bit numbers in software */
		memset(err, 0, sizeof(err));
		memset(ge, 0, sizeof(ge));
		memset(used, 0, sizeof(used));
		for (y = 0; y < H; y++) {
			/* rx * sqrt(1 - ((y - ry) / ry)^2), once a row */
			long t = ((long)ry * ry - (long)(y - ry) * (y - ry)) * ((long)rx * rx) / ((long)ry * ry), sq = 0, bit = 1L << 20;
			while (bit > t)
				bit >>= 2;
			while (bit) {
				if (t >= sq + bit) {
					t -= sq + bit;
					sq = (sq >> 1) + bit;
				} else {
					sq >>= 1;
				}
				bit >>= 2;
			}
			half[y] = (short)sq;
		}
		for (x = 0; x < W; x++)
			row[x] = (unsigned char)((long)x * PHOTO_N / W);
		dz_y = 0;
		dz_serial = (short)serial;
		dz_w = W;
		dz_h = H;
		dz_npal = ph_npal;
	}
	for (y = dz_y; y < H; y++) {
		if (more && y > dz_y && !more()) {
			dz_y = y;
			return 0;
		}
		if (grey) {
			/* black and white (ST high): one channel, its own loop, the
			   same sums as the colours' below (the three-channel loop
			   took an 8 MHz ST 720 ms for a 65 by 65 picture) */
			short *g0 = ge[y & 1], *g1 = ge[(y + 1) & 1];
			const unsigned short *src = px + ((long)y * PHOTO_N / H) * PHOTO_N;
			unsigned char *out = pens + (long)y * W;
			short lo = rx - half[y], hi = rx + half[y], white = ph_rgb[0][0], black = ph_rgb[1][0];
			memset(g1, 0, sizeof(ge[0]));
			for (x = 0; x < W; x++) {
				unsigned short p = src[row[x]];
				short v = (short)((((p >> 8) & 0xF8) + (((p >> 3) & 0xFC) << 1) + ((p << 3) & 0xF8)) >> 2), e;
				unsigned char pen;
				v = v + (g0[x + 1] >> 4);
				v = v < 0 ? 0 : v > 255 ? 255 : v;
				pen = v < 128;
				if (x < lo || x > hi) {
					out[x] = 255;
				} else {
					out[x] = pen;
					used[pen] = 1;
				}
				e = v - (pen ? black : white);
				g0[x + 2] += (e << 3) - e;		/* 7/16 to the right */
				g1[x] += (e << 1) + e;			/* 3/16 below left */
				g1[x + 1] += (e << 2) + e;		/* 5/16 below */
				g1[x + 2] += e;				/* 1/16 below right */
			}
			continue;
		}
		{
			short (*e0)[3] = err[y & 1], (*e1)[3] = err[(y + 1) & 1];
			const unsigned short *src = px + ((long)y * PHOTO_N / H) * PHOTO_N;
			unsigned char *out = pens + (long)y * W;
			short lo = rx - half[y], hi = rx + half[y];
			memset(e1, 0, sizeof(err[0]));
			for (x = 0; x < W; x++) {
				unsigned short p = src[row[x]];
				short c[3], q, pen;
				c[0] = (short)((p >> 8) & 0xF8);
				c[1] = (short)((p >> 3) & 0xFC);
				c[2] = (short)((p << 3) & 0xF8);
				if (grey)	/* (r + 2g + b) / 4: shifts, no multiplying */
					c[0] = c[1] = c[2] = (short)((c[0] + (c[1] << 1) + c[2]) >> 2);
				for (q = 0; q < nq; q++) {
					short v = c[q] + (e0[x + 1][q] >> 4);
					c[q] = v < 0 ? 0 : v > 255 ? 255 : v;
				}
				if (grey) {
					pen = c[0] < 128;
				} else {
					short i = ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4);
					pen = ph_lut[i];
					if (pen == PH_NONE)
						pen = ph_pen(i);
				}
				/* outside the ellipse: not drawn (255) */
				if (x < lo || x > hi) {
					out[x] = 255;
				} else {
					out[x] = (unsigned char)pen;
					used[pen] = 1;
				}
				for (q = 0; q < nq; q++) {
					short e = c[q] - ph_rgb[pen][q];
					e0[x + 2][q] += (e << 3) - e;		/* 7/16 to the right */
					e1[x][q] += (e << 1) + e;		/* 3/16 below left */
					e1[x + 1][q] += (e << 2) + e;		/* 5/16 below */
					e1[x + 2][q] += e;			/* 1/16 below right */
				}
			}
		}
	}
	dz_y = -1;
	pserial = (short)serial;
	pw = W;
	ph = H;
	pnpal = ph_npal;
	lists = 0;
	if (ph_npal > 16 && (long)W * H > acap) {
		free(at);
		acap = (long)W * H;
		if (!(at = malloc(acap * 2)))
			acap = 0;
	}
	if (ph_npal > 16 && at) {
		/* how many each pen has, where each one's list starts,
		   then the pixels in order: the lists come out top to bottom */
		static unsigned short fill[256];
		memset(fill, 0, sizeof(fill));
		for (y = 0; y < H; y++)
			for (x = 0; x < W; x++)
				if (pens[(long)y * W + x] != 255)
					fill[pens[(long)y * W + x]]++;
		first[0] = 0;
		for (k = 0; k < 256; k++) {
			first[k + 1] = first[k] + fill[k];
			fill[k] = first[k];
		}
		for (y = 0; y < H; y++)
			for (x = 0; x < W; x++) {
				short pn = pens[(long)y * W + x];
				if (pn != 255)
					at[fill[pn]++] = (unsigned short)((y << 8) | x);
			}
		lists = 1;
	}
	return 1;
}

int photo_dither(const unsigned short *px, int serial, short rx, short ry, int (*more)(void))
{
	return dither(px, serial, rx, ry, more);
}

void draw_photo(const unsigned short *px, int serial, short cx, short cy, short rx, short ry, const GRECT *clip)
{
	short W, H, x, y, k, wpl;
	GRECT vis;
	if (rx > PH_MAXW / 2)
		rx = PH_MAXW / 2;
	if (ry > PH_MAXH / 2)
		ry = PH_MAXH / 2;
	W = 2 * rx + 1;
	H = 2 * ry + 1;
	if (!dither(px, serial, rx, ry, 0))
		return;
	/* where it shows: the VDI's raster copy may not clip, so by hand */
	vis.x = cx - rx;
	vis.y = cy - ry;
	vis.w = W;
	vis.h = H;
	if (!rect_and(&vis, clip))
		return;
	wpl = (W + 15) / 16;
	if (lists) {
		short top = vis.y - (cy - ry), bottom = top + vis.h, strip = (short)(1024 / (wpl * 2));
		for (k = 1; k < ph_npal; k++) {		/* pen 0 is the white under it */
			const unsigned short *p = at + first[k], *end = at + first[k + 1];
			short y0, y1;
			while (p < end && (short)(*p >> 8) < top)
				p++;
			for (y0 = top; y0 < bottom && p < end; y0 = y1) {
				unsigned short *bits;
				y1 = y0 + strip < bottom ? y0 + strip : bottom;
				if ((short)(*p >> 8) >= y1)
					continue;	/* none in these rows */
				bits = st_ram((long)wpl * 2 * strip);
				if (!bits)
					return;
				memset(bits, 0, (size_t)wpl * 2 * (y1 - y0));
				for (; p < end && (short)(*p >> 8) < y1; p++) {
					short col = *p & 0xFF;
					bits[(long)((*p >> 8) - y0) * wpl + (col >> 4)] |= 0x8000 >> (col & 15);
				}
				ph_copy(bits, wpl, &vis, cx, cy, rx, ry, y0, y1, k);
			}
		}
		return;
	}
	for (k = 1; k < ph_npal; k++) {		/* pen 0 is the white under it */
		short y0, strip = (short)(1024 / (wpl * 2));
		if (!used[k])
			continue;
		for (y0 = vis.y - (cy - ry); y0 < vis.y - (cy - ry) + vis.h; y0 += strip) {
			short y1 = y0 + strip, any = 0;
			unsigned short *bits = st_ram((long)wpl * 2 * strip);
			if (!bits)
				return;
			if (y1 > vis.y - (cy - ry) + vis.h)
				y1 = vis.y - (cy - ry) + vis.h;
			memset(bits, 0, (size_t)wpl * 2 * (y1 - y0));
			for (y = y0; y < y1; y++) {
				const unsigned char *in = pens + (long)y * W;
				unsigned short *o = bits + (long)(y - y0) * wpl, m = 0x8000;
				for (x = 0; x < W; x++) {
					if (in[x] == k) {
						*o |= m;
						any = 1;
					}
					m >>= 1;
					if (!m) {
						m = 0x8000;
						o++;
					}
				}
			}
			if (any)
				ph_copy(bits, wpl, &vis, cx, cy, rx, ry, y0, y1, k);
		}
	}
}
