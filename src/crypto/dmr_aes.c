/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
#include "config.h"
#if CONFIG_DMR
#include "dmr_aes.h"
#include "voice_aes.h"
#include "codeplug/codeplug.h"
#include "dmr/ambe/ambeFec.h"
#include "dmr/call/dmr_call.h"
#include <string.h>
static voice_aes stream;
static uint8_t header[12];
static uint32_t last_ms;
static bool heard_voice;

void dmr_aes_reset(void)
{
    voice_aes_wipe(&stream, sizeof stream);
    memset(header, 0, sizeof header);
    heard_voice = false; last_ms = 0;
}

bool dmr_aes_begin(uint8_t slot, uint32_t dst)
{
    uint8_t key[32]; uint16_t kid; uint64_t pmi; uint32_t mi;
    dmr_aes_reset();
    bool ok = codeplug_aes_key(slot, key, &kid) && kid <= 255 &&
              codeplug_aes_reserve(slot, &pmi, &mi);
    if (ok) {
        voice_aes_dmr_start(&stream, key, mi);
        header[0] = 0x25; header[1] = 0x10; header[2] = (uint8_t)kid;
        for (unsigned i = 0; i < 4; i++) header[3 + i] = (uint8_t)(mi >> (24 - 8 * i));
        header[7] = (uint8_t)(dst >> 16); header[8] = (uint8_t)(dst >> 8); header[9] = (uint8_t)dst;
        uint16_t crc = dmr_crc_ccitt(header, 10) ^ 0x6969;
        header[10] = (uint8_t)(crc >> 8); header[11] = (uint8_t)crc;
    }
    voice_aes_wipe(key, sizeof key);
    return ok;
}

const uint8_t *dmr_aes_tx_header(void) { return header; }

bool dmr_aes_header(const uint8_t *pi, unsigned len, uint32_t dst, uint32_t now)
{
    if (len != 12) { dmr_aes_reset(); return false; }
    uint16_t crc = dmr_crc_ccitt(pi, 10) ^ 0x6969;
    uint32_t to = ((uint32_t)pi[7] << 16) | ((uint32_t)pi[8] << 8) | pi[9];
    uint32_t mi = 0;
    for (unsigned i = 0; i < 4; i++) mi = (mi << 8) | pi[3 + i];
    if (pi[0] != 0x25 || pi[1] != 0x10 || !mi || !pi[2] || (dst && to != dst) ||
        pi[10] != (uint8_t)(crc >> 8) || pi[11] != (uint8_t)crc) {
        dmr_aes_reset(); return false;
    }
    if (stream.ready && !memcmp(header, pi, 12)) return true;
    uint8_t key[32];
    bool ok = codeplug_aes_lookup(pi[2], key);
    dmr_aes_reset();
    if (ok) {
        voice_aes_dmr_start(&stream, key, mi);
        memcpy(header, pi, 12); last_ms = now;
    }
    voice_aes_wipe(key, sizeof key);
    return ok;
}

bool dmr_aes_voice(uint8_t ambe[27], unsigned index, bool tx, uint32_t now, uint32_t dst)
{
    uint32_t to = ((uint32_t)header[7] << 16) | ((uint32_t)header[8] << 8) | header[9];
    if (!tx && dst != to) { dmr_aes_reset(); return false; }
    if (!tx && now - last_ms >= (heard_voice ? 300u : DMR_CALL_FADE_MS)) {
        dmr_aes_reset(); return false;
    }
    uint8_t bits[3][49];
    bool silence[3];
    for (unsigned f = 0; f < 3; f++) {
        silence[f] = !tx && !memcmp(ambe + 9 * f, dmr_ambe_silence, 9);
        ambe_fec_result_t fec;
        ambeFecDecode(ambe + 9 * f, bits[f], &fec);
        if (fec.c0_failed) silence[f] = true;
    }
    bool ok = voice_aes_dmr_burst(&stream, index, bits);
    if (ok) {
        for (unsigned f = 0; f < 3; f++) {
            if (silence[f]) memcpy(ambe + 9 * f, dmr_ambe_silence, 9);
            else ambeFecEncode(bits[f], ambe + 9 * f);
        }
        heard_voice = true; last_ms = now;
    }
    voice_aes_wipe(bits, sizeof bits);
    return ok;
}
#endif
