/*
 * editor.c - a contact's card to fill in or change. The Mac edits the
 * card where it stands; an Atari screen is shorter, so here the card's
 * parts are sections of one dialog (as in Settings): the name, the
 * phones, the emails, the addresses, and the rest. Each phone, email,
 * address and web page has its label in a pop-up: the Mac's ones
 * ("mobile", "iPhone", "home", "work"...) or one typed with Custom...
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"

enum { P_NAME = 1, P_PHONE, P_EMAIL, P_ADDR, P_MORE };
static const char *const pages[] = { "Name", "Phone", "Email", "Address", "More" };

#define SIDE 10			/* the sections' list */
#define CX (SIDE + 2)		/* where a section starts */
#define VX (CX + 13)		/* its values */
#define DW (VX + 31)		/* the dialog's width */
#define NADR 2			/* addresses in the dialog (a third one is kept) */

/* a label pop-up: the standard labels, the row's own if it is another,
   and Custom... */
typedef struct {
	const char *items[16];
	char own[24];		/* a label not in the list */
	char lab[16][28];
	short n, custom;	/* how many; the index of Custom... */
	char *dest;		/* the CEDIT label it fills */
} LPOP;

static CEDIT ed;
static LPOP lp[CE_TEL + CE_EMAIL + NADR + CE_URL];
static short nlp;
static short lp_field[CE_TEL + CE_EMAIL + NADR + CE_URL];	/* its FIELD index */

static void put(FIELD *f, short type, short flags, short x, short y, short w, const char *label, short page)
{
	memset(f, 0, sizeof(*f));
	f->type = type;
	f->flags = flags;
	f->x = x;
	f->y = y;
	f->w = w;
	f->h = 1;
	f->label = label;
	f->page = page;
}

static void text(FIELD *f, short flags, short x, short y, short w, char *buf, short max, short page)
{
	put(f, FT_TEXT, flags, x, y, w, 0, page);
	f->buf = buf;
	f->max = max;
}

/* the pop-up's items for label (std: the standard ones) */
static short lpop_init(LPOP *p, const char *const *std, char *label)
{
	short i, sel = 0, found = -1;
	p->n = 0;
	p->dest = label;
	for (i = 0; std[i] && p->n < 13; i++) {
		snprintf(p->lab[p->n], sizeof(p->lab[0]), "  %s", std[i]);
		if (!strcasecmp(std[i], label))
			found = p->n;
		p->items[p->n] = p->lab[p->n];
		p->n++;
	}
	if (found < 0 && label[0]) {
		str_copy(p->own, label, sizeof(p->own));
		snprintf(p->lab[p->n], sizeof(p->lab[0]), "  %s", p->own);
		p->items[p->n] = p->lab[p->n];
		found = p->n++;
	}
	sel = found >= 0 ? found : 0;
	p->custom = p->n;
	snprintf(p->lab[p->n], sizeof(p->lab[0]), "  Custom...");
	p->items[p->n] = p->lab[p->n];
	p->n++;
	return sel;
}

/* Custom... chosen: a label of one's own */
static void changed(DLG *d, short i)
{
	short k;
	for (k = 0; k < nlp; k++) {
		LPOP *p = &lp[k];
		FIELD *f = &d->f[lp_field[k]];
		char name[24] = "";
		if (lp_field[k] != i || f->val != p->custom)
			continue;
		if (dlg_ask("Custom Label", "Label:", name, 20) && name[0]) {
			/* it takes the place of the row's own label, or a new one before Custom... */
			if (p->own[0]) {
				str_copy(p->own, name, sizeof(p->own));
				snprintf(p->lab[p->custom - 1], sizeof(p->lab[0]), "  %s", name);
				f->val = p->custom - 1;
			} else if (p->n < 16) {
				str_copy(p->own, name, sizeof(p->own));
				snprintf(p->lab[p->custom], sizeof(p->lab[0]), "  %s", name);
				p->items[p->custom] = p->lab[p->custom];
				snprintf(p->lab[p->custom + 1], sizeof(p->lab[0]), "  Custom...");
				p->items[p->custom + 1] = p->lab[p->custom + 1];
				f->val = p->custom;
				p->custom++;
				p->n++;
				f->nitems = p->n;
			}
		} else {
			f->val = 0;
		}
		dlg_redraw(d);
	}
}

