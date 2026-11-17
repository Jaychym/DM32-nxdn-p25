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
/* The dibits [from,stop) of the call on air; the key is 0,1,2.. xor keyxor. A stop before the terminator leaves the call
 * as the screen sees it while it runs. */
static uint32_t hear_ex(unsigned from,unsigned stop,bool aes,unsigned kid,unsigned keyxor)
{
    uint8_t key[32];for(unsigned i=0;i<32;i++)key[i]=(uint8_t)(i^keyxor);
    p25_voice_init(&rx,0x293,42,sink,NULL);
    if(aes)p25_voice_set_aes(&rx,key,(uint16_t)kid);
    samples=nonzero=0;hash=2166136261u;
    for(unsigned i=from;i<stop;i++)p25_voice_rx_dibit(&rx,air[i]);
    return hash;
}
static uint32_t hear(unsigned n,bool aes,unsigned kid,unsigned skip){return hear_ex(skip,n,aes,kid,0);}
/* The key store the radio looks keys up in by the call's key ID: the test key for KID 42 only */
static bool lookup42(uint16_t kid,uint8_t key[32])
{
    if(kid!=42)return false;
    for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
    return true;
}
static bool lookup_none(uint16_t kid,uint8_t key[32]){(void)kid;(void)key;return false;}
/* The frames of the call as the channel decoder hands them over, to patch what the encoders cannot make */
enum { HDU_AT=0, HDU_BYTES=99, LDU1_AT=99*4, LDU2_AT=99*4+216*4, TDU_BYTES=18, LDU_BYTES=216 };
static void decoded(unsigned at,unsigned bytes,p25_decoded *d)
{
    uint8_t frame[216]={0};
    for(unsigned i=0;i<bytes*4;i++)frame[i/4]|=(uint8_t)(air[at+i]<<(6-2*(i%4)));
    assert(!p25_decode_frame(frame,bytes,d));
}
/* Feed copies: the receiver decrypts the IMBE words it is given in place, and a template must stay as decoded */
static void feed(const p25_decoded *h,const p25_decoded *a,const p25_decoded *b)
{
    p25_decoded x;
    if(h){x=*h;p25_voice_rx_decoded(&rx,&x);}
    if(a){x=*a;p25_voice_rx_decoded(&rx,&x);}
    if(b){x=*b;p25_voice_rx_decoded(&rx,&x);}
}
static void fresh(bool aes,unsigned kid)
{
    uint8_t key[32];for(unsigned i=0;i<32;i++)key[i]=(uint8_t)i;
    p25_voice_init(&rx,0x293,42,sink,NULL);
    if(aes)p25_voice_set_aes(&rx,key,(uint16_t)kid);
    samples=nonzero=0;hash=2166136261u;
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

    /* What the screen is told about the call while it runs (the terminator ends it and clears all of this) */
    unsigned live=n-TDU_BYTES*4;
    hear_ex(0,live,true,42,0);
    assert(rx.enc_state==P25_ENC_OK && rx.enc_algid==0x84 && rx.enc_kid==42 && rx.playing && rx.encrypted);
    assert(rx.enc_seen==18 && !rx.enc_bad); // every clean frame decoded: a right key never gives the vocoder a bad pitch
    hear_ex(0,n,true,42,0);
    assert(rx.enc_state==P25_ENC_NONE && !rx.encrypted && !rx.enc_algid && !rx.enc_kid);
    hear_ex(0,live,false,0,0);
    assert(rx.enc_state==P25_ENC_OFF && rx.enc_algid==0x84 && rx.enc_kid==42 && rx.encrypted && !rx.playing);
    hear_ex(0,live,true,43,0);
    assert(rx.enc_state==P25_ENC_NOKEY && rx.enc_kid==42 && !rx.playing);
    hear_ex(99*4,live,true,42,0);
    assert(rx.enc_state==P25_ENC_SYNC && rx.encrypted && !rx.playing); // late entry: the sync comes with the next LDU2
    /* The right KID with another key: decrypted, so it plays, but as noise. About one frame in five then has a reserved
     * pitch (a clear one never does), so the 18 clean frames of a call flag it three times in four; these two keys do. */
    hear_ex(0,live,true,42,11);
    assert(rx.playing && rx.enc_seen==18 && rx.enc_bad>=3 && rx.enc_state==P25_ENC_BAD);
    hear_ex(0,live,true,42,8);
    assert(rx.playing && rx.enc_seen==18 && rx.enc_bad>=3 && rx.enc_state==P25_ENC_BAD);
    /* The radio looks the key up by KID; with the right key stored, the channel's own key slot need not be this one */
    fresh(true,7);rx.aes_lookup=lookup42;
    for(unsigned i=0;i<live;i++)p25_voice_rx_dibit(&rx,air[i]);
    assert(rx.enc_state==P25_ENC_OK && rx.playing && rx.aes_kid==42);
    fresh(true,7);rx.aes_lookup=lookup_none;
    for(unsigned i=0;i<live;i++)p25_voice_rx_dibit(&rx,air[i]);
    assert(rx.enc_state==P25_ENC_NOKEY && rx.enc_kid==42 && !rx.playing);

    {   /* The same call as decoded frames, patched into what real radios may send */
        p25_decoded hdu,ldu1,ldu2;
        decoded(HDU_AT,HDU_BYTES,&hdu);decoded(LDU1_AT,LDU_BYTES,&ldu1);decoded(LDU2_AT,LDU_BYTES,&ldu2);
        assert(hdu.metadata_valid && ldu1.metadata_valid && ldu2.metadata_valid && !hdu.message_indicator[8]);
        p25_decoded h=hdu,a=ldu1,b=ldu2;
        fresh(true,42);feed(&h,&a,&b);
        assert(samples==18*160 && hash==encrypted && rx.enc_state==P25_ENC_OK); // the decoded path is the dibit path
        /* The ninth byte of the 72-bit MI is not part of the IV: a radio that sends it is still heard */
        h=hdu;a=ldu1;b=ldu2;h.message_indicator[8]=0x5a;b.message_indicator[8]=0xa5;
        fresh(true,42);feed(&h,&a,&b);
        assert(samples==18*160 && hash==encrypted && !rx.rx_muted && rx.enc_state==P25_ENC_OK);
        /* ... and the MI itself still has to be there */
        h=hdu;a=ldu1;b=ldu2;memset(h.message_indicator,0,8);h.message_indicator[8]=0x5a;
        fresh(true,42);feed(&h,&a,&b);
        assert(!nonzero && rx.enc_state==P25_ENC_SYNC);
        /* A unit ID is 24 bits on the air: an eight-digit one (up to 16777215) plays like a seven-digit one */
        h=hdu;a=ldu1;b=ldu2;a.source_id=16777000;
        fresh(true,42);feed(&h,&a,&b);
        assert(samples==18*160 && hash==encrypted && rx.active_source==16777000 && rx.enc_state==P25_ENC_OK);
        /* Another algorithm is named, and never decrypted, whatever key the channel holds */
        h=hdu;a=ldu1;b=ldu2;h.algorithm_id=b.algorithm_id=0x81;
        fresh(true,42);feed(&h,&a,&b);
        assert(!nonzero && rx.enc_state==P25_ENC_ALGO && rx.enc_algid==0x81 && rx.encrypted);
        fresh(false,0);feed(&h,&a,&b);
        assert(!nonzero && rx.enc_state==P25_ENC_ALGO && rx.enc_algid==0x81); // with no key slot too
        /* The encryption sync decides, not the link control: an LDU1 whose LC says the call is not protected, is damaged or is
         * of a kind not read here (another format, another manufacturer) still decrypts when the header armed the call, and
         * the call keeps the group the header gave it */
        h=hdu;a=ldu1;b=ldu2;a.clear_voice=1;
        fresh(true,42);feed(&h,&a,&b);
        assert(samples==18*160 && hash==encrypted && rx.enc_state==P25_ENC_OK);
        a=ldu1;a.metadata_valid=0;a.link_control_format=a.manufacturer_id=a.service_options=0;a.talkgroup=0;a.source_id=0;
        fresh(true,42);feed(&h,&a,&b);
        assert(samples==18*160 && hash==encrypted && rx.enc_state==P25_ENC_OK && rx.active_group==42);
        a=ldu1;a.metadata_valid=0;a.link_control_format=3;a.manufacturer_id=0x90;a.talkgroup=0;a.source_id=0;
        fresh(true,42);feed(&h,&a,&b);
        assert(samples==18*160 && hash==encrypted && rx.enc_state==P25_ENC_OK && rx.active_group==42);
        /* ... but an LDU1 that is another call's (its LC names another group or caller) is not decrypted with this one's keystream */
        a=ldu1;a.talkgroup=43;
        fresh(true,42);feed(&h,&a,NULL);
        assert(!nonzero && rx.enc_state==P25_ENC_SYNC);
        /* With no group from the header either there is nothing to carry the call on with: the link control is what refused it */
        h=hdu;h.talkgroup=0;a=ldu1;a.metadata_valid=0;a.link_control_format=a.manufacturer_id=a.service_options=0;a.talkgroup=0;a.source_id=0;
        fresh(true,42);feed(&h,&a,NULL);
        assert(!nonzero && rx.enc_state==P25_ENC_LC && rx.encrypted && !rx.playing);
        /* A clear call shows nothing */
        n=build(false);
        decoded(HDU_AT,HDU_BYTES,&h);decoded(LDU1_AT,LDU_BYTES,&a);decoded(LDU2_AT,LDU_BYTES,&b);
        fresh(false,0);feed(&h,&a,&b);
        assert(rx.enc_state==P25_ENC_NONE && !rx.encrypted && rx.enc_seen==0);
    }
    puts("PASS real IMBE AES-256 TX/RX, no-key/wrong-KID mute, secure RX, late-entry gating and the call states the screen shows");
    return 0;
}
