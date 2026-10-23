/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * palset.c - a palette loaded behind the VDI's back, for
 * tests/hatari/loaded_palette.py: as COLORVDI.CPX does on Erez's Falcon,
 * it sets the video hardware's colour registers 16-254 (VsetRGB), all
 * bright green, and leaves the VDI's own table as it was; the sixteen
 * system pens' registers and black's (255) stay. Then it runs
 * C:\ECONTACT\ECONTACT.PRG in its folder.
 */
#include "tos.h"

static long green[239];

static void vsetrgb(short index, short count, const long *rgb)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"
			 "move.w #93,-(%%sp)\n\ttrap #14\n\tlea 10(%%sp),%%sp"
		: "=r"(r) : "r"(index), "r"(count), "r"(rgb) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	(void)r;
}

static void dsetpath(const char *path)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %1,-(%%sp)\n\tmove.w #0x3b,-(%%sp)\n\ttrap #1\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(path) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	(void)r;
}

static long pexec(const char *name)
{
	static const char none[2] = { 0, 0 };
	register long r __asm__("d0");
	__asm__ volatile("clr.l -(%%sp)\n\tmove.l %2,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
			 "clr.w -(%%sp)\n\tmove.w #0x4b,-(%%sp)\n\ttrap #1\n\tlea 16(%%sp),%%sp"
		: "=r"(r) : "r"(name), "r"(none) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

int main(void)
{
	short i;
	for (i = 0; i < 239; i++)
		green[i] = 0x0000FF00L;
	vsetrgb(16, 239, green);
	dsetpath("\\ECONTACT");	/* the program's folder, as the desktop would */
	pexec("C:\\ECONTACT\\ECONTACT.PRG");
	return 0;
}
