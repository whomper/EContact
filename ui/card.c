/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * card.c - the chosen person's card, as the Mac shows it: a round
 * picture with the initials, the name in bold, the job and company
 * under it, a "mail" button, then each phone, email, web page, address,
 * the birthday and the note, its label in grey on the left. Under it
 * all a bar with "+" for a new contact and "Edit".
 *
 * A click on a value offers what goes with it: write an email in EMail,
 * or copy it to the clipboard.
 *
 * The photo, when the card has one, takes the place of the initials. It
 * is read (src/photo.c) a moment after a contact is chosen, from the
 * event loop, so going down the list with the arrow keys doesn't wait
 * for each picture; the initials show until it's there.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/photo.h"
#include "../src/plat.h"
#include "../src/conn.h"

/* the photo shown, whose it is, and the last one tried */
static PHOTOIMG cur;
static int cur_ok, cur_serial;
static char cur_uid[100], tried_uid[100];
static short cur_book = -1, tried_book = -1;
static unsigned long cur_key, tried_key;
static int want;
static unsigned long want_at;
static GRECT r_avatar;

static int same(const CONTACT *c, const char *uid, short book, unsigned long key)
{
	return c->book == book && c->photo_key == key && !strcmp(c->uid ? c->uid : "", uid);
}

static void ask_for(const CONTACT *c)
{
	if (!opt.photos || !c->photo_key || same(c, tried_uid, tried_book, tried_key))
		return;
	if (!want) {
		want = 1;
		want_at = pf_ms();
	}
}

int card_photo_pending(void)
{
	return want;
}

void card_photo_recheck(void)
{
	/* a refresh may have brought new photos, or new pictures behind links */
	tried_uid[0] = 0;
	tried_book = -1;
	photo_prune();
	if (sel_contact())
		ask_for(sel_contact());
}

void card_photo_tick(void)
{
	CONTACT *c;
	PHOTOIMG *img;
	int how, r, quick;
	unsigned long serial = people_serial, t0, tl = 0;
	if (!want || pf_ms() - want_at < 250UL)
		return;
	want = 0;
	t0 = pf_ms();
	c = sel_contact();
	if (!c || !c->photo_key || !opt.photos)
		return;
	str_copy(tried_uid, c->uid ? c->uid : "", sizeof(tried_uid));
	tried_book = c->book;
	tried_key = c->photo_key;
	quick = photo_ready(c);
	if (!quick) {
		busy(1);
		status(opt.offline ? "Reading the photo..." : "Getting the photo...");
	}
	img = malloc(sizeof(PHOTOIMG));
	{
		/* a card kept in part is fetched whole: kept in memory while
		   its photo is decoded, then written (one open a file) */
		int job = img && c->photo_key == VC_KEY_UNKNOWN && !opt.offline && book_fetch_job(c->book);
		r = img ? photo_get(c, img, !opt.offline, &how) : PH_FAILED;
		if (job)
			book_fetch_job_end();
	}
	if (!quick) {
		busy(0);
		status_updated();
	}
	if (people_serial != serial) {
		/* the card was fetched whole for its photo: the list was read
		   again (c is gone), and the card has its photo's key now */
		unsigned long t = pf_ms();
		after_change();
		tl = pf_ms() - t;
		c = sel_contact();
		if (c) {
			tried_key = c->photo_key;
			tried_book = c->book;
		}
	}
	if (r == PH_FAILED) {
		/* the initials stay: the status line says why */
		char m[120];
		snprintf(m, sizeof(m), "The photo can't be shown: %s.", photo_why_text(photo_why));
		status(m);
	}
	if (r == PH_OK) {
		cur = *img;
		cur_ok = 1;
		cur_serial++;
		str_copy(cur_uid, tried_uid, sizeof(cur_uid));
		cur_book = tried_book;
		cur_key = tried_key;
		win_redraw(&r_avatar);
		if (!quick) {
			/* with Write a log on: the whole wait, as the user saw it */
			char m[100];
			unsigned long ms = pf_ms() - t0;
			snprintf(m, sizeof(m), "shown after %lu.%02lu s", ms / 1000, ms % 1000 / 10);
			if (tl)
				snprintf(m + strlen(m), sizeof(m) - strlen(m), " (the list read again: %lu.%02lu s)",
					 tl / 1000, tl % 1000 / 10);
			conn_log("Photo", " -- ", m, (long)strlen(m));
			conn_log_flush();
		}
	}
	free(img);
}

/* the values on the card, for clicks */
enum { HV_TEL, HV_EMAIL, HV_URL, HV_ADR, HV_BDAY, HV_NOTE, HV_MAILBTN };
typedef struct {
	GRECT r;
	short kind, idx;
} HIT;
static HIT hits[48];
static short nhits;
static GRECT b_new, b_edit, r_bar;

