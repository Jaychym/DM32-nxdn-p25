/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "voice.h"
#include "p25_runtime.h"

static void admission_reset(p25_voice *v)
{
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    voice_aes_wipe(&v->aes, sizeof v->aes);
    v->aes_phase = v->aes_tx = 0;
#endif
    v->crypto_clear = v->have_group = 0;
    v->playing = v->emergency = v->encrypted = 0;
    v->active_nac = P25_NAC_ANY;
    v->active_group = 0;
    v->active_source = 0;
    dm32_imbe_init(&v->codec);
}

void p25_voice_rx_reset(p25_voice *v)
{
    /* Stale receive records must not reset the shared active TX codec. */
    if (v->transmitting) return;
    p25_rx_init(&v->framer, P25_NAC_ANY, 2);
    admission_reset(v);
}

void p25_voice_init(p25_voice *v, uint16_t nac, uint16_t group,
                    p25_pcm_sink sink, void *ctx)
{
    memset(v, 0, sizeof *v);
    v->sink = sink;
    v->sink_ctx = ctx;
    v->group_filter = group;
    /* F7E hears every NAC; F7F (the repeater's any) is the same on receive */
    v->nac_filter = (nac == 0xf7e || nac == 0xf7f) ? P25_NAC_ANY : nac;
    v->last_nac = P25_NAC_ANY;
    p25_rx_init(&v->framer, P25_NAC_ANY, 2);
    admission_reset(v);
}

#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
void p25_voice_set_aes(p25_voice *v, const uint8_t key[32], uint16_t kid)
{
    p25_voice_rx_reset(v);
    v->aes_required = key != NULL;
    v->aes_kid = kid;
    if (key) memcpy(v->aes_key, key, 32);
    else voice_aes_wipe(v->aes_key, 32);
}
static uint64_t mi_read(const uint8_t mi[9])
{
    uint64_t n = 0;
    for (unsigned i = 0; i < 8; i++) n = (n << 8) | mi[i];
    return n;
}
static void sync_make(p25_crypto_sync *s, uint64_t mi, uint16_t kid)
{
    memset(s, 0, sizeof *s); s->algorithm_id = 0x84; s->key_id = kid;
    for (unsigned i = 0; i < 8; i++) s->message_indicator[i] = (uint8_t)(mi >> (56 - i * 8));
}
static bool sync_matches(const p25_voice *v, const p25_decoded *d)
{
    return v->aes_required && d->metadata_valid && d->algorithm_id == 0x84 &&
           d->key_id == v->aes_kid && !d->message_indicator[8] && mi_read(d->message_indicator);
}
static bool sync_load(p25_voice *v, const p25_decoded *d)
{
    if (!v->aes_required || !d->metadata_valid || d->algorithm_id != 0x84 ||
        d->message_indicator[8] || !mi_read(d->message_indicator)) return false;
    if (v->aes_lookup && !v->aes_lookup(d->key_id, v->aes_key)) return false;
    if (!v->aes_lookup && d->key_id != v->aes_kid) return false;
    v->aes_kid = d->key_id;
    return true;
}
#endif

