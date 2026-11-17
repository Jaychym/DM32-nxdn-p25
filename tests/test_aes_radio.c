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
        assert(!heard);dmr_call_stop();
    }
    setup(255,true);dmr_call_ptt(true);run(20);assert(!sim.tx_begins);dmr_call_stop();
    sim_flash_bad_range(map.settings_addr,map.settings_size);setup(1,true);dmr_call_ptt(true);run(20);assert(!sim.tx_begins);dmr_call_stop();sim_flash_bad_range(0,0);
}
int main(void) {test_store();test_dmr();sim_flash_free();puts("PASS journaled AES reservations, CPS rollback, reboot, write failure, zero MI, DMR PI and encrypted TX/RX");}
