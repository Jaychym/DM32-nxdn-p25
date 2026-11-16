/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice_aes.h"
#include "p25.h"

static void unhex(const char *hex, uint8_t *out, unsigned n)
{ for (unsigned i = 0; i < n; i++) { unsigned x; assert(sscanf(hex + 2 * i, "%2x", &x) == 1); out[i] = (uint8_t)x; } }
int main(void)
{
    uint8_t key[32], block[16], answer[16]; struct AES_ctx aes;
    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    unhex("00112233445566778899aabbccddeeff", block, 16);
    unhex("8ea2b7ca516745bfeafc49904b496089", answer, 16);
    AES_init_ctx(&aes, key); AES_ECB_encrypt(&aes, block); assert(!memcmp(block, answer, 16));
    assert(voice_aes_p25_step(0x1234567890abcdefULL) == 0x6fe2802aa403828bULL);
    assert(voice_aes_p25_step(0x6fe2802aa403828bULL) == 0xff35b1909449b835ULL);
    unhex("0123456789abcdef23456789abcdef01456789abcdef01236789abcdef012345", key, 32);
    voice_aes s;
    voice_aes_p25_start(&s, key, 0x1234567890abcdefULL);
    unhex("a4a452206523e33bad358af571cb029a", answer, 16); assert(!memcmp(s.stream, answer, 16));
    unhex("93aeff9bdb7b317fb85b09f548d7ce61", answer, 16); assert(!memcmp(s.stream + 16, answer, 16));
    for (unsigned i = 0; i < 18; i++) {
        uint16_t u[8] = {0x123,0x456,0x789,0xabc,0x345,0x678,0x123,0x55}, original[8];
        memcpy(original, u, sizeof u); voice_aes_p25_frame(&s, i, u);
        assert(memcmp(original, u, sizeof u)); voice_aes_p25_frame(&s, i, u); assert(!memcmp(original, u, sizeof u));
    }
    p25_call call = {.nac=0x293,.talkgroup=42,.source_id=123456,.service_options=0x40};
    p25_crypto_sync sync = {.algorithm_id=0x84,.key_id=123,.message_indicator={0x12,0x34,0x56,0x78,0x90,0xab,0xcd,0xef,0}};
    uint8_t air[216]; p25_decoded d;
    assert(p25_encode_hdu(&call, air) == -1);
    assert(!p25_encode_hdu_aes(&call, &sync, air)); assert(!p25_decode_frame(air, 99, &d));
    assert(d.metadata_valid && !d.clear_voice && d.algorithm_id==0x84 && d.key_id==123 && !memcmp(d.message_indicator, sync.message_indicator,9));
    uint16_t frames[9][8] = {{0}}; uint8_t lsd[2] = {1,2};
    for (unsigned i=0;i<9;i++) { frames[i][7]=(uint16_t)(i&1); voice_aes_p25_frame(&s,i,frames[i]); }
    assert(!p25_encode_ldu_aes(&call,&sync,P25_DUID_LDU1,frames,lsd,air));
    assert(!p25_decode_frame(air,216,&d)); assert(d.metadata_valid && !d.clear_voice);
    assert(!memcmp(frames,d.imbe,sizeof frames)); // ciphertext IMBE sync bit must survive FEC encoding
    assert(!p25_encode_ldu_aes(&call,&sync,P25_DUID_LDU2,frames,lsd,air));
    assert(!p25_decode_frame(air,216,&d)); assert(d.algorithm_id==0x84 && d.key_id==123 && !memcmp(d.message_indicator,sync.message_indicator,9));
    voice_aes rx; uint8_t bits[3][49]={{0}}, plain[3][49]={{0}};
    voice_aes_dmr_start(&s,key,0x12345678); voice_aes_dmr_start(&rx,key,0x12345678);
    /* Pinned dmrgo IV expansion + Go crypto/cipher OFB, synthetic key/MI. */
    unhex("5edd6bed2923120ed87680bd217a4ea4", answer, 16); assert(!memcmp(s.stream, answer, 16));
    for (unsigned burst=0;burst<20;burst++) {
        unsigned index=burst%6; memset(bits,0,sizeof bits);
        assert(voice_aes_dmr_burst(&s,index,bits)); assert(memcmp(bits,plain,sizeof bits));
        uint8_t retry[3][49];memcpy(retry,bits,sizeof retry);
        memset(bits,0,sizeof bits);assert(voice_aes_dmr_burst(&s,index,bits));assert(!memcmp(bits,retry,sizeof bits));
        assert(voice_aes_dmr_burst(&rx,index,bits)); assert(!memcmp(bits,plain,sizeof bits));
    }
    voice_aes_wipe(&s,sizeof s);assert(!voice_aes_dmr_burst(&s,0,bits));
    puts("PASS AES-256 primitive, P25 MI/OFB vectors, encrypted CAI/FEC, DMR superframes and retries");
    return 0;
}
