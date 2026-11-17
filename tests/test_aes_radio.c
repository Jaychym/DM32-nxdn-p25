/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "codeplug/codeplug.h"
#include "storage/storage.h"
#include "settings/settings.h"
#include "sim/sim_flash.h"
#include "sim/dmr_sim_modem.h"
#include "dmr/call/dmr_call.h"
#include "dmr/ambe/ambeFec.h"
#include "voice_aes.h"
#include "dmr_aes.h"
void storage_lock(void) {} void storage_unlock(void) {}
void storage_quick_lock(void) {} void storage_quick_unlock(void) {}
static uint32_t now;
uint32_t hal_millis(void) { return now; }
static bool chip(hal_chip_t *c) { memset(c,0,sizeof *c);c->uid[0]=123;c->uid[1]=456;c->uid[2]=789;return true; }
static const hal_system_t system_hal={.chip=chip};
const board_t board={.flash=&sim_flash,.system=&system_hal};
static const hal_flash_map_t map={.settings_addr=0x11000,.settings_size=0x8000,.journal_addr=0x19000,.openfw_eeprom=0,.openfw_flash=0x20000,.openfw_end=0x40000,.calibration_addr=0x10000,.calibration_size=0x200};
static uint8_t table[400], frame[9]; static unsigned heard;
static sim_air_t air[SIM_AIR_MAX];static unsigned air_count;
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void crc_entry(uint8_t *e) {put32(e+92,storage_crc32(0,e,92));}
static void provision(void)
{
    memset(table,0,sizeof table);memcpy(table,"DVAES1\0\0",8);table[8]=4;table[9]=96;
    put32(table+12,storage_crc32(0,table,12));
    uint8_t *e=table+16;e[0]=1;e[2]=42;
    for(unsigned i=0;i<32;i++)e[4+i]=(uint8_t)i;
    for(unsigned i=0;i<16;i++)e[36+i]=(uint8_t)(0xa0+i);
    crc_entry(e);assert(storage_write(0x7c00,table,sizeof table));
}
static void boot(void) {sim_flash_power_on();assert(storage_init());settings_init();}
static void test_store(void)
{
    sim_flash_create(0x40000,&map);boot();provision();
    uint8_t key[32];uint16_t kid;uint64_t p[4];uint32_t d[4];
    assert(codeplug_aes_key(1,key,&kid)&&kid==42);assert(!codeplug_aes_key(5,key,&kid));
    /* The channel menu's check: the key ID only, 0 for an empty, missing or damaged slot. */
    assert(codeplug_aes_kid(1)==42&&codeplug_aes_kid(2)==0&&codeplug_aes_kid(0)==0&&codeplug_aes_kid(5)==0);
    table[16+92]^=1;assert(storage_write(0x7c00,table,sizeof table));assert(codeplug_aes_kid(1)==0);
    table[16+92]^=1;assert(storage_write(0x7c00,table,sizeof table));assert(codeplug_aes_kid(1)==42);
    assert(codeplug_aes_reserve(1,p,d));boot();assert(codeplug_aes_reserve(1,p+1,d+1));
    // A stale CPS read uploaded after a transmission cannot roll back the settings reservation.
    assert(storage_write(0x7c00,table,sizeof table));boot();assert(codeplug_aes_reserve(1,p+2,d+2));
    for(unsigned k=0;k<2;k++) {
        uint64_t pm=p[k];uint32_t dm=d[k];
        for(unsigned i=0;i<VOICE_AES_RESERVE;i++){pm=voice_aes_p25_step(pm);dm=voice_aes_dmr_step(dm);}
        assert(pm==p[k+1]&&dm==d[k+1]);
    }
    sim_flash_bad_range(map.settings_addr,map.settings_size);assert(!codeplug_aes_reserve(1,p+3,d+3));
    sim_flash_bad_range(0,0);boot();assert(codeplug_aes_reserve(1,p+3,d+3));assert(p[3]!=p[2]&&d[3]!=d[2]);
    settings_begin()->aes_next_sf=UINT32_MAX-100;settings_end(now);assert(settings_flush());assert(!codeplug_aes_reserve(1,p,d));
    settings_begin()->aes_next_sf=0;settings_end(now);assert(settings_flush());boot();assert(!codeplug_aes_reserve(1,p,d));
    provision(); // fresh provisioning marker (real CPS also chooses a fresh random seed)
    // Produce an all-zero initial AES block to exercise the LFSR fixed-point refusal.
    uint8_t *e=table+16, seed[16]={0};struct AES_ctx ctx;AES_init_ctx(&ctx,e+4);AES_ECB_decrypt(&ctx,seed);
    hal_chip_t uid;chip(&uid);
    for(unsigned i=0;i<12;i++)seed[i]^=(uint8_t)(uid.uid[i/4]>>(8*(i%4)));
    memcpy(e+36,seed,16);crc_entry(e);assert(storage_write(0x7c00,table,sizeof table));assert(!codeplug_aes_reserve(1,p,d));
    provision();e=table+16;memcpy(e+96,e,96);crc_entry(e+96);assert(storage_write(0x7c00,table,sizeof table));assert(!codeplug_aes_lookup(42,key));
    provision();assert(!sim_flash_violations());
}
static bool next(uint8_t ambe[9],void *unused) {(void)unused;memcpy(ambe,frame,9);return true;}
static void receive(const uint8_t ambe[9],void *unused) {(void)unused;assert(!memcmp(ambe,frame,9));heard++;}
static const dmr_voice_source_t source={.next=next};static const dmr_voice_sink_t sink={.frame=receive};
static void run(unsigned count) {for(unsigned i=0;i<count;i++){now+=30;sim_slot();dmr_call_poll(now);}}
static void setup(uint8_t slot,bool tx)
{
    sim_reset();dmr_call_init(&sim_modem);dmr_call_set_voice(&sink,tx?&source:NULL);
    dmr_call_config_t c;dmr_call_config_default(&c);c.own_id=tx?123456:765432;c.talk_target.id=42;c.cc=7;c.ts=1;c.rmo=false;c.access=DMR_ACCESS_IMPOLITE;c.aes_slot=slot;
    dmr_call_configure(&c);assert(dmr_call_start());
}
static void test_dmr(void)
{
    memcpy(table+112,table+16,96);table[114]=43;crc_entry(table+112);
    assert(storage_write(0x7c00,table,sizeof table));
    uint8_t bits[49];for(unsigned i=0;i<49;i++)bits[i]=(uint8_t)(i&1);ambeFecEncode(bits,frame);
    setup(2,true);dmr_call_ptt(true);run(35);dmr_call_ptt(false);run(30);
    air_count=sim.air_count;assert(air_count>10);memcpy(air,sim.air,air_count*sizeof *air);dmr_call_stop();
    unsigned pi=0,voice=0;
    for(unsigned i=0;i<air_count;i++) {
        if(air[i].kind==DMR_BURST_PI_HEADER){pi++;assert(air[i].payload[0]==0x25&&air[i].payload[1]==0x10&&air[i].payload[2]==43);}
        if(air[i].kind==DMR_BURST_VOICE){voice++;assert(pi&&memcmp(air[i].payload,frame,9));}
    }
    assert(pi==1&&voice>=12);
    setup(1,false);heard=0;
    for(unsigned i=0;i<air_count;i++) {
        dmr_burst_t b={.kind=air[i].kind,.voice_index=air[i].voice_index,.len=air[i].len,.crc_ok=true,.cc=7,.ts=1,.sync=DMR_SYNC_MS,.in_step=true};
        memcpy(b.payload,air[i].payload,b.len);now+=60;sim_rx(&b);dmr_call_poll(now);
    }
    assert(heard==voice*3);dmr_call_stop();
    setup(0,false);heard=0;
    for(unsigned i=0;i<air_count;i++) {
        dmr_burst_t b={.kind=air[i].kind,.voice_index=air[i].voice_index,.len=air[i].len,.crc_ok=true,.cc=7,.ts=1,.sync=DMR_SYNC_MS,.in_step=true};
        memcpy(b.payload,air[i].payload,b.len);now+=60;sim_rx(&b);dmr_call_poll(now);
    }
    assert(!heard);dmr_call_stop();
    for(unsigned bad=0;bad<3;bad++) {
        setup(1,false);heard=0;
        for(unsigned i=0;i<air_count;i++) {
            if(bad==0 && air[i].kind==DMR_BURST_PI_HEADER)continue;
            dmr_burst_t b={.kind=air[i].kind,.voice_index=air[i].voice_index,.len=air[i].len,.crc_ok=true,.cc=7,.ts=1,.sync=DMR_SYNC_MS,.in_step=true};
            memcpy(b.payload,air[i].payload,b.len);
            if(b.kind==DMR_BURST_PI_HEADER && bad==1)b.payload[3]^=1;
            if(b.kind==DMR_BURST_VOICE && bad==2)now+=400;
            now+=60;sim_rx(&b);dmr_call_poll(now);
        }
        /* No PI header, or one that fails its CRC: nothing is heard. A spell of arrival delay (bad 2: bursts come in batches,
         * late) does not cost the call its keystream: the burst index tells where in the superframe it is */
        if(bad<2)assert(!heard);else assert(heard==voice*3);
        dmr_call_stop();
    }
    setup(255,true);dmr_call_ptt(true);run(20);assert(!sim.tx_begins);dmr_call_stop();
    sim_flash_bad_range(map.settings_addr,map.settings_size);setup(1,true);dmr_call_ptt(true);run(20);assert(!sim.tx_begins);dmr_call_stop();sim_flash_bad_range(0,0);
}
/* ---- The published AES-256 capture through the call stack (tests/data/dmr_aes256_capture.txt) ----
 * Real voice of the Motorola layout and the key its archive.org item publishes: the LC header, the PI header and the voice
 * bursts reach the stack as the modem hands them over, and the frames the sink gets must be the ones dsd-fme decrypted. */