static void label_back(LPOP *p, short val)
{
	const char *s = val >= 0 && val < p->n && val != p->custom ? p->items[val] : p->items[0];
	while (*s == ' ')
		s++;
	str_copy(p->dest, s, 24);
}

/* the empty rows of a new contact get the Mac's labels */
static void default_labels(CEDIT *e)
{
	static const char *const tel[] = { "mobile", "home", "work", "main", "home fax", "other" };
	static const char *const em[] = { "home", "work", "other", "home", "work", "other" };
	static const char *const adr[] = { "home", "work", "other" };
	short i;
	for (i = 0; i < CE_TEL; i++)
		if (!e->tel[i].value[0])
			str_copy(e->tel[i].label, tel[i], sizeof(e->tel[i].label));
	for (i = 0; i < CE_EMAIL; i++)
		if (!e->email[i].value[0])
			str_copy(e->email[i].label, em[i], sizeof(e->email[i].label));
	for (i = 0; i < CE_ADR; i++)
		if (!e->adr[i].street[0] && !e->adr[i].city[0] && !e->adr[i].country[0])
			str_copy(e->adr[i].label, adr[i], sizeof(e->adr[i].label));
	for (i = 0; i < CE_URL; i++)
		if (!e->url[i].value[0])
			str_copy(e->url[i].label, "homepage", sizeof(e->url[i].label));
}

