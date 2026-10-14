/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * fakeguide.c - a stand-in for ST-Guide in tests/hatari/help_key.py: an
 * accessory that answers to the name ST-GUIDE (as ST-GUIDE.ACC) and
 * writes the file a VA_START names into C:\VASTART.TXT. ST-Guide reads
 * the name the same way, from the other program's memory.
 */
#include <string.h>
#include "gem.h"
#include "tos.h"

int main(void)
{
	short msg[8];
	AEVENT ev;
	appl_init();		/* no entry in the Desk menu: Contacts stays the first */
	for (;;) {
		evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (msg[0] == 0x4711) {
			const char *p = (const char *)((unsigned long)(unsigned short)msg[3] << 16 |
						       (unsigned short)msg[4]);
			long h = Fcreate("C:\\VASTART.TXT", 0);
			if (h >= 0) {
				Fwrite((short)h, strlen(p), p);
				Fclose((short)h);
			}
		}
	}
}
