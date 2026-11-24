// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DSTAR_CODEPLUG_H
#define DSTAR_CODEPLUG_H

#include "codeplug/codeplug.h"
#include "dv_frame.h"

bool dstar_channel_save(uint16_t number, uint16_t from, const channel_t *channel);
bool dstar_channel_route(uint16_t number, const channel_t *channel, dstar_header_t *header);

#endif
