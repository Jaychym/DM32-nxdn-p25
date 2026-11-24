// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DSTAR_CODEPLUG_H
#define DSTAR_CODEPLUG_H

#include "codeplug/codeplug.h"
#include "dv_frame.h"

enum { DSTAR_UR, DSTAR_RPT1, DSTAR_RPT2, DSTAR_ROUTE_FIELDS };
#define DSTAR_CALL_CHARS 8u
#define DSTAR_ROUTE_BYTES (DSTAR_ROUTE_FIELDS * DSTAR_CALL_CHARS)

typedef struct {
	char call[DSTAR_ROUTE_FIELDS][DSTAR_CALL_CHARS + 1u];
} dstar_route_t;

void dstar_channel_read(uint16_t number, const channel_t *channel, dstar_route_t *route);
bool dstar_route_pack(const dstar_route_t *route, uint8_t packed[DSTAR_ROUTE_BYTES]);
bool dstar_channel_save(uint16_t number, uint16_t from, const channel_t *channel, const dstar_route_t *route);
bool dstar_channel_route(uint16_t number, const channel_t *channel, dstar_header_t *header);

#endif
