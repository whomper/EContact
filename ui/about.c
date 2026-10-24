/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EContact's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c), with EContact's status line.
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
#include "tlsovl.h"

void dlg_about(void)
{
	static char conn[100];
	char direct[40];
	const char *ram = about_ram();
	ABOUT a = { "EContact", app_version, "Contacts for the Atari ST, TT and Falcon", { conn, 0, 0 }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	/* the TLS: TLS.OVL's version once it is loaded (the guidelines 7.4) */
	snprintf(direct, sizeof(direct), tls_ovl_loaded() ? "Direct connection (TLS.OVL %s)" : "Direct connection",
		 tls_ovl_version());
	snprintf(conn, sizeof(conn), "%s  \xF9  %s  \xF9  %s%s%s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 opt.falcon ? direct : opt.gate[0] ? "Gateway" : "No gateway",
		 opt.offline ? "offline" : "ready", ram[0] ? "  \xF9  " : "", ram);
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
