/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * about.c - EContact's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c), with EContact's status line.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "aboutbox.h"
#include "icon.h"
#include "icon16.h"
#include "../src/version.h"
#include "../src/plat.h"

void dlg_about(void)
{
	static char conn[100];
	const char *ram = about_ram();
	ABOUT a = { "EContact", app_version, "Contacts for the Atari ST, TT and Falcon", { conn, 0, 0 }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb };
	snprintf(conn, sizeof(conn), "%s  \xF9  %s  \xF9  %s%s%s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 opt.falcon ? (strcmp(falcon_dsp_state(), "DSP") ? "Falcon" : "Falcon/DSP") : opt.gate[0] ? "Gateway" : "No gateway",
		 opt.offline ? "offline" : "ready", ram[0] ? "  \xF9  " : "", ram);
	about_show(&a);
}