/* the dialog; returns 1 saved, 0 cancelled, -1 delete asked for */
static int run_editor(CONTACT *c, int *book)
{
	enum { MAXF = 120 };
	static FIELD f[MAXF];
	static char book_lab[MAXBOOK][70];
	static const char *book_items[MAXBOOK];
	short book_idx[MAXBOOK], nb = 0, fbook = -1;
	short n = 0, i, row, r, fcompany, bdel, bcan, bok;
	DLG d;
	nlp = 0;
	/* Name */
	row = 3;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "First:", P_NAME);
	text(&f[n++], 0, VX, row, 30, ed.given, sizeof(ed.given), P_NAME);
	row++;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Last:", P_NAME);
	text(&f[n++], 0, VX, row, 30, ed.family, sizeof(ed.family), P_NAME);
	row++;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Middle:", P_NAME);
	text(&f[n++], 0, VX, row, 14, ed.middle, sizeof(ed.middle), P_NAME);
	put(&f[n++], FT_LABEL, 0, VX + 15, row, 0, "Nick:", P_NAME);
	text(&f[n++], 0, VX + 21, row, 9, ed.nick, sizeof(ed.nick), P_NAME);
	row++;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Prefix:", P_NAME);
	text(&f[n++], 0, VX, row, 8, ed.prefix, sizeof(ed.prefix), P_NAME);
	put(&f[n++], FT_LABEL, 0, VX + 15, row, 0, "Suffix:", P_NAME);
	text(&f[n++], 0, VX + 23, row, 7, ed.suffix, sizeof(ed.suffix), P_NAME);
	row++;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Company:", P_NAME);
	text(&f[n++], 0, VX, row, 30, ed.org, sizeof(ed.org), P_NAME);
	row++;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Department:", P_NAME);
	text(&f[n++], 0, VX, row, 30, ed.dept, sizeof(ed.dept), P_NAME);
	row++;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Job title:", P_NAME);
	text(&f[n++], 0, VX, row, 30, ed.title, sizeof(ed.title), P_NAME);
	row++;
	fcompany = n;
	put(&f[n++], FT_CHECK, 0, VX, row, 24, "Show as company", P_NAME);
	f[fcompany].val = ed.company;

	/* Phone */
	for (i = 0; i < CE_TEL; i++) {
		LPOP *p = &lp[nlp];
		lp_field[nlp] = n;
		put(&f[n], FT_POPUP, 0, CX, 3 + i, 12, 0, P_PHONE);
		f[n].val = lpop_init(p, vc_tel_labels, ed.tel[i].label);
		f[n].items = p->items;
		f[n].nitems = p->n;
		n++;
		nlp++;
		text(&f[n++], FF_LTR, VX, 3 + i, 30, ed.tel[i].value, 40, P_PHONE);
	}
	/* Email */
	for (i = 0; i < CE_EMAIL; i++) {
		LPOP *p = &lp[nlp];
		lp_field[nlp] = n;
		put(&f[n], FT_POPUP, 0, CX, 3 + i, 12, 0, P_EMAIL);
		f[n].val = lpop_init(p, vc_email_labels, ed.email[i].label);
		f[n].items = p->items;
		f[n].nitems = p->n;
		n++;
		nlp++;
		text(&f[n++], FF_LTR, VX, 3 + i, 30, ed.email[i].value, sizeof(ed.email[i].value), P_EMAIL);
	}
	/* Address */
	for (i = 0; i < NADR; i++) {
		LPOP *p = &lp[nlp];
		short y = 3 + i * 4;
		lp_field[nlp] = n;
		put(&f[n], FT_POPUP, 0, CX, y, 12, 0, P_ADDR);
		f[n].val = lpop_init(p, vc_adr_labels, ed.adr[i].label);
		f[n].items = p->items;
		f[n].nitems = p->n;
		n++;
		nlp++;
		text(&f[n++], 0, VX, y, 30, ed.adr[i].street, sizeof(ed.adr[i].street), P_ADDR);
		put(&f[n++], FT_LABEL, 0, CX + 1, y + 1, 0, "Zip, city", P_ADDR);
		text(&f[n++], FF_LTR, VX, y + 1, 8, ed.adr[i].zip, sizeof(ed.adr[i].zip), P_ADDR);
		text(&f[n++], 0, VX + 9, y + 1, 21, ed.adr[i].city, sizeof(ed.adr[i].city), P_ADDR);
		put(&f[n++], FT_LABEL, 0, CX + 1, y + 2, 0, "Region", P_ADDR);
		text(&f[n++], 0, VX, y + 2, 12, ed.adr[i].region, sizeof(ed.adr[i].region), P_ADDR);
		text(&f[n++], 0, VX + 13, y + 2, 17, ed.adr[i].country, sizeof(ed.adr[i].country), P_ADDR);
	}
	/* More */
	row = 3;
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Birthday:", P_MORE);
	text(&f[n], FF_LTR, VX, row, 11, ed.bday, sizeof(ed.bday), P_MORE);
	n++;
	put(&f[n++], FT_LABEL, FF_LTR, VX + 12, row, 0, "day.month.year", P_MORE);
	row++;
	for (i = 0; i < CE_URL; i++, row++) {
		LPOP *p = &lp[nlp];
		lp_field[nlp] = n;
		put(&f[n], FT_POPUP, 0, CX, row, 12, 0, P_MORE);
		f[n].val = lpop_init(p, vc_url_labels, ed.url[i].label);
		f[n].items = p->items;
		f[n].nitems = p->n;
		n++;
		nlp++;
		text(&f[n++], FF_LTR, VX, row, 30, ed.url[i].value, sizeof(ed.url[i].value), P_MORE);
	}
	put(&f[n++], FT_LABEL, 0, CX, row, 0, "Note:", P_MORE);
	put(&f[n], FT_MULTI, 0, VX, row, 30, 0, P_MORE);
	f[n].h = 3;
	f[n].buf = ed.note;
	f[n].max = sizeof(ed.note);
	n++;
	row += 3;
	/* a new contact: which address book, when there is a choice */
	if (!c) {
		for (i = 0; i < nbooks && nb < MAXBOOK; i++)
			if (book_writable(&books[i])) {
				snprintf(book_lab[nb], sizeof(book_lab[nb]), "  %s (%s)", books[i].name, accts[books[i].acct].name);
				book_items[nb] = book_lab[nb];
				book_idx[nb++] = i;
			}
		if (nb > 1) {
			put(&f[n++], FT_LABEL, 0, CX, row, 0, "Keep it in:", P_MORE);
			fbook = n;
			put(&f[n], FT_POPUP, 0, VX, row, 30, 0, P_MORE);
			f[n].items = book_items;
			f[n].nitems = nb;
			for (i = 0; i < nb; i++)
				if (book_idx[i] == *book)
					f[n].val = i;
			n++;
		}
	}
	/* on every section */
	bdel = n;
	put(&f[n++], FT_BUTTON, c ? FF_DANGER : FF_DISABLED, 0, 11, 9, "Delete", 0);
	bcan = n;
	put(&f[n++], FT_BUTTON, FF_CANCEL, DW - 22, 11, 10, "Cancel", 0);
	bok = n;
	put(&f[n++], FT_BUTTON, FF_DEFAULT, DW - 10, 11, 10, "Done", 0);
	if (c && !book_writable(&books[c->book]))
		f[bdel].flags = FF_DISABLED;

	memset(&d, 0, sizeof(d));
	d.title = c ? "Edit Contact" : "New Contact";
	d.f = f;
	d.n = n;
	d.w = DW;
	d.h = 12;
	d.focus = 1;
	d.pages = pages;
	d.npages = 5;
	d.side = SIDE;
	d.page = P_NAME;
	d.changed = changed;
	r = dlg_run(&d);
	if (r == bdel)
		return -1;
	if (r != bok)
		return 0;
	(void)bcan;
	for (i = 0; i < nlp; i++)
		label_back(&lp[i], f[lp_field[i]].val);
	ed.company = f[fcompany].val;
	if (fbook >= 0)
		*book = book_idx[f[fbook].val];
	return 1;
}

