// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DSTAR_DV_ENGINE_H
#define DSTAR_DV_ENGINE_H

#include "air/air_session.h"
#include "dv_stream.h"

typedef struct {
	dstar_stream_ops_t rx;
	bool (*tx_voice)(uint8_t voice[DSTAR_VOICE_BYTES], void *ctx);
} dstar_engine_hooks_t;

typedef struct {
	air_session_t session;
	dstar_stream_t receive;
	dstar_engine_hooks_t hooks;
	void *ctx;
	uint8_t symbols[(DSTAR_OPEN_BITS + 3) / 4];
	uint16_t pos, len;
	uint8_t seq;
	volatile bool stopping;
	bool tail;
} dstar_engine_t;

extern const air_mode_t dstar_air_mode;
extern const uint8_t dstar_silence[DSTAR_VOICE_BYTES];

bool dstar_engine_open(dstar_engine_t *e, const hal_air_t *air, const dstar_engine_hooks_t *hooks, void *ctx);
void dstar_engine_close(dstar_engine_t *e);
bool dstar_engine_prime(dstar_engine_t *e, const dstar_header_t *header);
void dstar_engine_stop(dstar_engine_t *e);

#endif
