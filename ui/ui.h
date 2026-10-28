/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - EContact's GEM interface: one window like the Mac's Contacts,
 * the groups on the left, the people in the middle under a search field,
 * the chosen one's card on the right; dialogs drawn by field.c so that
 * every text field takes Hebrew.
 */
#ifndef UI_H
#define UI_H

#include "tos.h"
#include "gem.h"
#include "../src/abook.h"

typedef struct { short x, y, w, h; } GRECT;

/* screen and font */
extern short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
extern short gl_wchar, gl_hchar;
extern short ap_id;
extern short is_acc;		/* started as a desk accessory */
extern short ncolors;		/* 2, 4, 16, 256 */

/* ---- drawing (draw.c) ---- */
#define TX_BOLD     1
#define TX_INVERSE  2	/* white text */
#define TX_RIGHT    4	/* right-to-left text against the right edge */
#define TX_LIGHT    8	/* drawn as normal text now: grey was hard to read */
#define TX_LTR     16	/* left-to-right even when it starts in Hebrew */
#define TX_CENTER  32
#define TX_UNDER   64
#define TX_DIM    128	/* what can't be used (disabled controls): light grey, else dotted */
#define TX_GREY   256	/* secondary text: labels, hints (dark grey) */
#define TX_PALE   512	/* what recedes (light grey) */
#define TX_WHITE 1024	/* on a white ground (pen 0): bold may be drawn faster (draw.c) */
void fill(GRECT *r, short color);
void set_wrmode(short mode);
void fill_pat(GRECT *r, short color, short interior, short style);
void frame(GRECT *r, short color);
void xor_frame(GRECT *r);
void clip_on(GRECT *r);
void clip_off(void);
int clip_rect(GRECT *r);
void text_at(short x, short y, const char *s, long n, short cols, short flags);
void text_big(short cx, short cy, const char *s, short n, short h, short flags);
void text_color(short color);	/* for the next text_at; 1 = black, reset after */
void hline(short x1, short x2, short y);
void vline(short x, short y1, short y2);
void line_col(short x1, short y1, short x2, short y2, short color);
long wrap_text(const char *s, long n, short width, long *breaks, long max);
short line_rtl(const char *s, long n);
void heb_font_map(char *s, long n, short where);
int  rect_hit(const GRECT *r, short x, short y);
int  rect_and(GRECT *a, const GRECT *b);	/* a = a & b; 0 if empty */
void st_ram_init(void);
#define MARK_LOOP 0
#define MARK_BELL 1
short mark_w(void);
void draw_mark(short x, short y, short size, int which, short color, const GRECT *clip);
void *st_ram(long n);
void disc(short cx, short cy, short rx, short ry, short color);	/* a filled ellipse */
/* a contact's picture (PHOTO_N square, RGB 5-6-5) in an ellipse; serial
   changes when the picture does (the dithering is kept until then) */
void draw_photo(const unsigned short *px, int serial, short cx, short cy, short rx, short ry, const GRECT *clip);
int photo_dither(const unsigned short *px, int serial, short rx, short ry, int (*more)(void));

#define NCOLORS 12	/* red orange yellow green blue purple brown grey pink teal navy dark green */
typedef struct {
	short color;
	short interior, style;
} PEN;
void pen_color(short k, PEN *p);	/* colour k of the twelve */
#include "popup.h"		/* popup(), popup_area: common/ui */
short win_popup(short x, short y, const char *const *labels, short n);
short red_color(void);
short grey_color(void);		/* lines (the light grey) */
short text_grey(int pale);	/* the dark (0) or light (1) grey for text */
short sel_color(void);		/* the selected row: the Mac's blue, black without colours */

