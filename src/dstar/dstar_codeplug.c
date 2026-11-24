// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "dstar_codeplug.h"
#include "codeplug/codeplug_tag.h"

#include <string.h>

// Parameter block: URCALL, RPT1 and RPT2, each eight ASCII characters. An empty block is the original simplex format.
enum { CALL_LEN = DSTAR_CALL_CHARS, ROUTE_LEN = DSTAR_ROUTE_BYTES };

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

void dstar_channel_read(uint16_t number, const channel_t *channel, dstar_route_t *route)
{
	uint8_t b[CODEPLUG_TAG_HEAD_MAX + ROUTE_LEN];
	int len = codeplug_tag_params(number, channel, b, ROUTE_LEN);
	memset(route, 0, sizeof *route);
	for (unsigned i = 0; i < DSTAR_ROUTE_FIELDS && len >= CALL_LEN; i++, len -= CALL_LEN) {
		const uint8_t *call = b + i * CALL_LEN;
		unsigned end = CALL_LEN;
		while (end && call[end - 1] == ' ')
			end--;
		memcpy(route->call[i], call, end);
	}
}

bool dstar_route_pack(const dstar_route_t *route, uint8_t packed[ROUTE_LEN])
{
	bool has_repeater[2] = { false, false };
	memset(packed, ' ', ROUTE_LEN);
	for (unsigned i = 0; i < DSTAR_ROUTE_FIELDS; i++) {
		const char *s = route->call[i];
		uint8_t *out = packed + i * CALL_LEN;
		while (*s == ' ')
			s++;
		size_t n = strlen(s);
		while (n && s[n - 1] == ' ')
			n--;
		if (!n)
			continue;
		if (n > CALL_LEN)
			return false;
		for (size_t j = 0; j < n; j++)
			out[j] = s[j] >= 'a' && s[j] <= 'z' ? (uint8_t)(s[j] - 'a' + 'A') : (uint8_t)s[j];
		if (!call_chars(out, i == DSTAR_UR))
			return false;
		if (i == DSTAR_UR) {
			if (n == 1 && (out[0] == 'E' || out[0] == 'I' || out[0] == 'U')) {
				out[CALL_LEN - 1] = out[0];
				out[0] = ' ';
			}
			continue;
		}
		// A short form such as "N0ABC B" puts its module in the final column.
		if (n < 3 || (n < CALL_LEN && out[n - 2] != ' '))
			return false;
		for (size_t j = 1; j < n - 1; j++) {
			if (out[j - 1] == ' ' && out[j] != ' ')
				return false;
		}
		uint8_t module = out[n - 1];
		out[n - 1] = ' ';
		out[CALL_LEN - 1] = module;
		if (!repeater_call(out))
			return false;
		has_repeater[i - DSTAR_RPT1] = true;
	}
	return has_repeater[0] == has_repeater[1];
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

bool dstar_channel_save(uint16_t number, uint16_t from, const channel_t *channel, const dstar_route_t *route)
{
	uint8_t packed[ROUTE_LEN];
	if (route && !dstar_route_pack(route, packed))
		return false;
	uint16_t len = route ? sizeof packed : 0;
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
	if (channel->kind == CODEPLUG_KIND_DSTAR && from != 0 && from != number && codeplug_tag_copy(from, number, &stored) && !route)
		return true;
	return codeplug_tag_rewrite(number, stored.name, &tag, packed, len) ||
	       codeplug_tag_write(number, stored.name, &tag, packed, len);
}
