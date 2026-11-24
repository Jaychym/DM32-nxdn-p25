// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DSTAR_AMBE2400_H
#define DSTAR_AMBE2400_H

#include <stdint.h>
#include "mbe/mbe.h"
#include "mbe/mbe_est.h"

#define DSTAR_AMBE_BITS 49

typedef struct {
	uint8_t l;
	int16_t gamma;
	int16_t logm[MBE_MAXL];
} dstar_ambe_history_t;

typedef struct {
	dstar_ambe_history_t history;
	mbe_est_state_t estimate;
	mbe_state_t synth;
	mbe_frame_t previous, current;
	mbe_params_t params;
	int32_t scratch[MBE_BUF_WORDS];
} dstar_ambe_t;

enum { DSTAR_VOICE_BAD = -1, DSTAR_VOICE_SILENCE, DSTAR_VOICE_OK };

// Raw AMBE fields in priority order, one bit per byte. No FEC or RF assumptions here.
void dstar_ambe_reset(dstar_ambe_t *codec);
int dstar_ambe_unpack(dstar_ambe_history_t *history, const uint8_t bits[DSTAR_AMBE_BITS], mbe_params_t *out);
void dstar_ambe_decode(dstar_ambe_t *codec, const uint8_t bits[DSTAR_AMBE_BITS], int16_t pcm[MBE_FRAME_LEN]);
void dstar_ambe_encode(dstar_ambe_t *codec, const int16_t pcm[MBE_FRAME_LEN], uint8_t bits[DSTAR_AMBE_BITS]);

#endif