/* ---- the window (win.c) ---- */
extern short win_h;		/* AES handle, -1 when closed */
extern short win_iconified;	/* shown as the icon */
extern short win_hold;		/* nothing drawn: the AES asks for all of it soon */
extern short picking;		/* choosing people for EMail or ECal (pick:) */
void pick_end(int choose);	/* Choose (1) or Cancel (0) */
void win_search_set(void);	/* search_text was set: the list again */
void icon_window(GRECT *area);	/* the iconified window: icon and how many */
extern GRECT work;		/* its work area */
void win_open(void);
void win_close(void);
void win_redraw(GRECT *area);	/* NULL: everything */
void win_redraw_later(const GRECT *area);	/* in bands from the event loop, copied when all drawn */
void win_redraw_bands(void);			/* all of it, a line a band, each copied when drawn */
void win_open_bands(void);			/* the accessory's window as it opens: the same */
int win_later_pending(void);
void win_later_slice(int (*more)(void));	/* bands while more() says so */
void win_later_finish(void);
void win_search_step(int (*more)(void));	/* a slice of a search's matching */
void win_buffer_init(long free);	/* at start-up: the buffer the window is drawn in */
void win_buffer_end(void);	/* the program's end */
void win_buffer_fonts(void);	/* GDOS fonts loaded later: on the bitmap too */
const char *win_drawing(void);	/* how the window is drawn, for the log */
void win_scroll(const GRECT *a, short dy);	/* a's content up by dy pixels */
/* the panes' bars drawn again where a is, on the screen (5.1) */
void win_bars(const GRECT *a);
/* the window's slider set from the card (5.1) */
void win_slider(void);
void draw_forget(void);		/* the VDI attributes set again at the next use */
void win_message(short *msg);
void win_status(const char *s);	/* the info line */
void win_frame_refresh(void);
int  win_instant(short mx, short my);
int  win_mouse(short mx, short my, short button, short clicks, short kstate);
int  win_key(short kstate, short key);
void win_layout(void);		/* after a size or font change */
void win_title(void);
void win_search_draw(void);	/* the search field again */

/* layout: filled in by win_layout */
extern GRECT r_tool, r_side, r_list, r_card, r_search;

/* ---- what is shown ---- */
enum { SRC_ALL, SRC_BOOK, SRC_GROUP };
extern short src_kind, src_idx;	/* the sidebar's choice: all, a book, a group */
extern char search_text[64];	/* the search field */
extern char sel_uid[80];	/* the chosen contact */
extern short sel_book;
CONTACT *sel_contact(void);	/* NULL: none */
void select_contact(CONTACT *c);	/* and show it */

/* ---- the list (list.c) ---- */
extern CONTACT **listed;		/* the people the list shows */
extern int nlisted;
void list_invalidate(void);	/* people, the source or the search changed */
void list_search_changed(void);	/* the search only: typing on narrows (list.c) */
int list_search_pending(void);	/* its matching under way, a slice a turn */
int list_search_step(int (*more)(void));	/* 1: done */
void list_source(short kind, short idx);	/* the sidebar's choice */
void list_fix_selection(void);	/* the first one, if the chosen one isn't shown */
/* several chosen (Shift+click one more, Control+click a range, ^A all: as
   EFTP's and EMail's lists): a bit of each CONTACT's mark (the pick's
   ticks are another) */
#define MK_TICK 1		/* ticked while choosing for EMail or ECal */
#define MK_SEL 2		/* one of several chosen */
int list_chosen(void);		/* how many are chosen: 0, 1 or more */
void list_select_all(void);	/* ^A */
CONTACT **list_chosen_contacts(int *n);	/* in the list's order; free() it */
/* their UIDs and books, to find each again as every change reads the
   list again (the CONTACTs go): one block, free() it; *skipped: those
   without a UID */
char **list_chosen_uids(int *n, short **book, int *skipped);
void list_draw(GRECT *clip);
int  list_click(short mx, short my, short clicks, short button, short kstate);
int  list_key(short kstate, short key);
void list_step(int dir);	/* the next / previous one */
void list_scroll_to_sel(void);
int list_scroll(int rows);	/* the list's scroll bar, the wheel: 0 at an end */
int  list_bar_hit(short mx, short my);
/* the list's bar, on the screen, within clip (after the copy) */
void list_bar_draw(const GRECT *clip);
short list_band(short y, short h);	/* a band of it to a row's end */
void list_bar_area(GRECT *r);
void contact_menu(short mx, short my, CONTACT *c);	/* right-click on a contact */

/* ---- the card (card.c) ---- */
void card_draw(GRECT *clip);
int  card_click(short mx, short my, short clicks, short button);
int  card_instant(short mx, short my);
/* the card in the window's slider (5.1): its height, the part shown and
   how far down it is, in pixels; WM_ARROWED and WM_VSLID */
void card_scroll_info(short *total, short *shown, short *top);
int  card_scroll_to(long t);
void card_arrowed(short how);
void card_slid(short pos);
/* the chosen contact's photo is read a moment after the choice (arrow
   keys going down the list don't wait for each one) */
int  card_photo_pending(void);	/* one is waiting */
int  card_photo_dithering(void);	/* one got, its pens made a slice a turn */
void card_avatar_area(GRECT *r);	/* the round picture, as last drawn */
void card_photo_tick(void);	/* from the event loop: reads it when it's time */
void card_photo_recheck(void);	/* after a refresh: photos may have changed */

