// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "dv_stream.h"

#include <string.h>

#define SYNC_MASK UINT32_C(0xFFFFFF)
#define SYNC_INTERVAL (DSTAR_SUPERFRAME_FRAMES * DSTAR_FRAME_BITS)
#define TAIL_HIGH UINT32_C(0xAAAA)
#define TAIL_LOW UINT32_C(0xAAAA135E)

enum { SEARCH, HEADER, STREAM, LATE_SYNC };

const uint8_t dstar_end[DSTAR_END_BITS / 8] = { 0x55, 0x55, 0x55, 0x55, 0xC8, 0x7A };

static void put(uint8_t *dst, unsigned pos, unsigned b)
{
	dst[pos / 8] |= b << (pos % 8);
}

void dstar_open_bits(const dstar_header_t *header, uint8_t out[DSTAR_OPEN_BYTES])
{
	uint8_t air[DSTAR_HEADER_AIR_BYTES];
	memset(out, 0, DSTAR_OPEN_BYTES);
	memset(out, 0x55, DSTAR_PREAMBLE_BITS / 8);
	dstar_header_encode(header, air);
	uint32_t sync = DSTAR_HEADER_SYNC_WORD & 0x7FFF;
	for (unsigned i = 0; i < DSTAR_FRAME_SYNC_BITS; i++)
		put(out, DSTAR_PREAMBLE_BITS + i, (sync >> (DSTAR_FRAME_SYNC_BITS - 1 - i)) & 1u);
	for (unsigned i = 0; i < DSTAR_HEADER_BITS; i++)
		put(out, DSTAR_PREAMBLE_BITS + DSTAR_FRAME_SYNC_BITS + i, (air[i / 8] >> (i % 8)) & 1u);
}

void dstar_stream_init(dstar_stream_t *r, const dstar_stream_ops_t *ops, void *ctx)
{
	memset(r, 0, sizeof(*r));
	r->ops = *ops;
	r->ctx = ctx;
}

void dstar_stream_reset(dstar_stream_t *r)
{
	if (r->state == STREAM && r->ops.end)
		r->ops.end(r->ctx);
	r->state = SEARCH;
	r->used = 0;
	r->pos = 0;
	r->since_sync = 0;
	r->early = r->recent = 0;
}

static void begin(dstar_stream_t *r, const dstar_header_t *h)
{
	r->state = STREAM;
	r->since_sync = 0;
	r->pos = 0;
	r->late = h == NULL;
	memset(r->frame, 0, DSTAR_FRAME_BYTES);
	if (r->ops.start)
		r->ops.start(h, r->ctx);
}

void dstar_stream_bit(dstar_stream_t *r, unsigned bit)
{
	bit &= 1u;
	r->early = (r->early << 1) | (r->recent >> 31);
	r->recent = (r->recent << 1) | bit;
	if (r->used < DSTAR_END_BITS)
		r->used++;
	uint32_t look = r->recent & SYNC_MASK;
	unsigned header_errs = r->used >= 24 ? __builtin_popcount(look ^ DSTAR_HEADER_SYNC_WORD) : 12;
	bool sync = look == DSTAR_STREAM_SYNC_WORD || (look ^ SYNC_MASK) == DSTAR_STREAM_SYNC_WORD;

	if ((r->state == SEARCH || r->state == LATE_SYNC) && (header_errs <= 1 || header_errs >= 23)) {
		r->invert = header_errs >= 23;
		r->state = HEADER;
		r->pos = 0;
		memset(r->frame, 0, DSTAR_HEADER_AIR_BYTES);
		return;
	}
	if (r->state == SEARCH) {
		if (r->used >= 24 && sync) {
			r->state = LATE_SYNC;
			r->invert = look != DSTAR_STREAM_SYNC_WORD;
			r->since_sync = 0;
		}
		return;
	}
	if (r->state == LATE_SYNC) {
		r->since_sync++;
		if (r->since_sync >= SYNC_INTERVAL - 2 &&
		    look == (DSTAR_STREAM_SYNC_WORD ^ (r->invert ? SYNC_MASK : 0))) {
			begin(r, NULL);
		} else if (r->since_sync >= SYNC_INTERVAL + 2) {
			r->state = SEARCH;
		}
		return;
	}
	if (r->state == HEADER) {
		put(r->frame, r->pos++, bit ^ r->invert);
		if (r->pos == DSTAR_HEADER_BITS) {
			dstar_header_t h;
			if (dstar_header_decode(r->frame, &h) && !(h.flags[0] & 0x80))
				begin(r, &h);
			else
				r->state = SEARCH;
		}
		return;
	}

	uint32_t polarity = r->invert ? UINT32_MAX : 0;
	if (r->used >= DSTAR_END_BITS && (r->recent ^ polarity) == TAIL_LOW &&
	    ((r->early ^ polarity) & 0xFFFFu) == TAIL_HIGH) {
		dstar_stream_reset(r);
		return;
	}
	// Three superframes without a sync: a receiver can coast over errors, then releases the channel.
	if (++r->since_sync > 3 * SYNC_INTERVAL) {
		dstar_stream_reset(r);
		return;
	}
	if ((r->pos >= DSTAR_VOICE_BYTES * 8 - 2 || r->pos <= 2) &&
	    (look ^ (polarity & SYNC_MASK)) == DSTAR_STREAM_SYNC_WORD) {
		// If the clock slipped, discard this frame and align to the next one.
		if (r->pos == DSTAR_FRAME_BITS - 1 && r->ops.voice)
			r->ops.voice(r->frame, r->ctx);
		r->since_sync = r->pos = 0;
		memset(r->frame, 0, DSTAR_FRAME_BYTES);
		return;
	}
	put(r->frame, r->pos++, bit ^ r->invert);
	if (r->pos == DSTAR_FRAME_BITS) {
		if (r->ops.voice)
			r->ops.voice(r->frame, r->ctx);
		r->pos = 0;
		memset(r->frame, 0, DSTAR_FRAME_BYTES);
	}
}
