// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DSTAR_DV_STREAM_H
#define DSTAR_DV_STREAM_H

#include "dv_frame.h"

#define DSTAR_PREAMBLE_BITS 64
#define DSTAR_FRAME_SYNC_BITS 15
#define DSTAR_OPEN_BITS (DSTAR_PREAMBLE_BITS + DSTAR_FRAME_SYNC_BITS + DSTAR_HEADER_BITS)
#define DSTAR_OPEN_BYTES ((DSTAR_OPEN_BITS + 7) / 8)
#define DSTAR_FRAME_BITS (DSTAR_FRAME_BYTES * 8)
#define DSTAR_END_BITS 48
#define DSTAR_HEADER_SYNC_WORD UINT32_C(0x557650)
#define DSTAR_STREAM_SYNC_WORD UINT32_C(0xAAB468)
#define DSTAR_IDLE_DIBITS 0x77

extern const uint8_t dstar_end[DSTAR_END_BITS / 8];

typedef struct {
	void (*start)(const dstar_header_t *header, void *ctx); // NULL header on late entry
	void (*voice)(const uint8_t frame[DSTAR_VOICE_BYTES], void *ctx);
	void (*end)(void *ctx);
} dstar_stream_ops_t;

typedef struct {
	dstar_stream_ops_t ops;
	void *ctx;
	uint32_t early, recent;
	uint16_t pos, since_sync;
	uint8_t used, state;
	bool invert, late;
	uint8_t frame[DSTAR_HEADER_AIR_BYTES];
} dstar_stream_t;

void dstar_open_bits(const dstar_header_t *header, uint8_t bits[DSTAR_OPEN_BYTES]);
void dstar_stream_init(dstar_stream_t *r, const dstar_stream_ops_t *ops, void *ctx);
void dstar_stream_reset(dstar_stream_t *r);
void dstar_stream_bit(dstar_stream_t *r, unsigned bit);

#endif
