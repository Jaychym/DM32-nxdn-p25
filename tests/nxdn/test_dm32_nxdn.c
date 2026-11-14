/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The board TX route on array-backed modem registers: routing, callback lifetime and microphone PCM isolation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal_audio_tap.h"
#include "hal_radio.h"
#include "modem/hrc7000/hrc7000.h"
#define CONFIG_NXDN 1
#define HRC_RAMFUNC
#define PIN_MIC_PATH 46
static hrc7000_t modem;
static uint32_t regs[0x600 / 4];
static uint8_t ram[0xA00];
static int fd;
static uint16_t fd_regs[128];
static bool transmitting, data_tx, have_rx, data_listen, held, mic_path, refuse_hook;
static radio_mode_t mode;
static bool rx_bw;
static unsigned pulls;
static void (*tx_hook)(void);
static uint32_t hrc_irq_save(void) { return 0; }
static void hrc_irq_restore(uint32_t s) { (void)s; }
static bool hrc_dmr_fm_tx_hook(void (*fn)(void)) { if (refuse_hook) return false; tx_hook = fn; return true; }
static void hrc_audio_pcm_hold(bool on) { held = on; }
static void hrc_pin_write(unsigned pin, bool on) { assert(pin == PIN_MIC_PATH); mic_path = on; }
static void fd6818_write(int *d, unsigned reg, uint16_t v) { (void)d; fd_regs[reg] = v; }
static uint16_t fd6818_read(int *d, unsigned reg) { (void)d; return fd_regs[reg]; }
static void ring_play(void) { hrc7000_audio_bits(&modem, 0, C7_AUDIO_RING_PLAY); }
static void speaker_source(void) {}
static bool hrc_fm_rx_start(void) { return true; }
static void hrc_fm_rx_stop(void) {}
static uint16_t hrc_fm_rx_read(int16_t *out, uint16_t n) { (void)out; (void)n; return 0; }
#include "dm32_nxdn.inc"
static void samples(int16_t *pcm, uint16_t n)
{
    assert(n == 256); pulls++;
    for (unsigned i = 0; i < n; i++) pcm[i] = (int16_t)(0x8000u + i);
}
int main(void)
{
    hrc7000_attach(&modem, regs, ram);
    memset(ram, 0x55, sizeof ram);
    assert(!flat_tx_inject(samples, AUDIO_TAP_FLAT));
    transmitting = true; mode = RADIO_MODE_DMR;
    assert(!flat_tx_inject(samples, AUDIO_TAP_FLAT));
    mode = RADIO_MODE_FM; refuse_hook = true;
    assert(!flat_tx_inject(samples, AUDIO_TAP_FLAT) && !data_tx && !held);
    refuse_hook = false;
    modem.audio_ctl = C7_AUDIO_PCM_MIC | C7_AUDIO_RING_PLAY;
    regs[C7_SYS_INTERP_MASK/4] = C7_SYS_FM_RX;
    regs[C7_FM_BANDWIDTH/4] = 0x50;
    mic_path = true;
    assert(!flat_tx_inject(samples, AUDIO_TAP_VOICE));
    assert(flat_tx_inject(samples, AUDIO_TAP_FLAT));
    assert(tx_hook && held && data_tx && !mic_path);
    assert((modem.audio_ctl & (C7_AUDIO_PCM_MIC | C7_AUDIO_TXRAM_SOURCE)) == (C7_AUDIO_PCM_MIC | C7_AUDIO_TXRAM_SOURCE));
    assert(!(modem.audio_ctl & C7_AUDIO_RING_PLAY));
    assert(regs[C7_SYS_INTERP_MASK/4] == (C7_SYS_FM_RX | C7_SYS_FM_TX));
    assert(regs[C7_FM_BANDWIDTH/4] == 0 && fd_regs[0x2B] == 0xC007);
    for (unsigned i = 0; i < sizeof ram; i++) assert(ram[i] == (i >= 0x30 && i < 0x430 ? 0 : 0x55));
    assert(!flat_tx_inject(samples, AUDIO_TAP_FLAT));
    for (unsigned half = 0; half <= 3; half++) {
        regs[C7_FM_ADDR_SW/4] = half;
        tx_hook();
    }
    assert(pulls == 2 && ram[0x30] == 0x80 && ram[0x31] == 0 && ram[0x230] == 0x80);
    void (*late)(void) = tx_hook;
    assert(flat_tx_inject(NULL, AUDIO_TAP_FLAT));
    assert(!held && !data_tx && !tx_hook && flat_pull == NULL);
    assert(regs[C7_SYS_INTERP_MASK/4] == C7_SYS_FM_RX && fd_regs[0x2B] == 0xC000);
    assert(modem.audio_ctl == C7_AUDIO_PCM_MIC);
    regs[C7_FM_ADDR_SW/4] = 1; late(); assert(pulls == 2);
    assert(flat_tx_inject(NULL, AUDIO_TAP_FLAT));
    puts("PASS TX RAM board route: guards, zeroed halves, mic PCM retained, ring off, callback and cleanup");
}
