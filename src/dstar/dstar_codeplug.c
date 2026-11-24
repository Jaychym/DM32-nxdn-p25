// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "dstar_codeplug.h"
#include "codeplug/codeplug_tag.h"

#include <string.h>

// Parameter block: URCALL, RPT1 and RPT2, each eight ASCII characters. An empty block is the original simplex format.
enum { CALL_LEN = 8, ROUTE_LEN = 3 * CALL_LEN };

static bool call_chars(const uint8_t *call, bool ur)
{
	for (unsigned i = 0; i < CALL_LEN; i++) {
		uint8_t c = call[i];
		if (c != ' ' && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && !(ur && c == '/'))
			return false;
	}
	return true;
}

static bool repeater_call(const uint8_t *call)
{
	return call[0] != ' ' && call[7] >= 'A' && call[7] <= 'Z' && call_chars(call, false);
}

bool dstar_channel_route(uint16_t number, const channel_t *channel, dstar_header_t *header)
{
	uint8_t b[CODEPLUG_TAG_HEAD_MAX + ROUTE_LEN];
	int n = codeplug_tag_params(number, channel, b, ROUTE_LEN);
	if (n > 0 && n < ROUTE_LEN)
		return false;
	if (n <= 0)
		memset(b, ' ', ROUTE_LEN);
	const uint8_t *rpt1 = b + CALL_LEN, *rpt2 = b + 2 * CALL_LEN;
	bool routed = memcmp(rpt1, "        ", CALL_LEN) || memcmp(rpt2, "        ", CALL_LEN);
	if (!call_chars(b, true) || (routed && (!repeater_call(rpt1) || !repeater_call(rpt2))))
		return false;
	memset(header->flags, 0, sizeof header->flags);
	header->flags[0] = routed ? DSTAR_FLAG_REPEATER : 0u;
	memcpy(header->rpt1, routed ? (const void *)rpt1 : "DIRECT  ", CALL_LEN);
	memcpy(header->rpt2, routed ? (const void *)rpt2 : "DIRECT  ", CALL_LEN);
	memcpy(header->ur, memcmp(b, "        ", CALL_LEN) ? (const void *)b : "CQCQCQ  ", CALL_LEN);
	memset(header->suffix, ' ', sizeof header->suffix);
	return true;
}

bool dstar_channel_save(uint16_t number, uint16_t from, const channel_t *channel)
{
	channel_t stored = *channel;
	stored.mode = RADIO_MODE_DMR;
	stored.kind = CODEPLUG_KIND_DSTAR;
	stored.system = CODEPLUG_SYSTEM_NONE;
	stored.bw = RADIO_BW_12K5;
	codeplug_tag_t tag = {
		.kind = CODEPLUG_KIND_DSTAR, .system = CODEPLUG_SYSTEM_NONE,
		.flags = stored.rx_only ? 0 : CODEPLUG_TAG_TX
	};

	// The generic writer puts down a receive-only DMR base before writing or following the tag.
	if (!codeplug_set_channel(number, &stored))
		return false;
	if (channel->kind == CODEPLUG_KIND_DSTAR && from != 0 && from != number &&
	    codeplug_tag_copy(from, number, &stored))
		return true;
	return codeplug_tag_rewrite(number, stored.name, &tag, NULL, 0) ||
	       codeplug_tag_write(number, stored.name, &tag, NULL, 0);
}
