// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
#ifndef RADIO_DSTAR_H
#define RADIO_DSTAR_H

#include "hal_audio.h"
#include "codeplug/codeplug.h"
#include "radio/radio_air.h"

typedef struct {
	char source[9];
	bool rx, played;
} radio_dstar_view_t;

void radio_dstar_init(const hal_audio_t *audio);
bool radio_dstar_fits(void);
bool radio_dstar_running(void);
void radio_dstar_begin(const channel_t *channel, uint16_t number);
void radio_dstar_end(void);
bool radio_dstar_poll(radio_dstar_view_t *view);
extern const radio_air_ops_t radio_dstar_air;

#endif
