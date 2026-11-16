/* SPDX-License-Identifier: GPL-3.0-or-later */
/* radio_dv.h on an NXDN48 channel: the board's flat FM audio paths
 * (hal_audio_tap.h fm_rx_flat / fm_tx_inject), the software modem and call
 * layer (nxdn_link.h) and the DMR voice bridge (core/dmr/ambe/ambeVoice.h:
 * the same vocoder, PCM stream and AMBE+2 FEC as DMR). Radio task only;
 * nothing blocks. radio_dv.c dispatches here for RADIO_MODE_NXDN. */
#ifndef RADIO_NXDN_H
#define RADIO_NXDN_H
#include "radio_dv.h"
#include "config.h"

bool radio_nxdn_select(const radio_dv_cfg_t *cfg);
bool radio_nxdn_ptt(bool down);
void radio_nxdn_status(radio_dv_status_t *out, uint32_t now_ms);
/* Every radio tick (10 ms): receive audio in, transmit ring topped up, voice paced to the speaker */
void radio_nxdn_service(uint32_t now_ms);

/* Bench builds only (CONFIG_P25_BENCH; 2.1 KB of RAM): the receive audio as the demodulator gets it, for measuring the radio's flat path (docs/nxdn-modem.md "What the
 * radio must confirm"): NXDN_SNAP_SAMPLES consecutive samples, then held NXDN_SNAP_HOLD_MS so the host reads them whole
 * with the debug link's READ_MEM (main SRAM; work/rx_ram_capture.py finds nxdn_snap in the map), then again. The
 * host takes a capture when seq and n read the same before and after the samples */
#if CONFIG_P25_BENCH
#define NXDN_SNAP_MAGIC 0x4E53584Eu   /* "NXSN" */
#define NXDN_SNAP_SAMPLES 1024u       /* 128 ms: at least one whole FSW, and 6 periods of 50 Hz */
#define NXDN_SNAP_HOLD_MS 2000u
typedef struct {
    uint32_t magic, seq, n;           /* captures completed; samples in (NXDN_SNAP_SAMPLES: held) */
    uint32_t start, t_ms;             /* the first sample's index in the channel's receive stream; when it filled */
    uint32_t peak, clips;             /* largest |sample|; samples at full scale */
    /* the receiver when it filled */
    uint32_t frames, calls, false_syncs, bad_lich, bad_sacch, foreign_ran, onsets, inverted;
    int32_t outer, dc;
    int16_t pcm[NXDN_SNAP_SAMPLES];
} nxdn_snap_t;
extern nxdn_snap_t nxdn_snap;
#endif

#endif
