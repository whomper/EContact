/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * memprobe.c - the room TOS gives a program started after the boot
 * (tests/hatari/acc_room.py): its basepage's p_hitpa - p_lowtpa, the
 * whole block TOS handed it, and Malloc(-1) after crt0's Mshrink, to the
 * emulator's console as "MEMPROBE tpa=N free=N total=N" (total: every
 * free block, each taken in turn and given back). A program as big as
 * another AERA program needs this much and more to start beside
 * ECONTACT.ACC on a 1 MB ST (the stand-in for EFTP's own memprobe.prg;
 * no other AERA program runs in EContact's tests).
 */
#include <stdio.h>
#include "tos.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */
extern void *_BasePage;

int main(void)
{
	char line[80];
	long *bp = _BasePage;
	long tpa = bp[1] - bp[0];	/* p_hitpa - p_lowtpa */
	long free = (long)Malloc(-1), total = 0;
	void *got[64];
	int n = 0;
	while (n < 64) {
		long k = (long)Malloc(-1);
		if (k < 16 || (long)(got[n] = Malloc(k)) <= 0)
			break;
		total += k;
		n++;
	}
	while (n > 0)
		Mfree(got[--n]);
	snprintf(line, sizeof(line), "MEMPROBE tpa=%ld free=%ld total=%ld low=%08lx high=%08lx", tpa, free, total, (unsigned long)bp[0], (unsigned long)bp[1]);
	nf_debug(line);
	return 0;
}