static int edit(CONTACT *c)
{
	int book = c ? c->book : book_for_new(), r, writable;
	char uid[80], gname[64];
	short gbook = -1;
	CONTACT *saved;
	if (book < 0) {
		alert(1, "[1][There's no address book to keep|contacts in yet: add an account|first (the menu, top right).][ OK ]");
		return 0;
	}
	writable = book_writable(&books[book]);
	vcard_to_edit(c, &ed);
	default_labels(&ed);
	str_copy(uid, c && c->uid ? c->uid : "", sizeof(uid));
	/* a new contact made while a group is shown goes into the group, as on the Mac */
	gname[0] = 0;
	if (!c && src_kind == SRC_GROUP && src_idx < ngroups) {
		str_copy(gname, groups[src_idx].name, sizeof(gname));
		gbook = groups[src_idx].book;
	}
	for (;;) {
		r = run_editor(c, &book);
		if (r == 0)
			return 0;
		if (r < 0) {
			cmd_delete_selected();
			return 1;
		}
		if (!writable) {
			alert(1, "[1][This address book is read-only:|the changes can't be saved.][ OK ]");
			return 0;
		}
		busy(1);
		if (c)
			c = contact_by_uid(uid, c->book);	/* the list may have been read again */
		saved = contact_save(&ed, c, book);
		busy(0);
		if (saved)
			break;
		if (alert(1, "[1][Couldn't save it:|%s][Edit|Cancel]", ab_err) != 1) {
			after_change();
			return 0;
		}
	}
	str_copy(uid, saved->uid ? saved->uid : "", sizeof(uid));
	if (gname[0] && gbook == book) {
		int i;
		busy(1);
		for (i = 0; i < ngroups; i++)
			if (groups[i].book == gbook && !strcmp(groups[i].name, gname)) {
				group_add(&groups[i], saved);
				break;
			}
		busy(0);
		saved = contact_by_uid(uid, book);
	}
	after_change();
	if (saved)
		select_contact(saved);
	return 1;
}

int editor_open(CONTACT *c)
{
	if (!c)
		return 0;
	return edit(c);
}

void editor_new(void)
{
	edit(0);
}
