/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* png_slim.c - each PNG named, a hash of its rows from png_decode and
   png_decode_rgb: make slim compares png.c with and without -DPNG_SLIM */
#include <stdio.h>
#include <stdlib.h>
#include "png.h"
static unsigned long hsh; static long W;
static void mix(const unsigned char *p,long n){while(n--)hsh=hsh*1000003u^*p++;}
static int r4(void*u,long y,const unsigned char*p){(void)u;hsh^=y*7;mix(p,W*4);return 1;}
static int r3(void*u,long y,const unsigned char*p){(void)u;hsh^=y*13;mix(p,W*3);return 1;}
int main(int c,char**v){for(int i=1;i<c;i++){FILE*f=fopen(v[i],"rb");if(!f)continue;static unsigned char b[4<<20];long n=fread(b,1,sizeof b,f);fclose(f);long h;
 if(!png_size(b,n,&W,&h)){printf("%s none\n",v[i]);continue;}
 hsh=0;int r=png_decode(b,n,1<<22,r4,0);unsigned long a=hsh;hsh=0;int r2=png_decode_rgb(b,n,1<<22,r4,r3,0);
 printf("%s %d %lx %d %lx\n",v[i],r,a,r2,hsh);}return 0;}
