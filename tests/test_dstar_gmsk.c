// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "air/air_softmodem.h"
#include "dv_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SAMPLES_PER_BIT = 10, RX_DECIMATION = 6, VOICE_FRAMES = 2 * DSTAR_SUPERFRAME_FRAMES + 1 };
static const dstar_header_t header = {
	.rpt2 = "DIRECT  ", .rpt1 = "DIRECT  ", .ur = "CQCQCQ  ",
	.my = "XE1ABC  ", .suffix = "TEST",
};
static const uint8_t voice[DSTAR_VOICE_BYTES] = {0x9e, 0x8d, 0x32, 0x88, 0x26, 0x1a, 0x3f, 0x61, 0xe8};
static const uint8_t slow[DSTAR_SLOW_DATA_BYTES] = {0x66, 0x66, 0x66};

static const double gauss[] = {
	0.15054851, 0.14020298, 0.11323992, 0.07932379, 0.04819132, 0.02539194,
	0.01160338, 0.00459869, 0.00158069, 0.00047122, 0.00012183,
};

struct receive {
	dstar_stream_t stream;
	unsigned starts, voices, ends, timing_syncs;
};

static void started(const dstar_header_t *decoded, void *ctx)
{
	struct receive *r = ctx;
	assert(decoded && !memcmp(decoded, &header, sizeof header));
	r->starts++;
}

static void received_voice(const uint8_t *decoded, void *ctx)
{
	struct receive *r = ctx;
	assert(!memcmp(decoded, voice, sizeof voice));
	r->voices++;
}

static void ended(void *ctx)
{
	struct receive *r = ctx;
	r->ends++;
}

static void chunk(const air_chunk_t *c, void *ctx)
{
	struct receive *r = ctx;
	if (c->flags & AIR_CHUNK_GAP)
		dstar_stream_reset(&r->stream);
	if (c->sync_at >= 0)
		r->timing_syncs++;
	for (unsigned i = 0; i < c->nsyms; i++)
		dstar_stream_bit(&r->stream, !((c->hard[i / 4] >> (6u - 2u * (i % 4))) & 2u));
}

static void append(int8_t *bits, const uint8_t *bytes, unsigned count)
{
	for (unsigned i = 0; i < count; i++)
		bits[i] = ((bytes[i / 8] >> (i % 8)) & 1u) ? 1 : -1;
}

// One section of a fourth-order 2800 Hz Butterworth lowpass at the discriminator's 8 kHz sample rate.
struct lowpass {
	double b, a1, a2, z1, z2;
};

static double lowpass(struct lowpass *f, double x)
{
	double y = f->b * x + f->z1;
	f->z1 = 2 * f->b * x + f->z2 - f->a1 * y;
	f->z2 = f->b * x - f->a2 * y;
	return y;
}

static void test_discriminator(unsigned phase, int polarity)
{
	enum { BEFORE = 216, AFTER = 480, N = BEFORE + DSTAR_OPEN_BITS + VOICE_FRAMES * DSTAR_FRAME_BITS + DSTAR_END_BITS + AFTER };
	static int8_t bits[N];
	uint8_t opening[DSTAR_OPEN_BYTES], frame[DSTAR_FRAME_BYTES];
	int8_t *next = bits + BEFORE;
	dstar_open_bits(&header, opening);
	append(next, opening, DSTAR_OPEN_BITS);
	next += DSTAR_OPEN_BITS;
	for (unsigned i = 0; i < VOICE_FRAMES; i++) {
		dstar_frame_encode(frame, voice, slow, i);
		append(next, frame, DSTAR_FRAME_BITS);
		next += DSTAR_FRAME_BITS;
	}
	append(next, dstar_end, DSTAR_END_BITS);

	const dstar_stream_ops_t ops = {started, received_voice, ended};
	struct receive r = {0};
	air_softmodem_t m;
	dstar_stream_init(&r.stream, &ops, &r);
	assert(air_softmodem_rx_open(&m, &dstar_air_mode.profile, 8000, 0, chunk, &r));
	struct lowpass f1 = {.b = 0.27541329, .a1 = 0.67274091, .a2 = 0.14453520};
	struct lowpass f2 = {.b = 1, .a1 = 0.89765794, .a2 = 0.52718690};
	int16_t samples[256];
	unsigned used = 0;
	uint32_t at_us = 100000;
	uint32_t noise = 1;
	// BT=0.5 Gaussian filtering of the NRZ bits at 48 kHz, sampled by the discriminator at 8 kHz.
	for (int at = (int)phase + SAMPLES_PER_BIT; at < (N - 1) * SAMPLES_PER_BIT; at += RX_DECIMATION) {
		double x = gauss[0] * bits[at / SAMPLES_PER_BIT];
		for (unsigned i = 1; i < sizeof gauss / sizeof *gauss; i++)
			x += gauss[i] * (bits[(at - (int)i) / SAMPLES_PER_BIT] + bits[(at + (int)i) / SAMPLES_PER_BIT]);
		noise = noise * 1664525u + 1013904223u;
		int jitter = (int)(noise >> 24) - 128;
		x = 1000 * polarity * x + (at < BEFORE * SAMPLES_PER_BIT ? 6 * jitter : jitter / 3);
		samples[used++] = (int16_t)lowpass(&f2, lowpass(&f1, x));
		if (used == sizeof samples / sizeof *samples) {
			air_softmodem_rx(&m, samples, used, at_us);
			at_us += used * 125u;
			used = 0;
		}
	}
	air_softmodem_rx(&m, samples, used, at_us);
	assert(r.starts == 1 && r.voices == VOICE_FRAMES && r.ends == 1);
	assert(r.timing_syncs >= 3);
}

int main(void)
{
	for (unsigned phase = 0; phase < RX_DECIMATION; phase++) {
		test_discriminator(phase, 1);
		test_discriminator(phase, -1);
	}
	puts("D-STAR GMSK discriminator acquisition passed");
	return 0;
}
