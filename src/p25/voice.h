/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef P25_VOICE_H
#define P25_VOICE_H
#include "p25.h"
#include "dm32_imbe.h"
#include <stdbool.h>
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
#include "voice_aes.h"
#endif

/* Conventional, clear, group voice; half duplex. All calls and the PCM sink
 * run in ONE worker task, never in an ISR. No dynamic allocation. The sink
 * must consume/copy 160 samples before returning. See docs/conventional.md.
 * A transport supplies dibits in on-air order; this does not assert that a
 * particular chip can expose that stream, or implement RF modulation.
 */
typedef void (*p25_pcm_sink)(void *ctx, const int16_t pcm[160]);
enum { P25_VOICE_QUEUE = 3 };
/* What an encrypted call heard is, and why it is muted or played (p25_voice.enc_state). radio_dv.c turns these into
 * the radio core's radio_dv_enc_t (core/radio/radio.h), numbered alike. */
enum {
    P25_ENC_NONE,   /* a clear call, or nothing heard */
    P25_ENC_ALGO,   /* an algorithm other than AES-256 (enc_algid): never decrypted */
    P25_ENC_OFF,    /* AES-256, but the channel has no key slot selected */
    P25_ENC_NOKEY,  /* AES-256, but no key is stored for the call's key ID (enc_kid) */
    P25_ENC_SYNC,   /* AES-256 with a key: waiting for an encryption sync (late entry, damaged header) */
    P25_ENC_LC,     /* the voice frame's link control refuses it: damaged, not group voice, or unprotected */
    P25_ENC_OK,     /* decrypting */
    P25_ENC_BAD     /* decrypting, but the vocoder refuses clean frames: the key or the keystream is wrong */
};
typedef struct {
    uint8_t data[P25_LDU_BYTES];
    uint16_t bytes;
} p25_voice_packet;
typedef struct {
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    voice_aes aes;
    uint8_t aes_key[32];
    uint16_t aes_kid, aes_superframes;
    uint8_t aes_required, aes_phase, aes_tx;
    bool (*aes_lookup)(uint16_t kid, uint8_t key[32]);
    /* For the screen: the last valid encryption sync (HDU or LDU2) of this call, and what became of the call */
    uint16_t enc_kid;
    uint8_t enc_state, enc_algid, enc_nokey;
    uint8_t enc_seen, enc_bad; /* clean decrypted frames given to the vocoder, and those it refused */
#endif
    dm32_imbe_context codec; /* RX and TX share it; every direction change resets it */
    p25_rx framer;
    p25_call call;
    p25_pcm_sink sink;
    void *sink_ctx;
    uint16_t group_filter; /* 0 = all group voice; implementation policy */
    uint16_t nac_filter;   /* P25_NAC_ANY or the channel NAC; the framer hears every NAC */
    /* Channel activity for the radio core (busy lockout, scan, display). */
    uint16_t last_nac;     /* NAC of the last valid frame, P25_NAC_ANY before any */
    uint8_t last_duid, playing, emergency, encrypted;
    uint32_t foreign_frames; /* valid frames with another NAC: counted, never played */
    uint16_t active_nac;
    uint16_t active_group;
    uint32_t active_source;
    uint16_t voice[9][8];
    p25_voice_packet queue[P25_VOICE_QUEUE];
    uint16_t packet_symbol;
    uint8_t head, tail, count, voice_count, next_duid;
    uint8_t transmitting, started, stopping, terminator_pending, fault;
    uint8_t crypto_clear, have_group;
    uint32_t rx_frames, rx_muted, rx_erasures, tx_frames, tx_underruns;
    uint32_t (*micros)(void); /* optional task-level profiling, no clock assumption */
    uint32_t fec_max_us, imbe_max_us;
} p25_voice;

/* nac: the channel's RX NAC (0xF7E or P25_NAC_ANY hears every NAC). Frames
 * with another NAC complete in the framer for the activity fields above but
 * never reach admission or the sink. */
void p25_voice_init(p25_voice *v, uint16_t nac, uint16_t group,
                    p25_pcm_sink sink, void *ctx);
/* Clear RX admission/timing after tuning, a discontinuity, or carrier loss.
 * Caller owns the receive hang timer; no time unit is guessed here. */
void p25_voice_rx_reset(p25_voice *v);
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
void p25_voice_set_aes(p25_voice *, const uint8_t key[32], uint16_t kid);
int p25_voice_tx_begin_aes(p25_voice *, const p25_call *, uint64_t mi);
#endif
void p25_voice_rx_dibit(p25_voice *v, uint8_t dibit);
/* The same for one frame the channel decoder already decoded without a failure (p25_decode_frame returned 0): the
 * dibit path ends here. Used by host tests for frames the encoders cannot make. The IMBE words are decrypted in place. */
void p25_voice_rx_decoded(p25_voice *v, p25_decoded *d);
int p25_voice_tx_begin(p25_voice *v, const p25_call *call);
/* Exactly one 8 kHz / 20 ms microphone block; mutable for the codec.
 * Returns 0 on invalid state, codec failure or queue overflow. Abort/unkey.
 * Producer must be paced by microphone time, not called in an unbounded loop. */
int p25_voice_tx_pcm(p25_voice *v, int16_t pcm[160]);
/* Release: pad only the incomplete nine-frame LDU with encoded silence,
 * then a TDU. Empty, unstarted calls are canceled. Task context only. */
int p25_voice_tx_end(p25_voice *v);
/* Up to capacity dibits, including across CAI unit boundaries. Zero means
 * prefill waiting or completely drained; inspect tx_done(). No fabricated
 * idle/CACH/sync is inserted. An active underrun sets fault and ends TX.
 * Initial prefill holds HDU until the first complete LDU is queued. */
size_t p25_voice_tx_read(p25_voice *v, uint8_t *dibits, size_t capacity);
int p25_voice_tx_done(const p25_voice *v);
void p25_voice_tx_abort(p25_voice *v);
#endif