/* ---- the sidebar (sidebar.c) ---- */
void side_draw(GRECT *clip);
int  side_click(short mx, short my, short clicks, short button);

/* ---- dialogs drawn by us (field.c) ---- */
enum { FT_LABEL, FT_TEXT, FT_PASS, FT_BUTTON, FT_CHECK, FT_POPUP, FT_MULTI, FT_HEAD, FT_COLOR };
#define FF_DEFAULT  1
#define FF_CANCEL   2
#define FF_DISABLED 4
#define FF_LTR      8
#define FF_DANGER  16
#define FF_SHOWN   32
#define FF_DIGITS  64
typedef struct {
	short type, flags;
	short x, y, w, h;	/* in characters */
	const char *label;
	char *buf;
	short max;
	short val;
	const char *const *items;
	short nitems;
	short cur, first;
	short page;
} FIELD;
typedef struct DLG DLG;
struct DLG {
	const char *title;
	short w, h;
	FIELD *f;
	short n;
	short focus;
	void (*changed)(DLG *d, short i);
	void *ud;
	const char *const *pages;
	short npages, page, side;
};
short dlg_run(DLG *d);
/* 1 when dialogs are drawn in the 3D look (5.6) */
int dlg_look3d(void);
void  dlg_redraw_field(DLG *d, short i);
void  dlg_redraw(DLG *d);
extern void (*dlg_after)(void);
#include "alert.h"		/* alert(): common/ui */
short dlg_pick(const char *title, const char *const *items, short n, short sel);
/* the dialogs in a window where the AES runs several programs (3.5) */
void dlg_window_init(void);
void menu_drop(const short *msg);
int   dlg_ask(const char *title, const char *label, char *buf, short len);
/* a box for long work: a line of what is done, a bar, a second line (the
   time left) and Cancel. prog_update returns 0 once Cancel was chosen */
void  prog_open(const char *title);
int   prog_update(long done, long total, const char *line1, const char *line2);
void  prog_close(void);

/* ---- the dialogs (dialogs.c, editor.c) ---- */
int  editor_open(CONTACT *c);		/* 1 if saved */
void editor_new(void);
void dlg_add_account(void);
int  dlg_account(int a);		/* 1 changed, -1 deleted */
void dlg_settings(void);
void dlg_connection(void);
void dlg_about(void);
void dlg_keys(void);
void win_find(void);			/* ^F: a new search */
void font_apply(void);
void font_boot(short id);	/* the accessory, before its first event call */
void font_menu(void);
void font_reselect(void);
int font_gdos_loaded(void);	/* GDOS fonts on the window's workstation */
int font_is_system(void);	/* the text font is the system font */
#define HM_MAP     0
#define HM_UNICODE 1
#define HM_SYSTEM  2
extern short heb_mode;
extern unsigned short heb_idx[27];
void font_sys_push(void);
short font_sys_out(void);
void font_sys_back(short d);
void font_sys_pop(void);

/* ---- main.c ---- */
void status(const char *msg);
void status_updated(void);
void busy(int on);
void cmd_refresh(int force);
void cmd_menu(short x, short y);
void cmd_delete_selected(void);
void cmd_new_contact(void);
void cmd_import(int book);
void cmd_export(void);
void cmd_sync_photos(void);
void cmd_new_group(int book);
void cmd_mail(const char *addr);	/* a message to addr in EMail */
void cmd_mail_to(const CONTACT *c, const char *email);	/* to "Name <email>" */
void cmd_copy(const char *text);	/* to the GEM clipboard */
void cmd_copy_contact(const CONTACT *c);	/* "Name <email>" to it */
void cmd_copy_chosen(void);	/* the one chosen, or several: "A <a>, B <b>" */
void cmd_mail_chosen(void);	/* a new message to the chosen ones */
void after_change(void);		/* contacts changed: the list and the window again */
extern short hebrew_kbd;
void set_hebrew_kbd(short on);
void settings_later(void);
void settings_flush(void);
void closing_down(int quitting);
unsigned char key_char(short kstate, short key);
void falcon_dsp_init(void);
const char *falcon_dsp_state(void);
/* the curves of a Direct connection's handshakes on the DSP (in TLS.OVL:
   common/ui/dsp_ovl.c, the guidelines 7.4) */
void falcon_dsp_ec_init(void);
const char *falcon_dsp_ec_state(void);
int  falcon_mode_set(int on);
extern short quit_now;
void set_logging(void);

#define K_ALT 0x08
#define KEY_SCAN(k)  (((unsigned short)(k)) >> 8)
#define KEY_ASCII(k) ((unsigned char)((k) & 0xff))

#endif
