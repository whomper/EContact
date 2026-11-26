/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cfrun.c - what tests/corecheck.c needs to run as ColdFire code under
 * qemu-m68k -cpu cfv4e (make core-cf), from ECal's: GEMDOS's memory calls
 * stood in for (common's atari/libc.c, the FireBee package's own, takes
 * its heap from them), printf and putchar into a buffer written to Linux's
 * stdout, the files of the list read whole through Linux (a FILE is the
 * file in memory), and the few platform calls the core makes.
 * common/tests/coldfire's rt.S starts it and gives the Linux calls; its
 * gemdos.c the signal's way in.
 */
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "rt.h"
#include "gemdos.h"
#include "plat.h"
#define CFRUN_SELF
#include "cfstdio.h"

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int cf_main(int argc, char **argv);

short _is_acc;
void *_BasePage;

/* the heap: one arena for libc.c */
static unsigned char arena[48L * 1024 * 1024] __attribute__((aligned(16)));

/* what is printed, written out when full and at the end */
static char obuf[64 * 1024];
static long olen;

static void flush(void)
{
	if (olen)
		sys_write(1, obuf, olen);
	olen = 0;
}

static void put(const char *s, long n)
{
	while (n > 0) {
		long k = (long)sizeof(obuf) - olen;
		if (k > n)
			k = n;
		memcpy(obuf + olen, s, (size_t)k);
		olen += k;
		s += k;
		n -= k;
		if (olen == (long)sizeof(obuf))
			flush();
	}
}

static void handler(int sig, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned long sp = uc->gregs[15], pc = uc->gregs[16];
	unsigned short fn = *(unsigned short *)sp;
	(void)sig;
	(void)info;
	if (*(unsigned short *)pc == 0x4e41)	/* (at the trap: past it) */
		pc += 2;
	else if (*(unsigned short *)(pc - 2) != 0x4e41) {
		flush();
		out("cfrun: SIGILL at ");
		out_hex(pc);
		out(", not a trap #1\n");
		sys_exit(3);
	}
	switch (fn) {
	case 0x48:			/* Malloc(-1): nothing more than the arena */
	case 0x44:			/* Mxalloc */
		uc->gregs[0] = 0;
		break;
	case 0x49:			/* Mfree */
		uc->gregs[0] = 0;
		break;
	case 0x4c:			/* Pterm(code) */
		flush();
		sys_exit(*(short *)(sp + 2));
	case 0x00:
		flush();
		sys_exit(0);
	default:
		flush();
		out("cfrun: GEMDOS ");
		out_hex(fn);
		out(" not stood in for\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc;
}

int cf_printf(const char *fmt, ...)
{
	static char b[8192];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	put(b, (long)strlen(b));
	return n;
}

int cf_putchar(int c)
{
	char ch = (char)c;
	put(&ch, 1);
	return c;
}

/* a file of the list: read whole */
struct cf_file {
	unsigned char *d;
	long n, at;
};

void *cf_fopen(const char *path, const char *mode)
{
	static struct cf_file f[4];
	static int next;
	long fd, n, cap = 4L * 1024 * 1024;
	struct cf_file *p = &f[next++ & 3];
	(void)mode;
	if ((fd = sys_open(path, 0, 0)) < 0)
		return 0;
	if (!(p->d = malloc((size_t)cap))) {
		sys_close(fd);
		return 0;
	}
	p->n = 0;
	while (p->n < cap && (n = sys_read(fd, p->d + p->n, cap - p->n)) > 0)
		p->n += n;
	sys_close(fd);
	p->at = 0;
	return p;
}

int cf_fseek(void *v, long off, int whence)
{
	struct cf_file *f = v;
	f->at = whence == SEEK_END ? f->n + off : whence == SEEK_CUR ? f->at + off : off;
	return 0;
}

long cf_ftell(void *v) { return ((struct cf_file *)v)->at; }

size_t cf_fread(void *buf, size_t a, size_t b, void *v)
{
	struct cf_file *f = v;
	long n = (long)(a * b);
	if (n > f->n - f->at)
		n = f->n - f->at;
	memcpy(buf, f->d + f->at, (size_t)n);
	f->at += n;
	return a ? (size_t)n / a : 0;
}

int cf_fclose(void *v)
{
	struct cf_file *f = v;
	free(f->d);
	f->d = 0;
	return 0;
}

void out(const char *s) { sys_write(1, s, (long)strlen(s)); }

void out_hex(unsigned long v)
{
	char b[11];
	int i;
	b[0] = '0';
	b[1] = 'x';
	for (i = 0; i < 8; i++)
		b[9 - i] = "0123456789abcdef"[(v >> (4 * i)) & 15];
	b[10] = 0;
	out(b);
}

/* common's libc.c acc_core (an accessory's memory, 9fd1335) names these: the
   ColdFire link has no tos.o; weak, so the ST's link takes tos.o's. corecheck
   is no accessory and has no cookie jar */
__attribute__((weak)) long get_cookie(long id)
{
	(void)id;
	return 0;
}

__attribute__((weak)) long tos_supexec(void *f)
{
	return ((long (*)(void))f)();
}

void heap_add(void *p, size_t bytes);

/* the list's path: corecheck's argument, from the environment's first
   string after the program's own (qemu passes argv on the stack, rt.S
   gives none to C): build/corecf/list, fixed */
int main(void)
{
	static char *argv[] = { "corecheck", "build/corecf/list", 0 };
	int r;
	sig_install(4, handler);	/* SIGILL: GEMDOS's trap #1 */
	heap_add(arena, sizeof(arena));
	r = cf_main(2, argv);
	flush();
	sys_exit(r);
}
