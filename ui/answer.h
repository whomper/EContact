/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * answer.h - EContact's side of PEOPLE (the guidelines, 4.1; answer.c)
 *
 *   answer_init();                      at start (the outbox: 4 KB)
 *   case AV_PEOPLE: answer_message(s, msg[1], data_ready);
 *   each turn:      answer_poll(data_ready);
 *   a pick (pick_new): the window's choose mode, then answer_picked()
 */
#ifndef ANSWER_H
#define ANSWER_H

#include "../src/abook.h"

void answer_init(void);
void answer_message(const char *s, short from, int ready);
void answer_poll(int ready);
int answer_busy(void);
void answer_picked(CONTACT *const *list, int n);	/* n = 0: cancelled */
void answer_picked_group(const GROUP *g);		/* its members with an address */
void answer_forget(void);				/* everything let go */

extern short pick_to;		/* the program a pick is for; -1: none */
extern char pick_asker[12];	/* its name */
extern char pick_search[64];	/* the text to search for */
extern int pick_new;		/* a pick came: the window shows it */

#endif
