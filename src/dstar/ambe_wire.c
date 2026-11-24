// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include <string.h>
#include "ambe_wire.h"
#include "dmr/ambe/ambeFec.h"

/*
 * The D-STAR channel order was verified against the original DSD interleaver
 * (include/dstar_const.h, 59423fa46be8b41ef0bd2f3d2b45590600be29f0).
 * Copyright (C) 2010 DSD Author
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

// Each row lists positions from the word's most significant bit to its least.
// Positions use the AMBE octets' MSB-first numbering. The DV framer sends
// those octets LSB first. The final two rows include the wrapped slots of C and D.
static const uint8_t positions[4][24] = {
	{ 0,6,12,18,24,30,36,42,48,54,60,66, 1,7,13,19,25,31,37,43,49,55,61,67 },
	{ 2,8,14,20,26,32,38,44,50,56,62,68, 3,9,15,21,27,33,39,45,51,57,63 },
	{ 69,4,10,16,22,28,34,40,46,52,58 },
	{ 64,70,5,11,17,23,29,35,41,47,53,59,65,71 },
};
static const uint8_t raw_len[4] = { 12, 12, 11, 14 };
static const uint8_t wire_len[4] = { 24, 23, 11, 14 };

void dstar_ambe_wire_encode(const uint8_t bits[DSTAR_AMBE_BITS], uint8_t frame[DSTAR_VOICE_BYTES])
{
	uint32_t words[4] = {0};
	unsigned at = 0;
	memset(frame, 0, DSTAR_VOICE_BYTES);
	for (unsigned w = 0; w < 4; w++)
		for (unsigned b = 0; b < raw_len[w]; b++)
			words[w] = (words[w] << 1) | (bits[at++] & 1u);
	words[1] = ambeGolay23Encode(words[1]) ^ ambeC1Mask(words[0]);
	words[0] = ambeGolay24Encode(words[0]);
	for (unsigned w = 0; w < 4; w++) {
		for (unsigned b = 0; b < wire_len[w]; b++) {
			unsigned pos = positions[w][b];
			unsigned bit = (words[w] >> (wire_len[w] - b - 1u)) & 1u;
			frame[pos / 8] |= bit << (7u - pos % 8);
		}
	}
}

bool dstar_ambe_wire_decode(const uint8_t frame[DSTAR_VOICE_BYTES], uint8_t bits[DSTAR_AMBE_BITS])
{
	uint32_t words[4] = {0};
	uint16_t a, b;
	for (unsigned w = 0; w < 4; w++) {
		for (unsigned i = 0; i < wire_len[w]; i++) {
			unsigned pos = positions[w][i];
			words[w] = (words[w] << 1) | ((frame[pos / 8] >> (7u - pos % 8)) & 1u);
		}
	}
	if (ambeGolay24Decode(words[0], &a) < 0 || ambeGolay23Decode(words[1] ^ ambeC1Mask(a), &b) < 0)
		return false;
	words[0] = a;
	words[1] = b;
	unsigned at = 0;
	for (unsigned w = 0; w < 4; w++)
		for (unsigned i = 0; i < raw_len[w]; i++)
			bits[at++] = (words[w] >> (raw_len[w] - i - 1u)) & 1u;
	return true;
}