static void add_hit(short x, short y, short w, short h, short kind, short idx)
{
	if (nhits >= (short)(sizeof(hits) / sizeof(hits[0])))
		return;
	hits[nhits].r.x = x;
	hits[nhits].r.y = y;
	hits[nhits].r.w = w;
	hits[nhits].r.h = h;
	hits[nhits].kind = kind;
	hits[nhits].idx = idx;
	nhits++;
}

static void layout_bar(void)
{
	short bh = ch + 8;
	r_bar.x = r_card.x;
	r_bar.y = r_card.y + r_card.h - bh;
	r_bar.w = r_card.w;
	r_bar.h = bh;
	b_new.x = r_bar.x + cw;
	b_new.y = r_bar.y + 3;
	b_new.w = 3 * cw;
	b_new.h = ch + 2;
	b_edit.w = 6 * cw;
	b_edit.h = ch + 2;
	b_edit.x = r_bar.x + r_bar.w - b_edit.w - cw;
	b_edit.y = r_bar.y + 3;
}

static void button(GRECT *b, const char *label, short dim)
{
	short n = (short)strlen(label);
	fill(b, 0);
	frame(b, dim ? grey_color() : 1);
	text_at(b->x + (b->w - n * cw) / 2, b->y + 1, label, n, n, TX_LTR | (dim ? TX_DIM : 0));
}

/* a value: Hebrew to the right of its column */
static void value_at(short x, short y, const char *s, short cols, short flags)
{
	long n = (long)strlen(s);
	text_at(x, y, s, n, cols, flags | (line_rtl(s, n) ? TX_RIGHT : TX_LTR));
}

/* the round picture: the photo, or grey with white initials, as the Mac's */
static void avatar(const CONTACT *c, short cx, short cy, short ry, const GRECT *clip)
{
	char ini[4];
	short rx = (short)((long)ry * 2 * cw / ch), n;
	short col = ncolors >= 16 ? text_grey(1) : 1;
	r_avatar.x = cx - rx;
	r_avatar.y = cy - ry;
	r_avatar.w = 2 * rx + 1;
	r_avatar.h = 2 * ry + 1;
	if (opt.photos && cur_ok && same(c, cur_uid, cur_book, cur_key)) {
		draw_photo(cur.px, cur_serial, cx, cy, rx, ry, clip);
		/* a refresh may have changed the picture behind a link */
		ask_for(c);
		return;
	}
	ask_for(c);
	disc(cx, cy, rx, ry, col);
	if (ncolors >= 4 && ncolors < 16) {
		/* four colours: no grey, a ring keeps it light */
		disc(cx, cy, rx - 2, ry - 1, 0);
		disc(cx, cy, rx - 4, ry - 2, 1);
	}
	vcard_initials(c, ini);
	n = (short)strlen(ini);
	/* as on the Mac, the letters about half as high as the circle */
	text_big(cx, cy, ini, n, ry, TX_INVERSE);	/* doubled: bold already */
}

static void label_at(short x, short y, short lw, const char *label)
{
	char l[40];
	short n;
	str_copy(l, label, sizeof(l));
	n = (short)strlen(l);
	if (n > lw)
		n = lw;
	text_at(x + (lw - n) * cw, y, l, n, n, TX_GREY | TX_LTR);
}

