/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
#ifndef DMR_AES_H
#define DMR_AES_H
#include <stdbool.h>
#include <stdint.h>
void dmr_aes_reset(void);
bool dmr_aes_begin(uint8_t slot, uint32_t dst);
bool dmr_aes_header(const uint8_t *pi, unsigned len, uint32_t dst, uint32_t now);
bool dmr_aes_voice(uint8_t ambe[27], unsigned index, bool tx, uint32_t now, uint32_t dst);
const uint8_t *dmr_aes_tx_header(void);
#endif
