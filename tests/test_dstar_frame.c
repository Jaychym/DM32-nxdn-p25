// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "dv_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_header(void)
{
	const dstar_header_t header = {
		.rpt2 = "DIRECT  ", .rpt1 = "DIRECT  ", .ur = "CQCQCQ  ",
		.my = "XE1ARS  ", .suffix = "TEST",
	};
	// Generated independently from the JARL matrix and reflected CRC-16/IBM-SDLC.
	static const uint8_t expected[] = {
		0x38, 0xCD, 0xEB, 0x42, 0x40, 0xFC, 0x4A, 0xA8, 0xA0, 0x54, 0xAA, 0x1C,
		0x7B, 0x02, 0xB1, 0x5E, 0x23, 0x63, 0x1B, 0xB6, 0xF3, 0x06, 0xD9, 0x36,
		0x96, 0xAB, 0x72, 0xA2, 0x13, 0xCC, 0x8F, 0xF4, 0x5F, 0x83, 0x14, 0x58,
		0x2A, 0x14, 0x93, 0xD8, 0x83, 0xFF, 0xF7, 0xA0, 0x4F, 0xC5, 0xEA, 0xD8,
		0xD3, 0x96, 0x67, 0x75, 0x1D, 0x8E, 0xCC, 0xE6, 0x9D, 0x2F, 0x86, 0x2E,
		0xD0, 0xAD, 0x53, 0xAB, 0x2E, 0x2E, 0x2B, 0x07, 0x65, 0x53, 0xC9, 0xA1,
		0xD2, 0x6A, 0x04, 0xFD, 0x4A, 0x77, 0x1E, 0xAE, 0x49, 0xAE, 0x06,
	};
	uint8_t air[DSTAR_HEADER_AIR_BYTES];
	dstar_header_t decoded;
	assert(sizeof expected == sizeof air);
	dstar_header_encode(&header, air);
	assert(memcmp(air, expected, sizeof air) == 0);
	assert(dstar_header_decode(expected, &decoded) && memcmp(&decoded, &header, sizeof header) == 0);

	// Every single on-air bit error is corrected.
	for (unsigned at = 0; at < DSTAR_HEADER_BITS; ++at) {
		uint8_t mask = 1u << (at % 8);
		air[at / 8] ^= mask;
		assert(dstar_header_decode(air, &decoded) && memcmp(&decoded, &header, sizeof header) == 0);
		air[at / 8] ^= mask;
	}
	memset(air, 0, sizeof air);
	assert(!dstar_header_decode(air, &decoded));
	assert(memcmp(&decoded, &header, sizeof header) == 0);
}

static void test_voice_frame(void)
{
	const uint8_t voice[DSTAR_VOICE_BYTES] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
	const uint8_t slow[DSTAR_SLOW_DATA_BYTES] = { 0x40, 0x20, 0x21 };
	const uint8_t data_on_air[] = { 0x30, 0x6F, 0xB2 };
	const uint8_t sync_on_air[] = { 0x55, 0x2D, 0x16 };
	uint8_t air[DSTAR_FRAME_BYTES], voice_out[DSTAR_VOICE_BYTES], slow_out[DSTAR_SLOW_DATA_BYTES];
	dstar_frame_encode(air, voice, slow, 1);
	assert(memcmp(air + sizeof voice, data_on_air, sizeof slow) == 0);
	assert(!dstar_frame_decode(air, voice_out, slow_out));
	assert(memcmp(voice_out, voice, sizeof voice) == 0 && memcmp(slow_out, slow, sizeof slow) == 0);
	dstar_frame_encode(air, voice, slow, DSTAR_SUPERFRAME_FRAMES);
	assert(memcmp(air + sizeof voice, sync_on_air, sizeof slow) == 0);
	assert(dstar_frame_decode(air, voice_out, slow_out));
	assert(memcmp(voice_out, voice, sizeof voice) == 0);
}

int main(void)
{
	test_header();
	test_voice_frame();
	puts("D-STAR DV framing passed");
	return 0;
}
