/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vcbench.c - how long reading cards takes on the Atari it runs on
 * (VCBENCH.PRG, tests/hatari/vcbench.py): the cards of C:\VCBENCH.VCF,
 * each looked at for its photo (vcard_photo_span) and parsed as abook.c
 * does at start-up, timed with the MFP's Timer C (26 us steps). The
 * times go to the emulator's console (NatFeats):
 *   VCB cards <n> bytes <n>
 *   VCB span <us> us/card     parse <us> us/card     free <us> us/card
 *   VCB done
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "vcard.h"

/* (vcard.c's new UIDs, not used here) */
unsigned long pf_ms(void)
{
	return 0;
}

void pf_entropy(unsigned char *p, int n)
{
	memset(p, 7, n);
}

/* 38400 a second: _hz_200 and Timer C's count between its ticks
   (supervisor mode) */
static unsigned long fine(void)
{
	unsigned long a, b;
	unsigned char t;
	do {
		/* (in asm: GCC takes a pointer to a fixed address for an empty array) */
		__asm__ volatile("move.l 0x4ba.w,%0" : "=d"(a));	/* _hz_200 */
		__asm__ volatile("move.b 0xfffffa23.w,%0" : "=d"(t));	/* Timer C's data */
		__asm__ volatile("move.l 0x4ba.w,%0" : "=d"(b));
	} while (a != b);
	return a * 192 + (192 - t);
}

#define NC 2000
static long off[NC], len[NC];
static CONTACT *made[NC];
static char buf[600000];

int main(void)
{
	char line[100];
	long h, n, i, nc = 0, ssp;
	unsigned long t0, t1, t2, t3;
	ssp = trap1_wl(0x20, 0);	/* Super: the timers */
	h = Fopen("C:\\VCBENCH.VCF", 0);
	if (h < 0) {
		nf_debug("VCB no C:\\VCBENCH.VCF\n");
		trap1_wl(0x20, ssp);
		return 1;
	}
	n = Fread((short)h, sizeof(buf) - 1, buf);
	Fclose((short)h);
	buf[n > 0 ? n : 0] = 0;
	for (i = 0; i < n && nc < NC;) {
		char *e = strstr(buf + i, "END:VCARD");
		if (!e)
			break;
		off[nc] = i;
		len[nc] = (e - buf) + 11 - i;
		nc++;
		i = (e - buf) + 11;
	}
	if (!nc) {
		nf_debug("VCB no cards\n");
		trap1_wl(0x20, ssp);
		return 1;
	}
	t0 = fine();
	for (i = 0; i < nc; i++) {
		long s, e;
		vcard_photo_span(buf + off[i], len[i], &s, &e);
	}
	t1 = fine();
	for (i = 0; i < nc; i++)
		made[i] = vcard_parse(buf + off[i], len[i]);
	t2 = fine();
	for (i = 0; i < nc; i++)
		free(made[i]);
	t3 = fine();
	trap1_wl(0x20, ssp);
#define US(t) ((t) * 125 / 24 * 5)	/* 1e6 / 38400, in 32 bits */
	snprintf(line, sizeof(line), "VCB cards %ld bytes %ld\n", nc, n);
	nf_debug(line);
	snprintf(line, sizeof(line), "VCB span %lu us/card\n", US(t1 - t0) / nc);
	nf_debug(line);
	snprintf(line, sizeof(line), "VCB parse %lu us/card\n", US(t2 - t1) / nc);
	nf_debug(line);
	snprintf(line, sizeof(line), "VCB free %lu us/card\n", US(t3 - t2) / nc);
	nf_debug(line);
	nf_debug("VCB done\n");
	return 0;
}