enum { CAP_MAX = 192 };
static uint8_t cap_pi[12], cap_lc[12], cap_ct[CAP_MAX][7], cap_pt[CAP_MAX][7], cap_key[32];
static unsigned cap_frames;
static uint8_t cap_got[CAP_MAX][49];
static unsigned cap_ngot;
static int unhex(const char *h, uint8_t *o, unsigned n)
{ for (unsigned i = 0; i < n; i++) { unsigned x; if (sscanf(h + 2 * i, "%2x", &x) != 1) return 0; o[i] = (uint8_t)x; } return 1; }
static void bits49(const uint8_t f[7], uint8_t b[49]) { for (unsigned i = 0; i < 49; i++) b[i] = (f[i / 8] >> (7 - i % 8)) & 1; }
static void cap_receive(const uint8_t ambe[9], void *u)
{ ambe_fec_result_t r; (void)u; assert(cap_ngot < CAP_MAX); ambeFecDecode(ambe, cap_got[cap_ngot++], &r); }
static const dmr_voice_sink_t cap_sink = {.frame = cap_receive};
static void load_capture(void)
{
    FILE *f = fopen("tests/data/dmr_aes256_capture.txt", "r"); char line[160];
    assert(f);
    while (fgets(line, sizeof line, f)) {
        char a[64], b[64];
        if (line[0] == '#') continue;
        if (!strncmp(line, "pi ", 3)) assert(unhex(line + 3, cap_pi, 12));
        else if (!strncmp(line, "lc ", 3)) assert(unhex(line + 3, cap_lc, 12));
        else if (sscanf(line, "%63s %63s", a, b) == 2) {
            assert(cap_frames < CAP_MAX && unhex(a, cap_ct[cap_frames], 7) && unhex(b, cap_pt[cap_frames], 7)); cap_frames++;
        }
    }
    fclose(f);
    assert(cap_frames == 180 && cap_pi[0] == 0x25 && cap_pi[1] == 0x10 && cap_pi[2] == 14);
    assert(unhex("3AF8C96E21BEFDE927EE1D03DF53CFA701516B0642AC7B38643549E2BE79840E", cap_key, 32));
}
static void cap_provision(unsigned kid)
{
    memset(table, 0, sizeof table); memcpy(table, "DVAES1\0\0", 8); table[8] = 4; table[9] = 96;
    put32(table + 12, storage_crc32(0, table, 12));
    uint8_t *e = table + 16; e[0] = 1; e[2] = (uint8_t)kid; memcpy(e + 4, cap_key, 32);
    for (unsigned i = 0; i < 16; i++) e[36 + i] = (uint8_t)(0xa0 + i);
    crc_entry(e); assert(storage_write(0x7c00, table, sizeof table));
}
static void cap_pi_fix(uint8_t pi[12])
{ uint16_t crc = dmr_crc_ccitt(pi, 10) ^ 0x6969; pi[10] = (uint8_t)(crc >> 8); pi[11] = (uint8_t)crc; }
static void cap_feed(const dmr_burst_t *b, uint32_t slot)
{ now += 60; if (slot) sim_rx_slot(b, slot); else sim_rx((dmr_burst_t *)b); dmr_call_poll(now); }
/* Hear the capture: pi is the PI header sent (NULL: none) before the voice, drop[b] a burst that never arrives, with the
 * slot counter on (slots) or the arrival time alone; fade_ms the stack's call fade. Returns the frames compared; a
 * frame that came out other than the capture's counts in *bad. The frames of a dropped burst are skipped, in the capture
 * and in what the sink got. */
