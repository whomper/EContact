/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * alertacc.c - a stand-in for another program's accessory in
 * tests/hatari/xaaes_dialogs.py: an accessory ("  Alert test" in the
 * Desk menu, Clients under XaAES) that puts up an alert when it is
 * chosen, so another client's alert comes over EContact's dialog. The
 * guidelines 1.2 (Erez, 8 Nov 2026): no other AERA program in EContact's
 * own tests; this one plays ECal's part, its first alert.
 */
#include "gem.h"

int main(void)
{
	short msg[8];
	AEVENT ev;
	short id = appl_init();
	menu_register(id, "  Alert test");
	for (;;) {
		evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (msg[0] == AC_OPEN)
			form_alert(1, "[1][Another program's alert,|over EContact's dialog.][Cancel]");
	}
}