void card_draw(GRECT *clip)
{
	GRECT r = r_card;
	CONTACT *c;
	short x0, y, lw, vx, cols, rh = ch + 4, ry, i, bottom;
	char s[200];
	if (!rect_and(&r, clip))
		return;
	clip_on(&r);
	fill(&r, 0);
	layout_bar();
	nhits = 0;
	c = sel_contact();
	bottom = r_bar.y - 2;
	if (!c) {
		const char *t = nlisted ? "No Card Selected" : "";
		short n = (short)strlen(t);
		text_at(r_card.x + (r_card.w - n * cw) / 2, r_card.y + (r_card.h - ch) / 2, t, n, n, TX_LTR | TX_GREY);
	} else {
		x0 = r_card.x + 2 * cw;
		y = r_card.y + ch;
		/* the picture and the name beside it */
		ry = ch >= 16 ? 2 * ch : 3 * ch;
		if (ry * 2 > r_card.h / 3)
			ry = r_card.h / 6;
		avatar(c, x0 + (short)((long)ry * 2 * cw / ch), y + ry, ry, &r);
		{
			short nx = x0 + (short)((long)ry * 4 * cw / ch) + 2 * cw, ncols = (r_card.x + r_card.w - nx) / cw - 1;
			short ny = y + ry - ch - ch / 2;
			char job[200];
			vcard_display_name(c, 0, s, sizeof(s));
			if (c->prefix && *c->prefix && !c->company) {
				char t[200];
				snprintf(t, sizeof(t), "%s %s", c->prefix, s);
				str_copy(s, t, sizeof(s));
			}
			value_at(nx, ny, s, ncols, TX_BOLD);
			/* "Chief Tinkerer, Hardware, Atari Club", or the person at a company */
			job[0] = 0;
			if (c->company) {
				if (c->given || c->family) {
					snprintf(job, sizeof(job), "%s%s%s", c->given ? c->given : "", c->given && c->family ? " " : "",
						 c->family ? c->family : "");
				}
			} else {
				if (c->title)
					str_copy(job, c->title, sizeof(job));
				if (c->dept)
					snprintf(job + strlen(job), sizeof(job) - strlen(job), "%s%s", job[0] ? ", " : "", c->dept);
				if (c->org)
					snprintf(job + strlen(job), sizeof(job) - strlen(job), "%s%s", job[0] ? ", " : "", c->org);
			}
			if (job[0])
				value_at(nx, ny + ch + 2, job, ncols, TX_GREY);
			if (c->nick && *c->nick) {
				snprintf(s, sizeof(s), "\"%s\"", c->nick);
				value_at(nx, ny + 2 * ch + 4, s, ncols, TX_GREY);
			}
			/* the Mac's mail button */
			if (c->nemail) {
				GRECT b;
				b.x = nx;
				b.y = y + 2 * ry + ch / 2;
				b.w = 6 * cw;
				b.h = ch + 2;
				if (b.y + b.h < bottom) {
					fill(&b, 0);
					frame(&b, 1);
					text_at(b.x + cw, b.y + 1, "mail", 4, 4, TX_LTR);
					add_hit(b.x, b.y, b.w, b.h, HV_MAILBTN, 0);
				}
			}
		}
		y += 2 * ry + ch / 2 + ch + 2 + ch / 2;
		line_col(x0, y, r_card.x + r_card.w - 2 * cw, y, grey_color());
		y += ch / 2;
		lw = 8;
		vx = x0 + (lw + 2) * cw;
		cols = (r_card.x + r_card.w - vx) / cw - 1;
		if (cols < 8) {
			lw = 6;
			vx = x0 + (lw + 1) * cw;
			cols = (r_card.x + r_card.w - vx) / cw - 1;
		}
		for (i = 0; i < c->ntel && y + ch < bottom; i++, y += rh) {
			label_at(x0, y, lw, c->tel[i].label);
			value_at(vx, y, c->tel[i].value, cols, 0);
			add_hit(vx, y, cols * cw, ch, HV_TEL, i);
		}
		if (c->ntel)
			y += rh / 3;
		for (i = 0; i < c->nemail && y + ch < bottom; i++, y += rh) {
			label_at(x0, y, lw, c->email[i].label);
			value_at(vx, y, c->email[i].value, cols, 0);
			add_hit(vx, y, cols * cw, ch, HV_EMAIL, i);
		}
		if (c->nemail)
			y += rh / 3;
		for (i = 0; i < c->nurl && y + ch < bottom; i++, y += rh) {
			label_at(x0, y, lw, c->url[i].label);
			value_at(vx, y, c->url[i].value, cols, 0);
			add_hit(vx, y, cols * cw, ch, HV_URL, i);
		}
		if (c->nurl)
			y += rh / 3;
		for (i = 0; i < c->nadr && y + ch < bottom; i++) {
			/* street, then "postcode city", region, country on their lines */
			char tmp[400], *part[5], *p = tmp, line[200];
			short k, y0 = y;
			str_copy(tmp, c->adr[i].value, sizeof(tmp));
			for (k = 0; k < 5; k++) {
				part[k] = p;
				if (p && (p = strchr(p, '\n')) != 0)
					*p++ = 0;
				if (!part[k])
					part[k] = "";
			}
			label_at(x0, y, lw, c->adr[i].label);
			if (part[0][0] && y + ch < bottom) {
				value_at(vx, y, part[0], cols, 0);
				y += ch + 1;
			}
			if ((part[1][0] || part[3][0]) && y + ch < bottom) {
				snprintf(line, sizeof(line), "%s%s%s", part[3], part[3][0] && part[1][0] ? " " : "", part[1]);
				value_at(vx, y, line, cols, 0);
				y += ch + 1;
			}
			if (part[2][0] && y + ch < bottom) {
				value_at(vx, y, part[2], cols, 0);
				y += ch + 1;
			}
			if (part[4][0] && y + ch < bottom) {
				value_at(vx, y, part[4], cols, 0);
				y += ch + 1;
			}
			add_hit(vx, y0, cols * cw, y - y0, HV_ADR, i);
			y += rh - ch;
		}
		if (c->nadr)
			y += rh / 3;
		if (vcard_bday_format(c->bday, s, sizeof(s)) && y + ch < bottom) {
			label_at(x0, y, lw, "birthday");
			value_at(vx, y, s, cols, 0);
			add_hit(vx, y, cols * cw, ch, HV_BDAY, 0);
			y += rh + rh / 3;
		}
		if (c->note && *c->note && y + ch < bottom) {
			long br[40], len = (long)strlen(c->note), nl = wrap_text(c->note, len, cols, br, 40), k;
			short y0 = y;
			label_at(x0, y, lw, "note");
			/* breaks[] are where each line starts */
			for (k = 0; k < nl && y + ch < bottom; k++) {
				long st = br[k], e = k + 1 < nl ? br[k + 1] : len, n = e - st;
				char ln[200];
				if (n > (long)sizeof(ln) - 1)
					n = sizeof(ln) - 1;
				memcpy(ln, c->note + st, n);
				ln[n] = 0;
				while (n && (ln[n - 1] == '\n' || ln[n - 1] == ' '))
					ln[--n] = 0;
				value_at(vx, y, ln, cols, 0);
				y += ch + 1;
			}
			add_hit(vx, y0, cols * cw, y - y0, HV_NOTE, 0);
		}
		{
			/* which book it is in, quietly at the bottom, when there are several */
			if (nbooks > 1 && c->book >= 0 && c->book < nbooks && bottom - ch - 2 > y) {
				snprintf(s, sizeof(s), "%s%s", books[c->book].name, book_writable(&books[c->book]) ? "" : " (read-only)");
				text_at(x0, bottom - ch - 2, s, (long)strlen(s), (r_card.w - 4 * cw) / cw, TX_PALE | TX_LTR);
			}
		}
	}
	/* the bar: + and Edit */
	line_col(r_bar.x, r_bar.y, r_bar.x + r_bar.w - 1, r_bar.y, grey_color());
	{
		GRECT bb = r_bar;
		bb.y++;
		bb.h--;
		fill(&bb, 0);
	}
	button(&b_new, "+", book_for_new() < 0);
	button(&b_edit, "Edit", !c);
	clip_on(clip);
}

