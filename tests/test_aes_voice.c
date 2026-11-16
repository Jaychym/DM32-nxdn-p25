/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice.h"
static p25_voice tx,rx;
static uint8_t air[2304]; static unsigned samples,nonzero; static uint32_t hash;
static void sink(void *ctx,const int16_t pcm[160])
{
    (void)ctx;samples+=160;
    for(unsigned i=0;i<160;i++){hash=(hash^(uint16_t)pcm[i])*16777619u;nonzero+=pcm[i]!=0;}
}
static unsigned build(bool encrypted)
{
    uint8_t key[32];for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
    p25_call c={.nac=0x293,.talkgroup=42,.source_id=123456};
    p25_voice_init(&tx,0x293,42,NULL,NULL);
    if(encrypted){p25_voice_set_aes(&tx,key,42);assert(!p25_voice_tx_begin(&tx,&c));assert(p25_voice_tx_begin_aes(&tx,&c,0x1234567890abcdefULL));}
    else assert(p25_voice_tx_begin(&tx,&c));
    for(unsigned frame=0;frame<18;frame++) {
        int16_t pcm[160]; for(unsigned i=0;i<160;i++)pcm[i]=((i+frame*160)%20<10)?2000:-2000;
        assert(p25_voice_tx_pcm(&tx,pcm));
    }
    assert(p25_voice_tx_end(&tx));
    unsigned n=(unsigned)p25_voice_tx_read(&tx,air,sizeof air);
    assert(n==99*4+216*8+18*4 && p25_voice_tx_done(&tx));return n;
}
static uint32_t hear(unsigned n,bool aes,unsigned kid,unsigned skip)
{
    uint8_t key[32];for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
    p25_voice_init(&rx,0x293,42,sink,NULL);
    if(aes)p25_voice_set_aes(&rx,key,(uint16_t)kid);
    samples=nonzero=0;hash=2166136261u;
    for(unsigned i=skip;i<n;i++)p25_voice_rx_dibit(&rx,air[i]);
    return hash;
}
int main(void)
{
    unsigned n=build(false);uint32_t clear=hear(n,false,0,0);assert(samples==18*160 && nonzero);
    hear(n,true,42,0);assert(!nonzero && rx.rx_muted==18); // secure channel does not admit clear audio
    n=build(true);uint32_t encrypted=hear(n,true,42,0);
    assert(samples==18*160 && nonzero && clear==encrypted && !rx.rx_muted);
    hear(n,false,0,0);assert(!nonzero && rx.rx_muted==18);
    hear(n,true,43,0);assert(!nonzero && rx.rx_muted==18);
    hear(n,true,42,99*4);assert(!nonzero && rx.rx_muted==18); // late entry waits for the next LDU1
    puts("PASS real IMBE AES-256 TX/RX, no-key/wrong-KID mute, secure RX and late-entry gating");
    return 0;
}
