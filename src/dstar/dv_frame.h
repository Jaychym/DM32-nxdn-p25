// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DM32_DSTAR_DV_FRAME_H
#define DM32_DSTAR_DV_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define DSTAR_HEADER_BYTES 39
#define DSTAR_HEADER_BITS 660
#define DSTAR_HEADER_AIR_BYTES ((DSTAR_HEADER_BITS + 7) / 8)
#define DSTAR_VOICE_BYTES 9
#define DSTAR_SLOW_DATA_BYTES 3
#define DSTAR_FRAME_BYTES (DSTAR_VOICE_BYTES + DSTAR_SLOW_DATA_BYTES)
#define DSTAR_SUPERFRAME_FRAMES 21

#define DSTAR_FLAG_REPEATER 0x40u

// Raw header: three flag bytes, RPT2, RPT1, UR, MY, and the four-byte MY suffix.
typedef struct {
	uint8_t flags[3];
	uint8_t rpt2[8], rpt1[8], ur[8], my[8], suffix[4];
} dstar_header_t;

// Packed over-the-air bits, earliest bit in bit 0. The header follows frame sync.
void dstar_header_encode(const dstar_header_t *header, uint8_t air[DSTAR_HEADER_AIR_BYTES]);
bool dstar_header_decode(const uint8_t air[DSTAR_HEADER_AIR_BYTES], dstar_header_t *header);

// A DV frame carries a codec-native voice block, already protected by the AMBE FEC.
void dstar_frame_encode(uint8_t out[DSTAR_FRAME_BYTES], const uint8_t voice[DSTAR_VOICE_BYTES],
	const uint8_t slow[DSTAR_SLOW_DATA_BYTES], uint8_t sequence);
bool dstar_frame_decode(const uint8_t frame[DSTAR_FRAME_BYTES], uint8_t voice[DSTAR_VOICE_BYTES],
	uint8_t slow[DSTAR_SLOW_DATA_BYTES]);

#endif
