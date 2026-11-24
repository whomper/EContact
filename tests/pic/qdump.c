/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* qdump.c - quant_565 on a fixed picture for every count from 1 to 70:
   make quant compares this machine's output with 68030 code's */
#include <stdio.h>
#include <stdlib.h>
#include "quant.h"
int main(void){ /* a fixed pseudo-random picture, every want from 1 to 70 */
 static unsigned short px[4096]; unsigned long s=12345; int w,k; short rgb[256][3];
 for(int i=0;i<4096;i++){s=s*1103515245+12345; px[i]=(unsigned short)(s>>13);}
 for(w=1;w<=70;w++){int n=quant_565(px,4096,px[7],w,rgb);printf("%d:",n);for(k=0;k<n;k++)printf(" %d,%d,%d",rgb[k][0],rgb[k][1],rgb[k][2]);printf("\n");}
 return 0;}
