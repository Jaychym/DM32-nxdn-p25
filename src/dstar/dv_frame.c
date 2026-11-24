// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
// JARL D-STAR specification, sections 4.1 and 6, appendices 1 and 2.
#include "dv_frame.h"

#include <limits.h>
#include <string.h>

#define HEADER_WITH_FCS (DSTAR_HEADER_BYTES + 2)
#define HEADER_TRELLIS_STEPS (HEADER_WITH_FCS * 8 + 2)
#define INTERLEAVER_ROWS 24
#define CONV_STATES 4

static const uint8_t slow_mask[] = { 0x70, 0x4F, 0x93 };
static const uint8_t slow_sync[] = { 0x55, 0x2D, 0x16 };

static uint8_t bit(const uint8_t *bytes, unsigned at)
{
	return (bytes[at / 8] >> (at % 8)) & 1u;
}

static void set_bit(uint8_t *bytes, unsigned at, uint8_t value)
{
	bytes[at / 8] |= value << (at % 8);
}

static uint16_t header_fcs(const uint8_t *bytes)
{
	uint16_t fcs = 0xFFFF;
	for (unsigned i = 0; i < DSTAR_HEADER_BYTES; ++i) {
		fcs ^= bytes[i];
		for (unsigned j = 0; j < 8; ++j)
			fcs = (fcs >> 1) ^ ((fcs & 1u) ? 0x8408u : 0u);
	}
	return fcs ^ 0xFFFF;
}

static uint8_t convolution(uint8_t state, uint8_t in)
{
	uint8_t old = state >> 1;
	uint8_t g1 = in ^ (state & 1u) ^ old;
	uint8_t g2 = in ^ old;
	return g1 | (g2 << 1);
}

// Appendix 2 sends each interleaver row in order. Rows 0–11 have one more symbol.
static unsigned air_index(unsigned coded)
{
	unsigned row = coded % INTERLEAVER_ROWS;
	return row * 27 + (row < 12 ? row : 12) + coded / INTERLEAVER_ROWS;
}

static void scramble(uint8_t air[DSTAR_HEADER_AIR_BYTES])
{
	uint8_t state = 0x7F;
	for (unsigned i = 0; i < DSTAR_HEADER_BITS; ++i) {
		uint8_t feedback = ((state >> 6) ^ (state >> 3)) & 1u;
		air[i / 8] ^= feedback << (i % 8);
		state = (uint8_t)((state << 1) | feedback);
	}
}

void dstar_header_encode(const dstar_header_t *header, uint8_t air[DSTAR_HEADER_AIR_BYTES])
{
	uint8_t raw[HEADER_WITH_FCS] = {0};
	memcpy(raw, header, DSTAR_HEADER_BYTES);
	uint16_t fcs = header_fcs(raw);
	raw[DSTAR_HEADER_BYTES] = (uint8_t)fcs;
	raw[DSTAR_HEADER_BYTES + 1] = (uint8_t)(fcs >> 8);

	memset(air, 0, DSTAR_HEADER_AIR_BYTES);
	uint8_t state = 0;
	for (unsigned i = 0; i < HEADER_TRELLIS_STEPS; ++i) {
		uint8_t in = i < HEADER_WITH_FCS * 8 ? bit(raw, i) : 0;
		uint8_t pair = convolution(state, in);
		set_bit(air, air_index(2 * i), pair & 1u);
		set_bit(air, air_index(2 * i + 1), pair >> 1);
		state = ((state << 1) | in) & (CONV_STATES - 1);
	}
	scramble(air);
}

static unsigned distance(uint8_t a, uint8_t b)
{
	uint8_t mismatch = a ^ b;
	return (mismatch & 1u) + (mismatch >> 1);
}

bool dstar_header_decode(const uint8_t air[DSTAR_HEADER_AIR_BYTES], dstar_header_t *header)
{
	uint8_t bits[DSTAR_HEADER_AIR_BYTES];
	uint8_t survivors[HEADER_TRELLIS_STEPS];
	uint8_t raw[HEADER_WITH_FCS] = {0};
	const unsigned unreachable = UINT_MAX / 2;
	unsigned metric[CONV_STATES] = {0, unreachable, unreachable, unreachable};

	memcpy(bits, air, sizeof bits);
	scramble(bits);
	for (unsigned i = 0; i < HEADER_TRELLIS_STEPS; ++i) {
		uint8_t received = bit(bits, air_index(2 * i)) | (bit(bits, air_index(2 * i + 1)) << 1);
		unsigned next[CONV_STATES];
		uint8_t choose_high = 0;
		for (uint8_t s = 0; s < CONV_STATES; ++s) {
			uint8_t low = s >> 1, high = low | 2u, in = s & 1u;
			unsigned a = metric[low] + distance(received, convolution(low, in));
			unsigned b = metric[high] + distance(received, convolution(high, in));
			next[s] = a;
			if (b < a) {
				next[s] = b;
				choose_high |= 1u << s;
			}
		}
		memcpy(metric, next, sizeof metric);
		survivors[i] = choose_high;
	}
	// Two zero tail bits guarantee that the final state is zero.
	uint8_t state = 0;
	for (unsigned i = HEADER_TRELLIS_STEPS; i-- > 0;) {
		if (i < HEADER_WITH_FCS * 8)
			set_bit(raw, i, state & 1u);
		state = (state >> 1) | (((survivors[i] >> state) & 1u) << 1);
	}
	uint16_t fcs = raw[DSTAR_HEADER_BYTES] | ((uint16_t)raw[DSTAR_HEADER_BYTES + 1] << 8);
	if (header_fcs(raw) != fcs)
		return false;
	memcpy(header, raw, DSTAR_HEADER_BYTES);
	return true;
}

void dstar_frame_encode(uint8_t out[DSTAR_FRAME_BYTES], const uint8_t voice[DSTAR_VOICE_BYTES],
	const uint8_t slow[DSTAR_SLOW_DATA_BYTES], uint8_t sequence)
{
	memcpy(out, voice, DSTAR_VOICE_BYTES);
	if (sequence % DSTAR_SUPERFRAME_FRAMES == 0) {
		memcpy(out + DSTAR_VOICE_BYTES, slow_sync, DSTAR_SLOW_DATA_BYTES);
		return;
	}
	for (unsigned i = 0; i < DSTAR_SLOW_DATA_BYTES; ++i)
		out[DSTAR_VOICE_BYTES + i] = slow[i] ^ slow_mask[i];
}

bool dstar_frame_decode(const uint8_t frame[DSTAR_FRAME_BYTES], uint8_t voice[DSTAR_VOICE_BYTES],
	uint8_t slow[DSTAR_SLOW_DATA_BYTES])
{
	memcpy(voice, frame, DSTAR_VOICE_BYTES);
	bool sync = memcmp(frame + DSTAR_VOICE_BYTES, slow_sync, DSTAR_SLOW_DATA_BYTES) == 0;
	for (unsigned i = 0; i < DSTAR_SLOW_DATA_BYTES; ++i)
		slow[i] = sync ? 0 : frame[DSTAR_VOICE_BYTES + i] ^ slow_mask[i];
	return sync;
}
