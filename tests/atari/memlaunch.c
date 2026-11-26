/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * memlaunch.c - starts tests/hatari/acc_room.py's probe as a user would: the
 * desktop starts it (Hatari's --auto), it calls the AES and waits 5 s, so
 * the AES runs the accessories to their first event call (their heap taken)
 * as it does before anyone double-clicks a program, then has the desktop
 * start C:\MEMPROBE.PRG once it has quit (shel_write).
 */
#include "tos.h"
#include "gem.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

int main(void)
{
	static char tail[2] = { 0, 0 };
	short ok;
	appl_init();
	evnt_timer_(5000);
	ok = shel_write(1, 1, 0, "C:\\MEMPROBE.PRG", tail);
	nf_debug(ok ? "MEMLAUNCH shel_write ok" : "MEMLAUNCH shel_write failed");
	appl_exit();
	return 0;
}
