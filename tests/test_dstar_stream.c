// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "ambe_wire.h"
#include "dv_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const dstar_header_t simplex = {
	.rpt2 = "DIRECT  ", .rpt1 = "DIRECT  ", .ur = "CQCQCQ  ",
	.my = "XE1ARS  ", .suffix = "TEST",
};

static const uint8_t voice_a[DSTAR_VOICE_BYTES] = { 0x37, 0xad, 0x7c, 0x4e, 0, 0x53, 0xdc, 0xee, 0xe9 };
static const uint8_t voice_b[DSTAR_VOICE_BYTES] = { 0x49, 0xac, 0x05, 0x11, 0x82, 0x34, 0xde, 0xe2, 0x31 };

struct trace {
	unsigned starts, late, voices, ends;
	dstar_header_t header;
	uint8_t voice[DSTAR_VOICE_BYTES];
};

static void start(const dstar_header_t *header, void *ctx)
{
	struct trace *trace = ctx;
	trace->starts++;
	if (header)
		trace->header = *header;
	else
		trace->late++;
}

static void voice(const uint8_t *frame, void *ctx)
{
	struct trace *trace = ctx;
	trace->voices++;
	memcpy(trace->voice, frame, sizeof trace->voice);
}

static void end(void *ctx)
{
	struct trace *trace = ctx;
	trace->ends++;
}

static const dstar_stream_ops_t hooks = { start, voice, end };

static void feed(dstar_stream_t *r, const uint8_t *data, unsigned count, bool invert)
{
	for (unsigned i = 0; i < count; i++)
		dstar_stream_bit(r, ((data[i / 8] >> (i % 8)) & 1u) ^ invert);
}

static void opening(dstar_stream_t *r, const dstar_header_t *header, bool invert, bool has_error)
{
	uint8_t air[DSTAR_OPEN_BYTES];
	dstar_open_bits(header, air);
	static const uint8_t sync[] = { 0x37, 0x05 }; // 111011001010000 in transmission order
	for (unsigned i = 0; i < DSTAR_PREAMBLE_BITS / 8; i++)
		assert(air[i] == 0x55);
	for (unsigned i = 0; i < DSTAR_FRAME_SYNC_BITS; i++) {
		unsigned at = DSTAR_PREAMBLE_BITS + i;
		assert(((air[at / 8] >> (at % 8)) & 1u) == ((sync[i / 8] >> (i % 8)) & 1u));
	}
	if (has_error) {
		air[DSTAR_PREAMBLE_BITS / 8] ^= 1u << 3; // one error in the 15-bit opening sync
		air[60] ^= 1u << 4; // one error in the convolution-coded header
	}
	feed(r, air, DSTAR_OPEN_BITS, invert);
}

static void frame(dstar_stream_t *r, const uint8_t *data, uint8_t seq, bool invert)
{
	uint8_t air[DSTAR_FRAME_BYTES];
	static const uint8_t idle[DSTAR_SLOW_DATA_BYTES] = {0x66, 0x66, 0x66};
	dstar_frame_encode(air, data, idle, seq);
	feed(r, air, DSTAR_FRAME_BITS, invert);
}

static void test_conversation(bool invert)
{
	dstar_stream_t r;
	struct trace t = {0};
	dstar_stream_init(&r, &hooks, &t);
	opening(&r, &simplex, invert, true);
	assert(t.starts == 1 && t.late == 0 && t.voices == 0 && t.ends == 0);
	assert(memcmp(&t.header, &simplex, sizeof simplex) == 0);
	for (unsigned seq = 0; seq <= DSTAR_SUPERFRAME_FRAMES; seq++) {
		const uint8_t *expected = seq & 1 ? voice_a : voice_b;
		frame(&r, expected, seq, invert);
		assert(t.voices == seq + 1);
		assert(memcmp(t.voice, expected, sizeof t.voice) == 0);
	}
	feed(&r, dstar_end, DSTAR_END_BITS, invert);
	assert(t.ends == 1 && t.voices == DSTAR_SUPERFRAME_FRAMES + 1);
	dstar_stream_reset(&r);
	assert(t.ends == 1);
	opening(&r, &simplex, invert, false);
	assert(t.starts == 2);
	dstar_stream_reset(&r);
	assert(t.ends == 2);
}

