// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#include "dstar_codeplug.h"
#include "codeplug/codeplug_tag.h"

#include <stddef.h>

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
