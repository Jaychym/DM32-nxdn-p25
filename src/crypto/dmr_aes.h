/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
#ifndef DMR_AES_H
#define DMR_AES_H
#include <stdbool.h>
#include <stdint.h>
/* What became of the PI header of the call heard, for the screen (dmr_aes_state). DMR_AES_NONE is also the state
 * before any PI header: the call stack knows when the call is encrypted and no header came. */
enum {
    DMR_AES_NONE,   /* no PI header taken for this call */
    DMR_AES_BAD,    /* a PI header that is not 12 octets, fails its CRC, or has no key ID or no MI */
    DMR_AES_FID,    /* a PI header of a manufacturer other than FID 0x10 */
    DMR_AES_ALGO,   /* FID 0x10, but not AES-256 */
    DMR_AES_NOKEY,  /* AES-256, but no key slot holds its key ID */
    DMR_AES_OK      /* decrypting */
};
void dmr_aes_reset(void);
bool dmr_aes_begin(uint8_t slot, uint32_t dst);
/* A PI header of the call whose destination is dst (0: not known yet). True when the call is decrypting with it. */
bool dmr_aes_header(const uint8_t *pi, unsigned len, uint32_t dst);
/* One voice burst, A-F (index). slot: the modem's slot counter of the burst, 0 when it has none: the superframes that
 * went by without a burst are told by it. */
bool dmr_aes_voice(uint8_t ambe[27], unsigned index, bool tx, uint32_t dst, uint32_t slot);
const uint8_t *dmr_aes_tx_header(void);
uint8_t dmr_aes_state(void);
#endif
