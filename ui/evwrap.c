/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * evwrap.c - the accessory lets go of its files before every AES call
 * that may let another program run (the guidelines, 2.2: no file open
 * across an event call). An accessory's files belong to the program in
 * front; if that program ends while we wait, TOS closes them and gives
 * their numbers to the next files opened, which would take our writes.
 *
 * The link wraps the calls (-Wl,--wrap=evnt_multi_,..., the Makefile), so
 * every one is covered: the shared code's (the busy bee, alerts, the
 * progress box) and EContact's own. pf_park (src/pfkeep.c) closes every
 * file; each is opened again where it was at its next read or write. The
 * program owns its files and keeps them.
 */
#include "gem.h"
#include "ui.h"
#include "../src/plat.h"
#include "conn.h"

short __real_evnt_multi_(short flags, short clicks, short mask, short state,
			 unsigned long timer, short *msg, AEVENT *ev);
short __real_evnt_timer_(unsigned long ms);
short __real_form_do(OBJECT *tree, short start);
short __real_form_alert(short def, const char *str);
short __real_fsel_exinput(char *path, char *name, short *button, const char *title);

/* the accessory's files closed: pfkeep.c's, and the log's (common's
   conn.c keeps it open a second after a line, guidelines 6.5; gem.c
   closes it before an evnt_ call, form_do, form_alert and the file
   selector wait for the user as well) */
static void acc_park(void)
{
	pf_park();
	conn_log_flush();
}

short __wrap_evnt_multi_(short flags, short clicks, short mask, short state,
			 unsigned long timer, short *msg, AEVENT *ev)
{
	if (is_acc)
		pf_park();
	return __real_evnt_multi_(flags, clicks, mask, state, timer, msg, ev);
}

short __wrap_evnt_timer_(unsigned long ms)
{
	if (is_acc)
		pf_park();
	return __real_evnt_timer_(ms);
}

short __wrap_form_do(OBJECT *tree, short start)
{
	if (is_acc)
		acc_park();
	return __real_form_do(tree, start);
}

short __wrap_form_alert(short def, const char *str)
{
	if (is_acc)
		acc_park();
	return __real_form_alert(def, str);
}

short __wrap_fsel_exinput(char *path, char *name, short *button, const char *title)
{
	if (is_acc)
		acc_park();
	return __real_fsel_exinput(path, name, button, title);
}

/* Placement (AERA-Atari 098183d): win.o to help.o, the window's drawing,
   the list, the card and the dialogs, where 2.7 had them modulo 256 (the
   68030's caches): the shared code of 17 Nov 2026 moved them by 208, and
   a row let go drew its card 8 ms slower on a stock Falcon (Falcon-4:16,
   three pairs). Appended to the last of this file's sections the link
   keeps; change it with the code before win.o */
__asm__(".section .text.__wrap_fsel_exinput,\"ax\",@progbits\n\t.skip 48\n\t.previous");