void p25_voice_rx_dibit(p25_voice *v, uint8_t dibit)
{
    if (v->transmitting || !p25_rx_push_dibit(&v->framer, dibit)) return;
    p25_decoded d;
    uint32_t at=v->micros?v->micros():0;
    int decoded=p25_decode_frame(v->framer.frame, p25_frame_bytes(v->framer.nid.duid), &d);
    uint32_t elapsed=v->micros?v->micros()-at:0;
    if (elapsed>v->fec_max_us)v->fec_max_us=elapsed;
    if (decoded) {
        admission_reset(v);
        return;
    }
    v->last_nac = d.nid.nac;
    v->last_duid = d.nid.duid;
    if (v->nac_filter != P25_NAC_ANY && d.nid.nac != v->nac_filter) {
        v->foreign_frames++;
        if (v->active_nac != P25_NAC_ANY) admission_reset(v);
        return;
    }
    v->rx_frames++;
    if (v->active_nac != d.nid.nac) {
        admission_reset(v);
        v->active_nac = d.nid.nac;
    }
    if (d.nid.duid == P25_DUID_TDU || d.nid.duid == P25_DUID_TDULC) {
        admission_reset(v);
        return;
    }
    if (!d.metadata_valid || (d.nid.duid == P25_DUID_HDU && d.manufacturer_id > 1)) {
        /* Do not reuse permission from a previous good header over damage. */
        v->crypto_clear = v->have_group = 0;
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
        v->aes.ready = 0;
#endif
    } else if (d.nid.duid == P25_DUID_HDU) {
        admission_reset(v);
        v->active_nac = d.nid.nac;
        v->crypto_clear = d.clear_voice;
        v->active_group = d.talkgroup;
        v->have_group = d.talkgroup != 0;
    } else if (d.nid.duid == P25_DUID_LDU1) {
        if ((v->have_group && v->active_group != d.talkgroup) ||
            (v->active_source && v->active_source != d.source_id)) {
            v->crypto_clear = 0;
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
            v->aes.ready = 0;
#endif
        }
        v->active_group = d.talkgroup;
        v->active_source = d.source_id;
        v->have_group = d.talkgroup && d.source_id && d.source_id <= 9999999u;
        if (!d.clear_voice) v->crypto_clear = 0;
        v->emergency = (d.service_options & 0x80u) != 0;
        /* LC's unprotected bit alone cannot override an encrypted ALGID.
         * Late entry waits for a valid LDU2 encryption sync. */
    } else if (d.nid.duid == P25_DUID_LDU2) {
        v->crypto_clear = d.clear_voice;
    }
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    if (d.nid.duid == P25_DUID_HDU && sync_load(v, &d)) {
        voice_aes_p25_start(&v->aes, v->aes_key, mi_read(d.message_indicator));
        v->aes_phase = 0;
    }
    bool decrypt = false;
    if (d.nid.duid == P25_DUID_LDU1) {
        decrypt = v->aes_required && d.metadata_valid && !d.clear_voice && v->aes.ready && !v->aes_phase;
        if (!decrypt) v->aes.ready = 0;
        v->aes_phase = decrypt ? 1 : 0;
    } else if (d.nid.duid == P25_DUID_LDU2) {
        decrypt = v->aes_required && v->aes.ready && v->aes_phase == 1 && sync_matches(v, &d);
    }
#endif
    if (d.nid.duid != P25_DUID_LDU1 && d.nid.duid != P25_DUID_LDU2) return;
    bool playable = v->crypto_clear;
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    if (v->aes_required) playable = decrypt;
#endif
    int admitted = playable && v->have_group &&
        (!v->group_filter || v->active_group == v->group_filter || v->active_group == 0xffff);
    v->playing = (uint8_t)admitted;
    v->encrypted = d.metadata_valid && !d.clear_voice;
    for (unsigned i = 0; i < 9; i++) {
        int16_t pcm[160] = {0};
        if (!admitted) v->rx_muted++;
        /* Conservative implementation policy, not a TIA concealment rule.
         * Golay/Hamming can miscorrect beyond t; no audio-quality claim. */
        else {
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
            if (decrypt) voice_aes_p25_frame(&v->aes, i + (d.nid.duid == P25_DUID_LDU2 ? 9 : 0), d.imbe[i]);
#endif
            at=v->micros?v->micros():0;
            int ok=d.voice_u0_corrected_bits[i] <= 3 && d.voice_corrected_bits[i] <= 10 &&
                   dm32_imbe_decode(&v->codec, d.imbe[i], pcm);
            elapsed=v->micros?v->micros()-at:0;
            if(elapsed>v->imbe_max_us)v->imbe_max_us=elapsed;
            if(!ok) {
                memset(pcm, 0, sizeof pcm);
                v->rx_erasures++;
                dm32_imbe_init(&v->codec);
            }
        }
        if (v->sink) v->sink(v->sink_ctx, pcm);
    }
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    if (d.nid.duid == P25_DUID_LDU2) {
        v->aes.ready = 0; v->aes_phase = 0;
        if (sync_load(v, &d)) voice_aes_p25_start(&v->aes, v->aes_key, mi_read(d.message_indicator));
    }
#endif
}

static p25_voice_packet *next_packet(p25_voice *v)
{
    return v->count < P25_VOICE_QUEUE ? &v->queue[v->head] : NULL;
}

static void commit_packet(p25_voice *v, uint16_t bytes)
{
    v->queue[v->head].bytes = bytes;
    v->head = (uint8_t)((v->head + 1) % P25_VOICE_QUEUE);
    v->count++;
    v->tx_frames++;
}

void p25_voice_tx_abort(p25_voice *v)
{
    v->transmitting = v->started = v->stopping = v->terminator_pending = 0;
    v->count = v->head = v->tail = v->voice_count = 0;
    v->packet_symbol = 0;
    p25_voice_rx_reset(v);
}

static int fail(p25_voice *v)
{
    p25_voice_tx_abort(v);
    v->fault = 1;
    return 0;
}

int p25_voice_tx_begin(p25_voice *v, const p25_call *call)
{
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    if (v->aes_required) return 0;
#endif
    if (!call || v->transmitting || !call->talkgroup ||
        !call->source_id || call->source_id > 9999999u) return 0;
    p25_voice_tx_abort(v);
    v->fault = 0;
    if (p25_encode_hdu(call, v->queue[0].data)) return 0;
    v->call = *call;
    v->transmitting = 1;
    v->next_duid = P25_DUID_LDU1;
    commit_packet(v, P25_HDU_BYTES);
    return 1;
}

