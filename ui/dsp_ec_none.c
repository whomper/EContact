/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsp_ec_none.c - the ST edition has no TLS of its own (tls_none.c), so
 * no curves to put on the DSP (common/ui/dsp_ec.c is the Falcon edition's).
 */
#include "ui.h"

void falcon_dsp_ec_init(void) { }
const char *falcon_dsp_ec_state(void) { return "68000"; }
