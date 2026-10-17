/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * answer.c - EContact's side of PEOPLE (the guidelines, 4.1): EMail and
 * ECal ask for the names behind addresses (names:), for people picked in
 * EContact's window (pick:), and are told when the people change
 * (changed:). The strings are common/ui/people_ask.c's; answers go out
 * one at a time from its outbox (people_poll).
 *
 * Answered from the contacts in memory alone: no file is read, no alert
 * raised, the mouse form left as it is. Before the accessory has read
 * its books (in the background, 2.2) the names are empty, and the askers
 * are told changed: when it has.
 */
#include <string.h>
#include <stdlib.h>
#include "gem.h"
#include "av.h"
#include "people_ask.h"
#include "../src/abook.h"
#include "../src/vcard.h"
#include "../src/plat.h"
#include "answer.h"

#define ASKERS 8

typedef struct {
	short id;
	char name[12];			/* as the AES knows it: "EMAIL", "ECAL" */
} ASKER;

static ASKER askers[ASKERS];
static int naskers;
static unsigned long told;		/* the names stamp they were last told */
static int told_any;
static unsigned long last_answer;	/* pf_ms: the outbox may still hold some */

short pick_to = -1;			/* who a pick is for; -1: none */
char pick_asker[12];
char pick_search[64];
int pick_new;				/* came since the window last looked */

static void keep_asker(short id, const char *name)
{
	int i;
	for (i = 0; i < naskers; i++)
		if (askers[i].id == id) {
			strncpy(askers[i].name, name, sizeof(askers[i].name) - 1);
			return;
		}
	if (naskers == ASKERS)
		memmove(askers, askers + 1, sizeof(ASKER) * --naskers);	/* the oldest goes */
	askers[naskers].id = id;
	memset(askers[naskers].name, 0, sizeof(askers[naskers].name));
	strncpy(askers[naskers].name, name, sizeof(askers[naskers].name) - 1);
	naskers++;
}

void answer_init(void)
{
	people_init(0, 4096L, 0, 0);
}

/* the answer to a pick: the people chosen (n = 0: cancelled) */
void answer_picked(CONTACT *const *list, int n)
{
	char name[PEOPLE_NAME];
	int i;
	if (pick_to < 0)
		return;
	people_answer(pick_to, "pick:", 0, 0);
	for (i = 0; i < n; i++) {
		const CONTACT *c = list[i];
		if (c->group || !c->nemail)
			continue;
		vcard_display_name(c, opt.last_first, name, sizeof(name));
		people_answer(pick_to, 0, c->email[0].value, name);
	}
	people_answer(pick_to, 0, 0, 0);
	pick_to = -1;
	pick_new = 0;
	last_answer = pf_ms();
}

/* a group chosen: its members with an address */
void answer_picked_group(const GROUP *g)
{
	CONTACT **l = malloc((npeople + 1) * sizeof(CONTACT *));
	int i, n = 0;
	for (i = 0; l && i < npeople; i++)
		if (!people[i]->group && people[i]->nemail && group_has(g, people[i]))
			l[n++] = people[i];
	answer_picked(l, n);
	free(l);
}

void answer_message(const char *s, short from, int ready)
{
	char asker[12], addr[PEOPLE_ADDR], name[PEOPLE_NAME];
	const char *rest, *p;
	int k = people_question(s, asker, sizeof(asker), &rest);
	if (k == PQ_NONE)
		return;
	keep_asker(from, asker);
	last_answer = pf_ms();
	if (k == PQ_NAMES) {
		people_answer(from, "names:", 0, 0);
		for (p = rest; (p = people_next(p, addr, sizeof(addr))) != 0;) {
			if (!ready || !people_name_for(addr, name, sizeof(name)))
				name[0] = 0;
			people_answer(from, 0, addr, name);
		}
		people_answer(from, 0, 0, 0);
		return;
	}
	/* pick: one at a time; a new one ends the last (cancelled) */
	if (pick_to >= 0 && pick_to != from)
		answer_picked(0, 0);
	pick_to = from;
	strncpy(pick_asker, asker, sizeof(pick_asker) - 1);
	pick_asker[sizeof(pick_asker) - 1] = 0;
	strncpy(pick_search, rest, sizeof(pick_search) - 1);
	pick_search[sizeof(pick_search) - 1] = 0;
	pick_new = 1;
}

/* each turn of the event loop: the outbox, and changed: when the names
   changed since the askers were last told (ready: the books are read) */
void answer_poll(int ready)
{
	people_poll();
	if (!ready || (people_names_stamp == told && told_any))
		return;
	if (told_any || naskers) {
		int i;
		for (i = 0; i < naskers;) {
			/* still the program that asked? (the AES's names are 8
			   characters, padded with spaces) */
			char pad[9];
			int k;
			for (k = 0; k < 8; k++)
				pad[k] = k < (int)strlen(askers[i].name) ? askers[i].name[k] : ' ';
			pad[8] = 0;
			if (appl_find(pad) != askers[i].id) {
				memmove(askers + i, askers + i + 1, sizeof(ASKER) * (--naskers - i));
				continue;
			}
			people_answer(askers[i].id, "changed:", 0, 0);
			people_answer(askers[i].id, 0, 0, 0);
			last_answer = pf_ms();
			i++;
		}
	}
	told = people_names_stamp;
	told_any = 1;
}

/* 1 while answers may still be waiting to go: the event loop's timer
   is short then */
int answer_busy(void)
{
	return pf_ms() - last_answer < 4000UL;
}

void answer_forget(void)
{
	naskers = 0;
	told_any = 0;
	pick_to = -1;
	pick_new = 0;
}