#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
int p25_voice_tx_begin_aes(p25_voice *v, const p25_call *call, uint64_t mi)
{
    if (!v->aes_required || !mi || !call || v->transmitting || !call->talkgroup ||
        !call->source_id || call->source_id > 9999999u) return 0;
    p25_voice_tx_abort(v);
    v->call = *call; v->call.service_options |= 0x40;
    p25_crypto_sync sync;
    sync_make(&sync, mi, v->aes_kid);
    if (p25_encode_hdu_aes(&v->call, &sync, v->queue[0].data)) return 0;
    voice_aes_p25_start(&v->aes, v->aes_key, mi);
    v->aes_tx = 1; v->aes_superframes = 0; v->fault = 0;
    v->transmitting = 1; v->next_duid = P25_DUID_LDU1;
    commit_packet(v, P25_HDU_BYTES);
    return 1;
}
#endif

int p25_voice_tx_pcm(p25_voice *v, int16_t pcm[160])
{
    if (!v->transmitting || v->stopping || !pcm) return 0;
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    if (v->aes_tx && !v->voice_count && v->next_duid == P25_DUID_LDU1 &&
        v->aes_superframes >= VOICE_AES_RESERVE) return fail(v);
#endif
    if (!dm32_imbe_encode(&v->codec, pcm, v->voice[v->voice_count])) return fail(v);
    if (++v->voice_count < 9) return 1;
    p25_voice_packet *p = next_packet(v);
    uint8_t lsd[2] = {0, 0};
    if (!p) return fail(v);
#if defined(CONFIG_DV_AES) && CONFIG_DV_AES
    if (v->aes_tx) {
        unsigned base = v->next_duid == P25_DUID_LDU2 ? 9 : 0;
        for (unsigned i = 0; i < 9; i++) {
            v->voice[i][7] = (uint16_t)((v->voice[i][7] & ~1u) | ((i + (base != 0)) & 1));
            voice_aes_p25_frame(&v->aes, base + i, v->voice[i]);
        }
        unsigned off = base ? 200 : 99;
        lsd[0] = v->aes.stream[off]; lsd[1] = v->aes.stream[off + 1];
        p25_crypto_sync sync;
        uint64_t next = voice_aes_p25_step(v->aes.p25_mi);
        sync_make(&sync, next, v->aes_kid);
        if (p25_encode_ldu_aes(&v->call, &sync, v->next_duid, v->voice, lsd, p->data)) return fail(v);
        if (base) {
            v->aes_superframes++;
            voice_aes_p25_start(&v->aes, v->aes_key, next);
        }
    } else
#endif
    if (p25_encode_ldu(&v->call, v->next_duid, v->voice, lsd, p->data)) return fail(v);
    commit_packet(v, P25_LDU_BYTES);
    v->voice_count = 0;
    v->next_duid ^= P25_DUID_LDU1 ^ P25_DUID_LDU2;
    v->started = 1;
    return 1;
}

static void append_terminator(p25_voice *v)
{
    p25_voice_packet *p = next_packet(v);
    if (v->terminator_pending && p) {
        if (p25_encode_tdu(&v->call, p->data)) { fail(v); return; }
        commit_packet(v, P25_TDU_BYTES);
        v->terminator_pending = 0;
    }
}

int p25_voice_tx_end(p25_voice *v)
{
    if (!v->transmitting || v->stopping) return 0;
    if (!v->started && !v->voice_count) { p25_voice_tx_abort(v); return 1; }
    while (v->voice_count) {
        int16_t silence[160] = {0};
        if (!p25_voice_tx_pcm(v, silence)) return 0;
    }
    v->stopping = v->terminator_pending = 1;
    append_terminator(v);
    return !v->fault;
}

size_t p25_voice_tx_read(p25_voice *v, uint8_t *out, size_t capacity)
{
    if (!v->transmitting || !v->started || !out || !capacity) return 0;
    size_t n = 0;
    while (n < capacity) {
        append_terminator(v);
        if (!v->count) {
            if (v->stopping) { p25_voice_tx_abort(v); break; }
            v->tx_underruns++;
            fail(v);
            break;
        }
        p25_voice_packet *p = &v->queue[v->tail];
        unsigned bit = v->packet_symbol * 2u;
        out[n++] = (uint8_t)((p->data[bit / 8] >> (6 - bit % 8)) & 3u);
        if (++v->packet_symbol == p->bytes * 4u) {
            v->packet_symbol = 0;
            v->tail = (uint8_t)((v->tail + 1) % P25_VOICE_QUEUE);
            v->count--;
        }
    }
    /* A read ending exactly at the TDU boundary is done immediately. */
    if (v->transmitting && v->stopping && !v->count && !v->terminator_pending)
        p25_voice_tx_abort(v);
    return n;
}

int p25_voice_tx_done(const p25_voice *v) { return !v->transmitting; }
