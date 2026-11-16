/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native controller + real DMR voice/FEC, with a delayed P25 worker and PCM driver. */
#define main native_fixture_main
#define board native_fixture_board
#define NATIVE_CHANNEL_TEST
#define NATIVE_AUDIO_HANDOFF_TEST
#include "test_lab.c"
#undef main
#undef board
#include "dmr/ambe/ambeVoice.h"
#include "dmr/ambe/ambeFec.h"

static audio_block_cb_t audio_cb;
static void *audio_ctx;
static uint32_t worker_sid;
static unsigned cleanup_left,waits,encoded;
static bool p25_tx,mic_native;
static uint8_t expected[VOCODER_BITS];

static bool audio_start(audio_block_cb_t cb,void *ctx)
{ audio_cb=cb;audio_ctx=ctx;return true; }
static void audio_stop(void) { audio_cb=NULL; }
static void speaker_pcm(bool on) { (void)on; }
static void mic_route(bool on) { mic_native=on; }
static const hal_audio_t audio={.start=audio_start,.stop=audio_stop,.speaker_pcm=speaker_pcm};
static bool present(void) { return true; }
static void reset(void) { }
static void encode(const int16_t pcm[VOCODER_FRAME_SAMPLES],uint8_t bits[VOCODER_BITS])
{
    assert(!mic_native && pcm[0]==1200 && pcm[1]==-1200);
    for(unsigned i=0;i<VOCODER_BITS;i++)bits[i]=expected[i]=(uint8_t)(i%3==0);
    encoded++;
}
static void decode(const uint8_t bits[VOCODER_BITS],int16_t pcm[VOCODER_FRAME_SAMPLES])
{ (void)bits;memset(pcm,0,VOCODER_FRAME_SAMPLES*sizeof *pcm); }
static const hal_vocoder_t vocoder={.present=present,.reset=reset,.encode=encode,.decode=decode};
const board_t board={.native_modem=&ops,.power=&power,.battery_critical_mv=6500,
                     .audio=&audio,.vocoder=&vocoder,.native_mic_route=mic_route};

static void p25_audio(const int16_t *mic,int16_t *spk,void *ctx)
{ (void)mic;(void)spk;(void)ctx; }
int p25_lab_channel_rx(uint32_t hz)
{
    worker_sid=native_lab_voice_start(hz,0);assert(worker_sid);
    busy=1;p25_tx=false;audio_start(p25_audio,NULL);return 1;
}
int p25_lab_live_begin(uint32_t sid)
{ worker_sid=sid;busy=1;p25_tx=true;mic_route(true);audio_start(p25_audio,NULL);return 1; }
void p25_lab_live_up(void) { }
void vTaskDelay(unsigned ticks)
{
    assert(!depth && ticks==1);clock_us+=1000;waits++;
    /* The P25 worker yields while finishing a decode/encode before cleanup. */
    if(busy && cleanup_left && !--cleanup_left) {
        assert(live && !live->owned && live->state==NT_STOPPED);
        board.audio->stop();board.audio->speaker_pcm(false);
        if(p25_tx)board.native_mic_route(false);
        assert(native_lab_voice_release(worker_sid));busy=0;
    }
}
int main(void)
{
    native_channel ch={145500000,145500000,0};
    for(unsigned tx=0;tx<2;tx++)for(unsigned repeat=0;repeat<50;repeat++) {
        fresh();ambeVoiceInit(&audio,&vocoder);ambeVoiceResetStats();waits=encoded=0;mic_native=false;
        sm.view.ch.mode=RADIO_MODE_P25;
        assert(native_lab_channel(&ch));
        native_lab_service(&sm,0);native_lab_service(&sm,0);
        assert(live->state==NT_RX && busy && audio_cb==p25_audio);
        if(tx) {
            assert(native_lab_channel_ptt(true));native_lab_service(&sm,0);
            cleanup_left=3;while(busy)vTaskDelay(1);
            native_lab_service(&sm,0);
            assert(busy && p25_tx && mic_native);
        }
        cleanup_left=3;unsigned before=waits;
        assert(native_lab_channel(NULL));
        const dmr_voice_source_t *source=ambeVoiceSource();source->begin(source->ctx);
        /* If channel leave returned early, the old worker stops this new stream. */
        for(unsigned ms=0;ms<20;ms++)vTaskDelay(1);
        int16_t mic[VOCODER_FRAME_SAMPLES],spk[VOCODER_FRAME_SAMPLES];
        for(unsigned i=0;i<VOCODER_FRAME_SAMPLES;i++)mic[i]=(int16_t)(i&1?-1200:1200);
        if(audio_cb)audio_cb(mic,spk,audio_ctx);
        uint8_t frame[AMBE_FRAME_BYTES],bits[VOCODER_BITS];ambe_fec_result_t fec;
        assert(source->next(frame,source->ctx) && encoded==1);
        ambeFecDecode(frame,bits,&fec);assert(!fec.c0_failed && !memcmp(bits,expected,sizeof bits));
        ambe_voice_stats_t stats;ambeVoiceGetStats(&stats);
        assert(stats.tx_frames==1 && stats.tx_mic_peak==1200);
        assert(waits==before+23 && !busy && !native_lab_reserved());
        source->end(source->ctx);ambeVoiceStop();assert(!audio_cb && !depth);
    }
    puts("P25 RX/TX -> DMR: delayed worker cleanup precedes first PTT mic voice, 100 transitions PASS");
}
