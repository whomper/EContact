/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat.h - what the calendar core needs from the machine: files, folders,
 * the clock, a millisecond timer and TCP. Implemented for TOS/GEM in
 * atari/ and for Linux in host/ (used by the tests and mail-cli).
 */
#ifndef PLAT_H
#define PLAT_H

#include <stddef.h>

#ifdef CAL_TOS
#define PF_SEP '\\'
#else
#define PF_SEP '/'
#endif

/* ---- files ---- */
#define PF_READ   0
#define PF_WRITE  1	/* create or truncate */
#define PF_APPEND 2
#define PF_UPDATE 3	/* an existing file, written over from the start, not truncated */

int  pf_open(const char *path, int mode);	/* handle >= 0, < 0 on error */
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_seek(int h, long off);			/* from the start; new position or < 0 */
long pf_size(const char *path);			/* -1 if missing */
unsigned long pf_stamp(const char *path);	/* when it was last written (date, time); 0 if missing */
/* both from one look at the folder (the guidelines, 6.5): the date as
   pf_stamp, the size in *size (-1 if missing) */
unsigned long pf_info(const char *path, long *size);
/* an open file's size, its position left at the start (common's
   pf_size_open: pf_load opens first and looks only when that fails) */
long pf_size_of(int h);
/* up to n bytes from off: opened, read and closed (tls_read_at) */
long pf_read_at(const char *path, long off, void *buf, long n);
int  pf_exists(const char *path);
int  pf_mkdir(const char *path);		/* 0 ok or already there */
int  pf_rmdir(const char *path);		/* an empty folder; 0 ok */
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);
/* While a sync runs, the files it opens again and again (the address
   book's cache, the log) are kept open instead (pfkeep.c): every open
   takes an entry in TOS's small internal pool, and a long Sync Contact
   Photos opened them hundreds of times. 1 on, 0 off (all closed) */
void pf_keep(int on);
/* every file closed now, those in use too: each opened again where it
   was at its next read, write or seek. The accessory calls it before
   every event call (ui/evwrap.c): its files belong to the program in
   front, whose end would close them and give their numbers to others
   (the guidelines, 2.2) */
void pf_park(void);
extern long pf_opens;		/* files opened (GEMDOS calls), for the log */
/* the machine's own calls under pf_open...pf_rename (atari/plat_tos.c,
   tests/host/plat_posix.c) */
int  os_open(const char *path, int mode);
long os_read(int h, void *buf, long n);
long os_write(int h, const void *buf, long n);
void os_bounce_init(void);	/* Atari: TT RAM through ST RAM when no _FRB (at boot) */
void os_close(int h);
long os_seek(int h, long off);
long os_tell(int h);
long os_seek_end(int h);
long os_size(const char *path);
unsigned long os_stamp(const char *path);
unsigned long os_info(const char *path, long *size);
int  os_exists(const char *path);
int  os_remove(const char *path);
int  os_rename(const char *from, const char *to);
/* call cb for every plain file in dir; stop early if cb returns non-zero */
int  pf_list(const char *dir, int (*cb)(const char *name, long size, void *ud), void *ud);

/* whole-file helpers: pf_load returns a malloc'ed NUL-terminated copy */
char *pf_load(const char *path, long *len);
extern long (*pf_size_open)(int h);	/* set: pf_load opens first (pf_size_of) */
int   pf_save(const char *path, const void *data, long len);

/* ---- time ---- */
typedef struct {
	short year, mon, day, hour, min, sec;
} PFTIME;

void pf_now(PFTIME *t);		/* local wall clock */
unsigned long pf_ms(void);	/* monotonic milliseconds (5 ms steps on TOS) */
void pf_debug(const char *s);	/* emulator console / stderr */
/* raw unpredictable bytes (timer jitter on the Atari, /dev/urandom on
   Linux); tls.c hashes them with a seed kept on disk */
void pf_entropy(unsigned char *buf, int n);

/* ---- TCP ---- */
int  net_init(void);			/* 1 if a TCP/IP stack is present */
const char *net_stack(void);		/* "STinG", "MiNTnet", "POSIX" ... */
/* connect (resolving names); handle >= 0, or < 0 with err filled in */
int  net_open(const char *host, unsigned short port, char *err, int errlen);
long net_write(int h, const void *buf, long n);	/* bytes sent or < 0 */
/* non-blocking: > 0 bytes read, 0 nothing yet, < 0 closed or failed */
long net_read(int h, void *buf, long max);
void net_close(int h);
/* close once what was sent has gone out, waiting at most this long */
void net_close_wait(int h, int seconds);

/* the UI can show progress and keep the screen alive while we wait */
extern void (*pf_idle)(void);

#endif
