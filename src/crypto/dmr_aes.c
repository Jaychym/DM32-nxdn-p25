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
static uint32_t last_slot, bound;
static bool heard_voice;
static uint8_t state;

void dmr_aes_reset(void)
{
    voice_aes_wipe(&stream, sizeof stream);
    memset(header, 0, sizeof header);
    heard_voice = false; last_slot = bound = 0;
    state = DMR_AES_NONE;
}

uint8_t dmr_aes_state(void) { return state; }

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

/* A PI header with FID 0x10 carries the algorithm, key ID and 32-bit MI of the call (bytes 0, 2 and 3-6; 7-9 are the
 * destination, 10-11 the CRC). The scheme and its AES-256 keystream are the DMR Association's, as DSD-FME and dmrgo
 * read them; the keystream is checked against dmrgo's public AES-256 capture (docs/aes256.md). 0x25 is AES-256, and
 * 0x05 is what other manufacturers send for it; the destination in the header is not compared with the call's, as
 * neither of those does, since it only says which call the header belongs to and the header comes on that call's slot. */
bool dmr_aes_header(const uint8_t *pi, unsigned len, uint32_t dst)
{
    if (len != 12) { dmr_aes_reset(); state = DMR_AES_BAD; return false; }
    uint16_t crc = dmr_crc_ccitt(pi, 10) ^ 0x6969;
    uint32_t mi = 0;
    for (unsigned i = 0; i < 4; i++) mi = (mi << 8) | pi[3 + i];
    uint8_t alg = pi[0], fid = pi[1], kid = pi[2];
    if (pi[10] != (uint8_t)(crc >> 8) || pi[11] != (uint8_t)crc || !mi || !kid) {
        dmr_aes_reset(); state = DMR_AES_BAD; return false;
    }
    if (fid != 0x10) {
        dmr_aes_reset(); state = DMR_AES_FID; return false;
    }
    if ((alg & 0xDFu) != 0x05) {
        dmr_aes_reset(); state = DMR_AES_ALGO; return false;
    }
    if (stream.ready && !memcmp(header, pi, 12)) return true;
    uint8_t key[32];
    bool ok = codeplug_aes_lookup(kid, key);
    dmr_aes_reset();
    state = ok ? DMR_AES_OK : DMR_AES_NOKEY;
    if (ok) {
        voice_aes_dmr_start(&stream, key, mi);
        memcpy(header, pi, 12); bound = dst;
    }
    voice_aes_wipe(key, sizeof key);
    return ok;
}

/* Whole superframes since the last burst decrypted, beyond the one a wrap of the burst index already counts. The bursts
 * of this slot come every 60 ms (two slot ticks): so many went by since the last one, give or take a tick, and the
 * distance in bursts is the one that agrees with the burst index. Only the modem's slot counter says: bursts arrive in
 * batches and late, so arrival time is no clock (a modem without a counter loses the call's keystream with a whole
 * superframe, as it always did). */
static unsigned lost_superframes(unsigned index, uint32_t slot)
{
    unsigned pos = stream.position;
    if (pos > 5) return 0;
    unsigned r = (index + 6 - pos) % 6;
    /* The same index again (the call stack has dropped repeats) is a superframe on, which a wrap of the index does not show */
    if (!slot || !last_slot) return r ? 0 : 1;
    uint32_t bursts = (slot - last_slot + 1) / 2;
    unsigned k = bursts > r ? (unsigned)((bursts - r + 3) / 6) : 0;
    if (!r && !k) k = 1;
    unsigned crossed = (pos + r + 6 * k) / 6;
    return crossed - (index < pos ? 1 : 0);
}

bool dmr_aes_voice(uint8_t ambe[27], unsigned index, bool tx, uint32_t dst, uint32_t slot)
{
    if (!tx) {
        if (!stream.ready) return false;
        /* The header was taken for one call: another destination on this slot is another call */
        if (!bound) bound = dst;
        else if (dst && dst != bound) { dmr_aes_reset(); return false; }
        if (heard_voice && index < 6 && !voice_aes_dmr_advance(&stream, lost_superframes(index, slot))) {
            dmr_aes_reset(); return false;
        }
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
        heard_voice = true; last_slot = slot;
    }
    voice_aes_wipe(bits, sizeof bits);
    return ok;
}
#endif
