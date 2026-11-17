/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
/* DMR IV expansion/ordering adapted from dmrgo, Copyright 2023-2026 Jacob McSwain (MIT). */
#include "voice_aes.h"
#include <string.h>

void voice_aes_wipe(void *p, unsigned n)
{
    volatile uint8_t *b = p;
    while (n--) *b++ = 0;
}

uint64_t voice_aes_p25_step(uint64_t mi)
{
    for (unsigned i = 0; i < 64; i++)
        mi = (mi << 1) | (((mi >> 63) ^ (mi >> 61) ^ (mi >> 45) ^
                          (mi >> 37) ^ (mi >> 26) ^ (mi >> 14)) & 1);
    return mi;
}

static void dmr_iv(uint32_t mi, uint8_t iv[16])
{
    memset(iv, 0, 16);
    for (unsigned i = 0; i < 4; i++) iv[i] = (uint8_t)(mi >> (24 - i * 8));
    for (unsigned i = 32; i < 128; i++) {
        uint8_t bit = (uint8_t)(((mi >> 31) ^ (mi >> 21) ^ (mi >> 1) ^ mi) & 1);
        mi = (mi << 1) | bit;
        iv[i / 8] = (uint8_t)((iv[i / 8] << 1) | bit);
    }
}

uint32_t voice_aes_dmr_step(uint32_t mi)
{
    for (unsigned i = 0; i < 32; i++)
        mi = (mi << 1) | (((mi >> 31) ^ (mi >> 21) ^ (mi >> 1) ^ mi) & 1);
    return mi;
}

static void ofb(voice_aes *s, uint8_t iv[16], unsigned n)
{
    AES_ECB_encrypt(&s->aes, iv); /* both voice formats discard the first OFB block */
    for (unsigned at = 0; at < n; at += 16) {
        AES_ECB_encrypt(&s->aes, iv);
        unsigned count = n - at < 16 ? n - at : 16;
        memcpy(s->stream + at, iv, count);
    }
    voice_aes_wipe(iv, 16);
}

void voice_aes_p25_start(voice_aes *s, const uint8_t key[32], uint64_t mi)
{
    uint8_t iv[16];
    uint64_t next = voice_aes_p25_step(mi);
    memset(s, 0, sizeof *s);
    AES_init_ctx(&s->aes, key);
    s->p25_mi = mi;
    for (unsigned i = 0; i < 8; i++) {
        iv[i] = (uint8_t)(mi >> (56 - i * 8));
        iv[8 + i] = (uint8_t)(next >> (56 - i * 8));
    }
    ofb(s, iv, 213);
    s->ready = 1;
}

void voice_aes_p25_frame(voice_aes *s, unsigned frame, uint16_t u[8])
{
    if (!s->ready || frame >= 18) return;
    unsigned offset = 11 + 11 * frame + (frame >= 8 ? 2 : 0) + (frame >= 17 ? 2 : 0);
    unsigned bit = 0;
    for (unsigned word = 0; word < 8; word++) {
        unsigned width = word < 4 ? 12 : word < 7 ? 11 : 7;
        for (unsigned j = 0; j < width; j++, bit++)
            u[word] ^= (uint16_t)(((s->stream[offset + bit / 8] >> (7 - bit % 8)) & 1u) << (width - j - 1));
    }
}

static void dmr_generate(voice_aes *s)
{
    uint8_t iv[16];
    dmr_iv(s->dmr_mi, iv);
    ofb(s, iv, 126);
}

void voice_aes_dmr_start(voice_aes *s, const uint8_t key[32], uint32_t mi)
{
    memset(s, 0, sizeof *s);
    AES_init_ctx(&s->aes, key);
    s->dmr_mi = mi;
    s->position = 255;
    s->ready = 1;
    dmr_generate(s);
}

bool voice_aes_dmr_advance(voice_aes *s, unsigned superframes)
{
    if (!s->ready) return false;
    if (!superframes) return true;
    if (superframes >= VOICE_AES_RESERVE || s->superframes + superframes >= VOICE_AES_RESERVE) { s->ready = 0; return false; }
    for (unsigned i = 0; i < superframes; i++) s->dmr_mi = voice_aes_dmr_step(s->dmr_mi);
    s->superframes = (uint16_t)(s->superframes + superframes);
    dmr_generate(s);
    return true;
}

bool voice_aes_dmr_burst(voice_aes *s, unsigned index, uint8_t bits[3][49])
{
    if (!s->ready || index >= 6) return false;
    if (s->position != 255 && index < s->position) {
        if (++s->superframes >= VOICE_AES_RESERVE) { s->ready = 0; return false; }
        s->dmr_mi = voice_aes_dmr_step(s->dmr_mi);
        dmr_generate(s);
    }
    s->position = (uint8_t)index;
    for (unsigned frame = 0; frame < 3; frame++)
        for (unsigned bit = 0; bit < 49; bit++)
            bits[frame][bit] ^= (s->stream[(index * 3 + frame) * 7 + bit / 8] >> (7 - bit % 8)) & 1;
    return true;
}
