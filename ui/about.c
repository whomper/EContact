/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EContact's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c), with its three status lines (aboutline.c).
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "aboutbox.h"
#include "iconify.h"
#include "../src/abook.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"
#include "../src/version.h"
#include "../src/plat.h"
#include "aboutline.h"
#include "../src/http.h"

void dlg_about(void)
{
	/* three status lines in the family's words (the guidelines 1.3;
	   Erez, 27 Oct 2026): the connection, the contacts, the memory */
	static char conn[64], held[64], mem[64];
	char host[64];
	int i, n = 0;
	ABOUT a = { "EContact", app_version, "Contacts for the Atari ST, TT and Falcon", { conn, held, mem },
		    { 0, 0, 0 }, icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	http_kept_host(host, sizeof(host));
	about_net_line(conn, sizeof(conn), net_stack(),
		       opt.falcon ? ABOUT_DIRECT : opt.gate[0] ? ABOUT_GATEWAY : ABOUT_NO_GATEWAY,
		       opt.offline ? ABOUT_OFFLINE : host[0] ? ABOUT_ONLINE : ABOUT_READY);
	for (i = 0; i < npeople; i++)
		n += !people[i]->group;
	snprintf(held, sizeof(held), "%d contact%s in %d address book%s", n, n == 1 ? "" : "s", nbooks,
		 nbooks == 1 ? "" : "s");
	about_mem_line(mem, sizeof(mem));
	about_show(&a);
}

/* The iconified window: the icon, and how many contacts there are under
   it when there is room (common/ui/iconify.c) */
void icon_window(GRECT *area)
{
	char s[12];
	int i, n = 0;
	for (i = 0; i < npeople; i++)
		n += !people[i]->group;
	snprintf(s, sizeof(s), "%d", n);
	icon_redraw(win_h, area, icon_bits, icon16_px, s);
}