static unsigned cap_play(const uint8_t *pi, const bool *drop, bool slots, unsigned fade_ms, unsigned *bad)
{
    cap_ngot = 0; sim_reset(); dmr_call_init(&sim_modem); dmr_call_set_voice(&cap_sink, NULL);
    dmr_call_config_t c; dmr_call_config_default(&c);
    c.own_id = 765432; c.talk_target.id = 1; c.cc = 1; c.ts = 1; c.rmo = false; c.access = DMR_ACCESS_IMPOLITE; c.aes_slot = 1;
    if (fade_ms) c.fade_ms = (uint16_t)fade_ms;
    dmr_call_configure(&c); assert(dmr_call_start());
    dmr_burst_t h = {.kind = DMR_BURST_VOICE_LC_HEADER, .len = 12, .crc_ok = true, .cc = 1, .ts = 1, .sync = DMR_SYNC_MS, .in_step = true, .lc_valid = true};
    memcpy(h.payload, cap_lc, 12); memcpy(h.lc, cap_lc, 9);
    cap_feed(&h, 0); cap_feed(&h, 0);
    if (pi) {
        dmr_burst_t p = {.kind = DMR_BURST_PI_HEADER, .len = 12, .crc_ok = true, .cc = 1, .ts = 1, .sync = DMR_SYNC_MS, .in_step = true};
        memcpy(p.payload, pi, 12); cap_feed(&p, 0);
    }
    unsigned compared = 0, expect[CAP_MAX], n = 0; uint32_t slot = 1000;
    *bad = 0;
    for (unsigned b = 0; b < cap_frames / 3; b++) {
        slot += 2;
        if (drop && drop[b]) { now += 60; continue; }
        dmr_burst_t v = {.kind = DMR_BURST_VOICE, .voice_index = (uint8_t)(b % 6), .len = 27, .crc_ok = true, .cc = 1, .ts = 1, .sync = DMR_SYNC_MS, .in_step = true};
        for (unsigned k = 0; k < 3; k++) { uint8_t b49[49]; bits49(cap_ct[b * 3 + k], b49); ambeFecEncode(b49, v.payload + 9 * k); expect[n++] = b * 3 + k; }
        cap_feed(&v, slots ? slot : 0);
    }
    if (cap_ngot > n) cap_ngot = n;
    for (unsigned i = 0; i < cap_ngot; i++) {
        uint8_t want[49]; bits49(cap_pt[expect[i]], want); compared++;
        if (memcmp(cap_got[i], want, 49)) (*bad)++;
    }
    return compared;
}
static void test_capture(void)
{
    unsigned bad, got;
    bool drop[CAP_MAX / 3] = {0};
    uint8_t pi[12];
    load_capture();
    sim_flash_create(0x40000, &map); boot(); cap_provision(14);
    /* The whole call: every frame is the one dsd-fme decrypted; the view says what the screen needs */
    got = cap_play(cap_pi, NULL, false, 0, &bad);
    assert(got == 180 && !bad);
    const dmr_caller_t *cv = &dmr_call_view()->caller;
    assert(cv->decrypted && cv->encrypted && cv->pi_header && cv->aes == DMR_AES_OK && cv->pi_alg == 0x25 && cv->pi_fid == 0x10 && cv->pi_key == 14);
    got = cap_play(cap_pi, NULL, true, 0, &bad); assert(got == 180 && !bad);
    /* Bursts that never arrive. 5 in a row is 300 ms: the call still holds (its fade is 390 ms), and a stream that gave up
     * after 300 ms of silence lost the rest of the call */
    memset(drop, 0, sizeof drop); drop[7] = drop[19] = drop[20] = 1; for (unsigned b = 31; b <= 35; b++) drop[b] = 1;
    for (unsigned slots = 0; slots < 2; slots++) {
        got = cap_play(cap_pi, drop, slots, 0, &bad);
        assert(got == (cap_frames / 3 - 8) * 3 && !bad);
    }
    /* A fade of whole superframes, with the call held through it (a longer call fade than the stack's 390 ms): 6, 8 and 13
     * bursts. Only the modem's slot counter can tell how many superframes went by; with it the keystream follows (without
     * one it follows what the burst index shows: the wrap, and the same index again, as with the 5 lost above) */
    memset(drop, 0, sizeof drop); for (unsigned b = 7; b <= 12; b++) drop[b] = 1; for (unsigned b = 24; b <= 31; b++) drop[b] = 1;
    for (unsigned b = 40; b <= 52; b++) drop[b] = 1;
    got = cap_play(cap_pi, drop, true, 4000, &bad);
    assert(got == (cap_frames / 3 - 27) * 3 && !bad);
    /* The PI header: a repeat, the destination it names (not compared with the call's), AES-256 as 0x05 */
    memset(drop, 0, sizeof drop);
    memcpy(pi, cap_pi, 12); pi[7] = 0x12; pi[8] = 0x34; pi[9] = 0x56; cap_pi_fix(pi);
    got = cap_play(pi, NULL, false, 0, &bad); assert(got == 180 && !bad);
    memcpy(pi, cap_pi, 12); pi[0] = 0x05; cap_pi_fix(pi);
    got = cap_play(pi, NULL, false, 0, &bad); assert(got == 180 && !bad && dmr_call_view()->caller.aes == DMR_AES_OK);
    /* What stops the call, and what the view says of it */
    memcpy(pi, cap_pi, 12); pi[1] = 0x68; cap_pi_fix(pi);
    got = cap_play(pi, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.aes == DMR_AES_FID && dmr_call_view()->caller.pi_fid == 0x68);
    memcpy(pi, cap_pi, 12); pi[0] = 0x24; cap_pi_fix(pi);
    got = cap_play(pi, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.aes == DMR_AES_ALGO && dmr_call_view()->caller.pi_alg == 0x24);
    memcpy(pi, cap_pi, 12); pi[2] = 15; cap_pi_fix(pi);
    got = cap_play(pi, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.aes == DMR_AES_NOKEY && dmr_call_view()->caller.pi_key == 15);
    memcpy(pi, cap_pi, 12); pi[3] = pi[4] = pi[5] = pi[6] = 0; cap_pi_fix(pi);
    got = cap_play(pi, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.aes == DMR_AES_BAD);
    got = cap_play(NULL, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.encrypted && dmr_call_view()->caller.aes == DMR_AES_NONE);
    memcpy(pi, cap_pi, 12); pi[11] ^= 1;
    got = cap_play(pi, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.aes == DMR_AES_NONE);
    /* The key under another key ID: that call has no key */
    cap_provision(15);
    got = cap_play(cap_pi, NULL, false, 0, &bad); assert(!got && dmr_call_view()->caller.aes == DMR_AES_NOKEY);
    dmr_call_stop();
}
int main(void) {test_store();test_dmr();test_capture();sim_flash_free();puts("PASS journaled AES reservations, CPS rollback, reboot, write failure, zero MI, DMR PI, encrypted TX/RX and a real AES-256 capture");}
