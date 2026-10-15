/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * Contact photos (the AERA guidelines, 7.1): a JPEG from a card or a
 * server made into EContact's picture (TJpgDec, else jpegdc.c), and
 * jpegdc.c on its own, every block's colour read.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "photo.h"
#include "jpegdc.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static PHOTOIMG img;
	PHOTOREF ref;
	JDC *j;
	int w, h, why, r, g, b;
	memset(&ref, 0, sizeof(ref));
	ref.kind = PR_DATA;
	ref.fmt = PF_JPEG;
	if (n > 0 && (d[0] & 1)) {
		ref.has_crop = 1;
		ref.crop[0] = 10;
		ref.crop[1] = 10;
		ref.crop[2] = 50;
		ref.crop[3] = 50;
	}
	photo_kind(d, (long)n);
	photo_decode_jpeg(d, (long)n, &ref, &img);
	j = jdc_open(d, (long)n, &w, &h, &why);
	if (j) {
		if (jdc_read(j)) {
			/* every block of a small picture, the corners of a big one */
			int bw = (w + 7) / 8, bh = (h + 7) / 8, x, y;
			if ((long)bw * bh <= 4096) {
				for (y = 0; y < bh; y++)
					for (x = 0; x < bw; x++)
						jdc_rgb(j, x, y, &r, &g, &b);
			} else if (bw > 0 && bh > 0) {
				jdc_rgb(j, 0, 0, &r, &g, &b);
				jdc_rgb(j, bw - 1, bh - 1, &r, &g, &b);
			}
		}
		jdc_free(j);
	}
	return 0;
}
