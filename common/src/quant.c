/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * quant.c - a median cut of a picture's 5-6-5 pixels (quant.h).
 *
 * The pixels are copied once and each box sorted along its widest side by
 * counting (32 or 64 values), so no qsort and no 32-bit multiply per pixel:
 * a 64 x 64 photo to 64 colours is a few hundred thousand byte operations.
 */
#include <stdlib.h>
#include <string.h>
#include "quant.h"

#define R5(v) ((v) >> 11)
#define G6(v) (((v) >> 5) & 63)
#define B5(v) ((v) & 31)

typedef struct {
	long start, len;
	int side;			/* 0 R, 1 G, 2 B: the widest, scaled to 6 bits */
	int width;			/* its span on that side, 6 bits */
} BOX;

static int chan(unsigned short v, int side)
{
	return side == 0 ? R5(v) : side == 1 ? G6(v) : B5(v);
}

/* the box's widest side and its span (R and B doubled to G's 6 bits) */
static void measure(const unsigned short *p, BOX *b)
{
	int lo[3] = { 63, 63, 63 }, hi[3] = { 0, 0, 0 }, k, s;
	long i;
	for (i = 0; i < b->len; i++) {
		unsigned short v = p[b->start + i];
		int c[3];
		c[0] = R5(v) << 1;
		c[1] = G6(v);
		c[2] = B5(v) << 1;
		for (k = 0; k < 3; k++) {
			if (c[k] < lo[k])
				lo[k] = c[k];
			if (c[k] > hi[k])
				hi[k] = c[k];
		}
	}
	b->side = 1;
	b->width = hi[1] - lo[1];
	for (k = 0; k < 3; k += 2) {
		s = hi[k] - lo[k];
		if (s > b->width) {
			b->width = s;
			b->side = k;
		}
	}
}

/* the box's pixels sorted along its side, by counting; tmp holds len */
static void sort_box(unsigned short *p, unsigned short *tmp, const BOX *b)
{
	long count[64], i, at = 0;
	int k, m = b->side == 1 ? 64 : 32;
	unsigned short *q = p + b->start;
	for (k = 0; k < m; k++)
		count[k] = 0;
	for (i = 0; i < b->len; i++)
		count[chan(q[i], b->side)]++;
	for (k = 0; k < m; k++) {
		long c = count[k];
		count[k] = at;
		at += c;
	}
	for (i = 0; i < b->len; i++)
		tmp[count[chan(q[i], b->side)]++] = q[i];
	memcpy(q, tmp, (size_t) b->len * sizeof *q);
}

int quant_565(const unsigned short *px, long n, long skip, int want, short rgb[][3])
{
	unsigned short *p, *tmp;
	BOX *box;
	long i, m = 0;
	int nb = 1, k;

	if (want < 1 || n <= 0)
		return 0;
	if (want > 256)
		want = 256;
	p = malloc((size_t) n * sizeof *p);
	tmp = malloc((size_t) n * sizeof *tmp);
	box = malloc((size_t) want * sizeof *box);
	if (p == NULL || tmp == NULL || box == NULL) {
		free(p);
		free(tmp);
		free(box);
		return 0;
	}
	for (i = 0; i < n; i++)
		if ((long) px[i] != skip)
			p[m++] = px[i];
	if (m == 0) {
		nb = 0;
		goto out;
	}
	box[0].start = 0;
	box[0].len = m;
	measure(p, &box[0]);
	while (nb < want) {
		/* the box to cut: the most pixels times its width */
		int best = -1, c;
		long score = 0, half;
		BOX *b, *nw;
		for (k = 0; k < nb; k++) {
			long s = box[k].width > 0 && box[k].len > 1 ? box[k].len * box[k].width : 0;
			if (s > score) {
				score = s;
				best = k;
			}
		}
		if (best < 0)
			break;		/* every box one colour */
		b = &box[best];
		sort_box(p, tmp, b);
		/* the median, moved off a run of one value so both halves differ */
		half = b->len >> 1;
		c = chan(p[b->start + half], b->side);
		while (half > 0 && chan(p[b->start + half - 1], b->side) == c)
			half--;
		if (half == 0) {
			half = b->len >> 1;
			while (half < b->len && chan(p[b->start + half], b->side) == c)
				half++;
		}
		nw = &box[nb++];
		nw->start = b->start + half;
		nw->len = b->len - half;
		b->len = half;
		measure(p, b);
		measure(p, nw);
	}
	for (k = 0; k < nb; k++) {
		/* the box's mean, 8 bits a side, to VDI's 0-1000 */
		unsigned long s[3] = { 0, 0, 0 };
		long len = box[k].len, j;
		const unsigned short *q = p + box[k].start;
		for (j = 0; j < len; j++) {
			s[0] += R5(q[j]);
			s[1] += G6(q[j]);
			s[2] += B5(q[j]);
		}
		rgb[k][0] = (short) ((s[0] * 1000 + (unsigned long) len * 15) / ((unsigned long) len * 31));
		rgb[k][1] = (short) ((s[1] * 1000 + (unsigned long) len * 31) / ((unsigned long) len * 63));
		rgb[k][2] = (short) ((s[2] * 1000 + (unsigned long) len * 15) / ((unsigned long) len * 31));
	}
out:
	free(p);
	free(tmp);
	free(box);
	return nb;
}
