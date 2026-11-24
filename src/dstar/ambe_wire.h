// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef DSTAR_AMBE_WIRE_H
#define DSTAR_AMBE_WIRE_H

#include <stdbool.h>
#include "ambe2400.h"
#include "dv_frame.h"

void dstar_ambe_wire_encode(const uint8_t bits[DSTAR_AMBE_BITS], uint8_t frame[DSTAR_VOICE_BYTES]);
bool dstar_ambe_wire_decode(const uint8_t frame[DSTAR_VOICE_BYTES], uint8_t bits[DSTAR_AMBE_BITS]);

#endif
