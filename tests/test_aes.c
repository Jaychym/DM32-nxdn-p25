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
    /* The next-MI steps and the IV below were computed by OP25's own op25_crypt_algs::cycle_p25_mi / expand_mi_to_128
     * (boatbod/op25 at 3f44d43, the commit sources.lock.json pins); tests/protocol/test_p25.py checks the firmware
     * against the live OP25 code for random keys and MIs. */
    assert(voice_aes_p25_step(0x1234567890abcdefULL) == 0x6fe2802aa403828bULL);
    assert(voice_aes_p25_step(0x6fe2802aa403828bULL) == 0xff35b1909449b835ULL);
    unhex("0123456789abcdef23456789abcdef01456789abcdef01236789abcdef012345", key, 32);
    voice_aes s;
    voice_aes_p25_start(&s, key, 0x1234567890abcdefULL);
    unhex("a4a452206523e33bad358af571cb029a", answer, 16); assert(!memcmp(s.stream, answer, 16));
    unhex("93aeff9bdb7b317fb85b09f548d7ce61", answer, 16); assert(!memcmp(s.stream + 16, answer, 16));
    {
        /* What OP25 XORs into each of the 18 IMBE frames of the superframe for this key and MI (OP25's AES-256 process() on
         * all-zero frames: 11 bytes a frame, the 88 bits u0..u7 packed MSB first). Interoperability with other receivers
         * rests on this table, not on the firmware agreeing with itself. */
        static const char *const op25_mask[18] = {
            "f571cb029a93aeff9bdb7b", "317fb85b09f548d7ce6193", "144ce08f62a453abe5e540",
            "c10a3e2f54f7692d707ebf", "dfe31ccf53509c890c30c3", "3c9dd5d2671c98b57138bf",
            "05ae2f2250d471b0f2c337", "873be6db55a512f359c9a8", "173eaf102f8aa448c5d899",
            "b1511bb4e71228410744a3", "89da845232d10771333120", "7577848f2626a4aa396a6f",
            "b9af2090cf197ceee18570", "d381089575f6fa21aa6247", "8025feceda2a8c8f72f360",
            "2c95e8a6bc8b450969574c", "e8f7b5321e62fab980355b", "f396bcc07e65b93586017f",
        };
        for (unsigned f = 0; f < 18; f++) {
            uint16_t u[8] = {0};
            uint8_t packed[11] = {0}, want[11];
            unsigned bit = 0;
            voice_aes_p25_frame(&s, f, u);
            for (unsigned w = 0; w < 8; w++) {
                unsigned width = w < 4 ? 12 : w < 7 ? 11 : 7;
                for (unsigned j = 0; j < width; j++, bit++)
                    packed[bit / 8] |= (uint8_t)(((u[w] >> (width - 1 - j)) & 1u) << (7 - bit % 8));
            }
            unhex(op25_mask[f], want, 11);
            assert(!memcmp(packed, want, 11));
        }
    }
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
    /* The ninth byte of the 72-bit MI is outside the 64 bits the IV uses; a radio may send it, and the frames carry it as given */
    sync.message_indicator[8]=0x5a;
    assert(!p25_encode_hdu_aes(&call,&sync,air)); assert(!p25_decode_frame(air,99,&d));
    assert(d.metadata_valid && d.message_indicator[8]==0x5a && !memcmp(d.message_indicator,sync.message_indicator,9));
    assert(!p25_encode_ldu_aes(&call,&sync,P25_DUID_LDU2,frames,lsd,air)); assert(!p25_decode_frame(air,216,&d));
    assert(d.metadata_valid && d.message_indicator[8]==0x5a && !memcmp(d.message_indicator,sync.message_indicator,9));
    sync.message_indicator[8]=0;
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
    {   /* Superframes that went by without a burst (a fade): advance() lands on the keystream of the superframe that many on */
        voice_aes a,b;uint8_t t[3][49],want[3][49];
        voice_aes_dmr_start(&a,key,0x12345678);voice_aes_dmr_start(&b,key,0x12345678);
        for(unsigned sf=0;sf<4;sf++)for(unsigned burst=0;burst<6;burst++){
            memset(t,0,sizeof t);assert(voice_aes_dmr_burst(&a,burst,t));
            if(sf==3&&burst==2)memcpy(want,t,sizeof want);
        }
        memset(t,0,sizeof t);assert(voice_aes_dmr_burst(&b,2,t));
        assert(voice_aes_dmr_advance(&b,0)&&voice_aes_dmr_advance(&b,3));
        memset(t,0,sizeof t);assert(voice_aes_dmr_burst(&b,2,t));assert(!memcmp(t,want,sizeof want));
        assert(!voice_aes_dmr_advance(&b,VOICE_AES_RESERVE)&&!voice_aes_dmr_burst(&b,3,t)); // past the budget the stream stops
    }
    voice_aes_wipe(&s,sizeof s);assert(!voice_aes_dmr_burst(&s,0,bits));
    puts("PASS AES-256 primitive, P25 MI/OFB vectors, encrypted CAI/FEC, DMR superframes and retries");
    return 0;
}
