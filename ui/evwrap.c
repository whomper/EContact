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

short __real_evnt_multi_(short flags, short clicks, short mask, short state,
			 unsigned long timer, short *msg, AEVENT *ev);
short __real_evnt_timer_(unsigned long ms);
short __real_form_do(OBJECT *tree, short start);
short __real_form_alert(short def, const char *str);
short __real_fsel_exinput(char *path, char *name, short *button, const char *title);

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
		pf_park();
	return __real_form_do(tree, start);
}

short __wrap_form_alert(short def, const char *str)
{
	if (is_acc)
		pf_park();
	return __real_form_alert(def, str);
}

short __wrap_fsel_exinput(char *path, char *name, short *button, const char *title)
{
	if (is_acc)
		pf_park();
	return __real_fsel_exinput(path, name, button, title);
}
