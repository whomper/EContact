/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * asker.c - a stand-in for EMail in tests/hatari/people_ask.py: a program
 * (ASKER.PRG) that asks EContact about people as EMail does, through
 * common/ui/people_ask.c, and says what came back on the emulator's
 * console (NatFeats) and in C:\ASKER.TXT:
 *   ASKER name <address> = "<name>"   each answer (empty: not known yet)
 *   ASKER changed                     EContact said its people changed
 *   ASKER picked <n>: <address> <name>...
 * It asks for one name at once, and when the name has come, picks
 * "Grace" in EContact's window; it ends when the pick is answered, or
 * after five minutes.
 */
#include <string.h>
#include <stdio.h>
#include "gem.h"
#include "tos.h"
#include "av.h"
#include "people_ask.h"

#define ADDR "grace.baker@example.net"

static int changed, picked_n = -1;
static char picked_text[512];

static void say(const char *s)
{
	long h;
	nf_debug(s);
	if ((h = Fopen("C:\\ASKER.TXT", 1)) < 0)
		h = Fcreate("C:\\ASKER.TXT", 0);
	else
		Fseek(0, (short)h, 2);
	if (h >= 0) {
		Fwrite((short)h, strlen(s), s);
		Fclose((short)h);
	}
}

static void names_came(void)
{
	changed++;
	say("ASKER changed\n");
}

static void picked(const PERSON *l, int n)
{
	int i;
	picked_n = n;
	snprintf(picked_text, sizeof(picked_text), "ASKER picked %d:", n);
	for (i = 0; i < n; i++)
		snprintf(picked_text + strlen(picked_text), sizeof(picked_text) - strlen(picked_text), " %s %s",
			 l[i].addr, l[i].name);
	strcat(picked_text, "\n");
	say(picked_text);
}

int main(void)
{
	short msg[8];
	AEVENT ev;
	const char *last = 0, *n;
	char line[160];
	int pick_sent = 0;
	unsigned long t0;
	appl_init();
	av_init("ASKER");
	people_init(16, 2048, names_came, picked);
	t0 = tos_ms();
	while (tos_ms() - t0 < 300000UL && picked_n < 0) {
		short which = evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 100, msg, &ev);
		const char *s;
		if ((which & MU_MESAG) && av_message(msg, &s) == AV_PEOPLE)
			people_message(s, msg[1]);
		people_poll();
		n = people_name(ADDR);
		if (n && n != last) {
			snprintf(line, sizeof(line), "ASKER name %s = \"%s\"\n", ADDR, n);
			say(line);
			last = n;
		}
		if (n && *n && !pick_sent && people_pick("Grace")) {
			pick_sent = 1;
			say("ASKER pick sent\n");
		}
		if (!n)
			last = 0;
	}
	say(picked_n < 0 ? "ASKER gave up\n" : "ASKER done\n");
	av_exit();
	appl_exit();
	return 0;
}
