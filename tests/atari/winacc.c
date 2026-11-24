/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * winacc.c - a stand-in for another program's accessory with a window of
 * its own, for tests/hatari/photo_pens.py: while the rig keeps WINACC.GO
 * in C:\ (looked for twice a second; ECONTACT.PRG has no Desk menu) it
 * has a small white window open at the desk's top left, which goes on
 * top of EContact's. EContact then
 * gives a photo's own colours back (pens 192-254, the guidelines 5.2) and
 * takes them again when its window is on top once more. The guidelines
 * 1.2: no other AERA program in EContact's own tests.
 */
#include "gem.h"
#include "tos.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

static short vdi = -1, wh = -1;

static void paint(short x, short y, short w, short h)
{
	short pxy[4], r[4];
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(wh, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	while (r[2] && r[3]) {
		short x1 = r[0] > x ? r[0] : x, y1 = r[1] > y ? r[1] : y;
		short x2 = r[0] + r[2] < x + w ? r[0] + r[2] : x + w;
		short y2 = r[1] + r[3] < y + h ? r[1] + r[3] : y + h;
		if (x2 > x1 && y2 > y1) {
			pxy[0] = x1;
			pxy[1] = y1;
			pxy[2] = x2 - 1;
			pxy[3] = y2 - 1;
			vs_clip(vdi, 1, pxy);
			vr_recfl(vdi, pxy);
		}
		wind_get(wh, WF_NEXTXYWH, &r[0], &r[1], &r[2], &r[3]);
	}
	vs_clip(vdi, 0, pxy);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

int main(void)
{
	short msg[8], out[57], d;
	AEVENT ev;
	short id = appl_init();
	menu_register(id, "  Window test");
	vdi = graf_handle(&d, &d, &d, &d);
	vdi = v_opnvwk_(vdi, out);
	vsf_color(vdi, 0);
	vsf_interior(vdi, 1);
	vswr_mode(vdi, 1);
	for (;;) {
		short go;
		if (!(evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 500, msg, &ev) & MU_MESAG)) {
			go = Fsfirst("C:\\WINACC.GO", 0) == 0;
			msg[0] = go && wh < 0 ? AC_OPEN : !go && wh >= 0 ? WM_CLOSED : 0;
			msg[3] = wh;
		}
		switch (msg[0]) {
		case AC_OPEN:
			if (wh < 0) {
				short x, y, w, h;
				wind_get(0, WF_WORKXYWH, &x, &y, &w, &h);
				wh = wind_create(NAME | CLOSER | MOVER, x, y, 240, 120);
				wind_set_str(wh, WF_NAME, " Window test ");
				wind_open(wh, x + 8, y + 8, 240, 120);
				nf_debug("WINACC open");
			} else
				wind_set(wh, WF_TOP, 0, 0, 0, 0);
			break;
		case AC_CLOSE:
			wh = -1;	/* (the AES took it) */
			break;
		case WM_REDRAW:
			if (msg[3] == wh)
				paint(msg[4], msg[5], msg[6], msg[7]);
			break;
		case WM_TOPPED:
			if (msg[3] == wh)
				wind_set(wh, WF_TOP, 0, 0, 0, 0);
			break;
		case WM_MOVED:
			if (msg[3] == wh)
				wind_set(wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		case WM_CLOSED:
			if (msg[3] == wh) {
				wind_close(wh);
				wind_delete(wh);
				wh = -1;
				nf_debug("WINACC closed");
			}
			break;
		}
	}
}
