/* Real PCM decoder/mixer with a silent test output backend. */
#include "sound/sound_internal.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);exit(1);}}while(0)
int snd_backend_start(void) {return 1;}
void snd_backend_stop(void) {}
void snd_backend_pause(void) {}
void snd_backend_resume(void) {}
void snd_set_java_vm(void *vm) {(void)vm;}
int main(void) {
    CHECK(audio_init(host_asset_manager("assets")));
    snd_set_volume(100);   /* test the mixer's raw clipping at full volume */
    CHECK(snd_load("jump.wav") && snd_load("break.wav") && snd_load("place.wav"));
    CHECK(snd_load("jump.wav"));CHECK(!snd_load("../jump.wav"));CHECK(!snd_load("missing.wav"));
    CHECK(!snd_play("missing.wav"));
    int16_t out[2056];for(int i=0;i<2056;i++)out[i]=0x1234;
    for(int i=0;i<16;i++)CHECK(snd_play("jump.wav"));
    snd_frame(out,1024);int nonzero=0,clipped=0;
    for(int i=0;i<1024;i++){CHECK(out[i*2]==out[i*2+1]);nonzero+=out[i*2]!=0;clipped+=abs(out[i*2])>=32767;}
    CHECK(nonzero>900 && clipped>0);for(int i=2048;i<2056;i++)CHECK(out[i]==0x1234);
    CHECK(snd_play("break.wav") && snd_play("place.wav"));
    for(int i=0;i<20;i++)snd_frame(out,1024);
    for(int i=0;i<2048;i++)CHECK(out[i]==0);
    snd_frame(NULL,0);audio_pause();audio_resume();audio_shutdown();CHECK(!snd_play("jump.wav"));
    CHECK(audio_init(host_asset_manager("assets")));CHECK(snd_load("jump.wav"));audio_shutdown();
    puts("PASS authored PCM16 effects, one-shot completion, bounded eight-voice clipping, output bounds and audio reinitialization");
    return 0;
}
