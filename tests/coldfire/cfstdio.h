/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* what tests/corecheck.c and tests/host/vcdump.c take from stdio, as
   ColdFire code under qemu (tests/coldfire/cfrun.c): included first with
   -include, so that their printf, putchar and files are cfrun's */
#ifndef CFSTDIO_H
#define CFSTDIO_H
#include <stddef.h>
int cf_printf(const char *fmt, ...);
int cf_putchar(int c);
void *cf_fopen(const char *path, const char *mode);
int cf_fseek(void *f, long off, int whence);
long cf_ftell(void *f);
size_t cf_fread(void *buf, size_t a, size_t b, void *f);
int cf_fclose(void *f);
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#ifndef CFRUN_SELF
#define printf cf_printf
#define putchar cf_putchar
#define fopen cf_fopen
#define fseek cf_fseek
#define ftell cf_ftell
#define fread cf_fread
#define fclose cf_fclose
#endif
#endif