static void test_late_entry(void)
{
	dstar_stream_t r;
	struct trace t = {0};
	dstar_stream_init(&r, &hooks, &t);
	frame(&r, voice_a, 0, true);
	for (unsigned seq = 1; seq < DSTAR_SUPERFRAME_FRAMES; seq++)
		frame(&r, voice_a, seq, true);
	assert(t.starts == 0 && t.voices == 0);
	frame(&r, voice_b, 0, true);
	assert(t.starts == 1 && t.late == 1 && t.voices == 0);
	frame(&r, voice_a, 1, true);
	assert(t.voices == 1 && memcmp(t.voice, voice_a, sizeof t.voice) == 0);
	for (unsigned seq = 0; seq < DSTAR_SUPERFRAME_FRAMES * 3u; seq++)
		frame(&r, voice_a, 1, true);
	assert(t.ends == 1);
}

static void test_non_dv(void)
{
	dstar_stream_t r;
	struct trace t = {0};
	dstar_stream_init(&r, &hooks, &t);
	uint8_t air[DSTAR_OPEN_BYTES];
	dstar_open_bits(&simplex, air);
	// Retain opening synchronization while replacing the convolution-coded payload with zeros.
	air[DSTAR_PREAMBLE_BITS / 8 + 1] &= 0x7fu;
	memset(air + DSTAR_PREAMBLE_BITS / 8 + 2, 0, sizeof air - DSTAR_PREAMBLE_BITS / 8 - 2);
	feed(&r, air, DSTAR_OPEN_BITS, false);
	assert(t.starts == 0);
	dstar_header_t dd = simplex;
	dd.flags[0] = 0x80;
	opening(&r, &dd, false, false);
	assert(t.starts == 0);
	opening(&r, &simplex, false, false);
	assert(t.starts == 1);
}

static void test_realign(bool insert)
{
	dstar_stream_t r;
	struct trace t = {0};
	dstar_stream_init(&r, &hooks, &t);
	opening(&r, &simplex, false, false);
	static const uint8_t idle[DSTAR_SLOW_DATA_BYTES] = {0x66, 0x66, 0x66};
	uint8_t air[DSTAR_FRAME_BYTES];
	dstar_frame_encode(air, voice_a, idle, 0);
	// Slip one bit before the first frame, so the 24-bit sync itself is untouched.
	if (insert) {
		dstar_stream_bit(&r, 1);
		feed(&r, air, DSTAR_FRAME_BITS, false);
	} else {
		for (unsigned i = 1; i < DSTAR_FRAME_BITS; i++)
			dstar_stream_bit(&r, (air[i / 8] >> (i % 8)) & 1u);
	}
	frame(&r, voice_b, 1, false);
	assert(memcmp(t.voice, voice_b, sizeof t.voice) == 0);
}

static void flip(uint8_t *frame, unsigned i)
{
	frame[i / 8] ^= 0x80u >> (i % 8);
}

static void test_channel_words(void)
{
	// Synthetic priority bits with Golay/whitening and the original DSD channel interleaver.
	static const uint8_t packed[] = {0x35, 0xea, 0xf0, 0xbe, 0x02, 0xd3, 0x55, 0x80};
	static const uint8_t expected[DSTAR_VOICE_BYTES] = {0x50, 0x38, 0xa7, 0x0f, 0x20, 0x60, 0xf3, 0x58, 0x5c};
	uint8_t bits[DSTAR_AMBE_BITS], decoded[DSTAR_AMBE_BITS], frame[DSTAR_VOICE_BYTES];
	for (unsigned i = 0; i < DSTAR_AMBE_BITS; i++)
		bits[i] = (packed[i / 8] >> (7 - i % 8)) & 1u;
	dstar_ambe_wire_encode(bits, frame);
	assert(memcmp(frame, expected, sizeof frame) == 0);
	assert(dstar_ambe_wire_decode(frame, decoded) && memcmp(bits, decoded, sizeof bits) == 0);

	for (unsigned row = 0; row < 12; row++) {
		for (unsigned slot = 0; slot < (row == 11 ? 3u : 4u); slot++) {
			unsigned i = row * 6u + slot;
			flip(frame, i);
			assert(dstar_ambe_wire_decode(frame, decoded) && memcmp(bits, decoded, sizeof bits) == 0);
			flip(frame, i);
		}
	}
	for (unsigned row = 0; row < 3; row++) {
		flip(frame, row * 6u);
		flip(frame, row * 6u + 2);
	}
	assert(dstar_ambe_wire_decode(frame, decoded) && memcmp(bits, decoded, sizeof bits) == 0);
	flip(frame, 18);
	assert(!dstar_ambe_wire_decode(frame, decoded));
}

int main(void)
{
	test_conversation(false);
	test_conversation(true);
	test_late_entry();
	test_non_dv();
	test_realign(false);
	test_realign(true);
	test_channel_words();
	puts("D-STAR stream and AMBE channel words passed");
	return 0;
}
