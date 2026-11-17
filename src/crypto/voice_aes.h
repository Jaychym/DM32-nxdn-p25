/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
#ifndef VOICE_AES_H
#define VOICE_AES_H
#include <stdbool.h>
#include <stdint.h>
#include "aes.h"
enum { VOICE_AES_RESERVE = 4096 };
typedef struct {
    struct AES_ctx aes;
    uint8_t stream[213];
    uint64_t p25_mi;
    uint32_t dmr_mi;
    uint16_t superframes;
    uint8_t ready, position;
} voice_aes;
void voice_aes_wipe(void *p, unsigned n);
uint64_t voice_aes_p25_step(uint64_t mi);
uint32_t voice_aes_dmr_step(uint32_t mi);
void voice_aes_p25_start(voice_aes *s, const uint8_t key[32], uint64_t mi);
void voice_aes_p25_frame(voice_aes *s, unsigned frame, uint16_t u[8]);
void voice_aes_dmr_start(voice_aes *s, const uint8_t key[32], uint32_t mi);
/* A-F burst position; missing bursts are skipped, a whole lost superframe must reset the stream. */
bool voice_aes_dmr_burst(voice_aes *s, unsigned index, uint8_t bits[3][49]);
/* Whole superframes passed without a burst (a fade): the MI steps once for each, so the keystream is the one that many
 * superframes on. The burst position is kept. False when the stream is not ready or the superframe budget is spent. */
bool voice_aes_dmr_advance(voice_aes *s, unsigned superframes);
#endif
