/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * memlaunch.c - starts tests/hatari/acc_room.py's probe as a user would: the
 * desktop starts it (Hatari's --auto), it calls the AES and waits 5 s, so
 * the AES runs the accessories to their first event call (their heap taken)
 * as it does before anyone double-clicks a program, then has the desktop
 * start C:\MEMPROBE.PRG once it has quit (shel_write). Built with
 * -DOPEN_ACC (build/memopen.prg) it first sends ECONTACT.ACC an AC_OPEN, as
 * its Desk entry does, and waits 15 s more (the rig answers the alert), so
 * the probe looks after the accessory has been opened.
 */
#include "tos.h"
#include "gem.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

int main(void)
{
	static char tail[2] = { 0, 0 };
	short ok, me = appl_init();
	evnt_timer_(5000);
#ifdef OPEN_ACC
	{
		short msg[8], id = appl_find("ECONTACT");
		msg[0] = AC_OPEN;
		msg[1] = me;
		msg[2] = 0;
		msg[4] = -1;		/* (any entry: the accessory takes -1 as its own) */
		if (id >= 0)
			appl_write(id, 16, msg);
		nf_debug(id >= 0 ? "MEMLAUNCH AC_OPEN sent" : "MEMLAUNCH no ECONTACT");
		evnt_timer_(15000);
	}
#endif
	(void)me;
	ok = shel_write(1, 1, 0, "C:\\MEMPROBE.PRG", tail);
	nf_debug(ok ? "MEMLAUNCH shel_write ok" : "MEMLAUNCH shel_write failed");
	appl_exit();
	return 0;
}