int card_instant(short mx, short my)
{
	layout_bar();
	return rect_hit(&b_new, mx, my) || rect_hit(&b_edit, mx, my);
}

static void value_menu(short mx, short my, CONTACT *c, HIT *h)
{
	const char *items[3];
	short n = 0, k, mail = h->kind == HV_EMAIL || h->kind == HV_MAILBTN;
	const char *val = "";
	switch (h->kind) {
	case HV_TEL: val = c->tel[h->idx].value; break;
	case HV_EMAIL: val = c->email[h->idx].value; break;
	case HV_MAILBTN: val = c->email[0].value; break;
	case HV_URL: val = c->url[h->idx].value; break;
	case HV_ADR:	/* the address on lines, as on an envelope */
		val = c->adr[h->idx].value;
		break;
	case HV_BDAY: val = c->bday; break;
	case HV_NOTE: val = c->note; break;
	}
	if (h->kind == HV_MAILBTN && c->nemail == 1) {
		cmd_mail_to(c, val);
		return;
	}
	if (h->kind == HV_MAILBTN) {
		/* several addresses: which one */
		static char lab[16][110];
		static const char *its[16];
		short m;
		for (m = 0; m < c->nemail && m < 16; m++) {
			snprintf(lab[m], sizeof(lab[m]), "  %-8s %s", c->email[m].label, c->email[m].value);
			its[m] = lab[m];
		}
		k = win_popup(mx, my, its, m);
		if (k >= 0)
			cmd_mail_to(c, c->email[k].value);
		return;
	}
	if (mail)
		items[n++] = "  Send Email";
	items[n++] = "  Copy";
	k = win_popup(mx, my, items, n);
	if (k < 0)
		return;
	if (mail && k == 0)
		cmd_mail_to(c, val);
	else
		cmd_copy(val);
}

int card_click(short mx, short my, short clicks, short button)
{
	CONTACT *c = sel_contact();
	short i;
	layout_bar();
	if (rect_hit(&b_new, mx, my)) {
		cmd_new_contact();
		return 1;
	}
	if (rect_hit(&b_edit, mx, my)) {
		if (c)
			editor_open(c);
		return 1;
	}
	if (!c)
		return 1;
	for (i = 0; i < nhits; i++)
		if (rect_hit(&hits[i].r, mx, my)) {
			if (hits[i].kind == HV_EMAIL && clicks == 2 && !(button & 2))
				cmd_mail_to(c, c->email[hits[i].idx].value);
			else
				value_menu(mx, my, c, &hits[i]);
			return 1;
		}
	if (button & 2)
		contact_menu(mx, my, c);
	else if (clicks == 2)
		editor_open(c);
	return 1;
}
