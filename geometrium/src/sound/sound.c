/* Short PCM16 effects; one-shot mixer, no music or interpreter API. */
#include "sound_internal.h"
#include <pthread.h>
#include <stdio.h>

enum { MAX_SOUNDS=8, MAX_VOICES=8 };
typedef struct { char name[48]; int16_t *samples; size_t frames; int channels,rate; } Sound;
typedef struct { Sound *sound; double position; } Voice;
static Sound sounds[MAX_SOUNDS];
static Voice voices[MAX_VOICES];
static AAssetManager *assets;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned u16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

int snd_load(const char *name) {
    if (!name || strlen(name)>=sizeof(sounds[0].name) || strchr(name,'/') || strchr(name,'\\')) return 0;
    pthread_mutex_lock(&lock);
    int slot=-1;
    for (int i=0;i<MAX_SOUNDS;i++) {
        if (!strcmp(sounds[i].name,name)) { pthread_mutex_unlock(&lock); return 1; }
        if (!sounds[i].samples && slot<0) slot=i;
    }
    pthread_mutex_unlock(&lock);
    if (slot<0) return 0;
    char path[80]; snprintf(path,sizeof(path),"sounds/%s",name);
    uint8_t *data; size_t size;
    if (!asset_read(assets,path,&data,&size)) return 0;
    int channels=0,rate=0,format=0;
    const uint8_t *pcm=NULL; size_t bytes=0;
    if (size>=12 && !memcmp(data,"RIFF",4) && !memcmp(data+8,"WAVE",4)) {
        for (size_t pos=12;pos+8<=size;) {
            size_t len=u32(data+pos+4),start=pos+8;
            if (len>size-start) break;
            if (!memcmp(data+pos,"fmt ",4) && len>=16) {
                format=(int)u16(data+start); channels=(int)u16(data+start+2); rate=(int)u32(data+start+4);
                if (u16(data+start+14)!=16) format=0;
            } else if (!memcmp(data+pos,"data",4)) { pcm=data+start; bytes=len; }
            pos=start+len+(len&1);
        }
    }
    if (format!=1 || channels<1 || channels>2 || rate<4000 || rate>192000 || !pcm || bytes<2*(size_t)channels) { free(data); return 0; }
    size_t frames=bytes/(channels*2u);
    int16_t *samples=malloc(frames*channels*sizeof(*samples));
    if (!samples) { free(data); return 0; }
    for (size_t i=0;i<frames*channels;i++) samples[i]=(int16_t)u16(pcm+i*2);
    free(data);
    pthread_mutex_lock(&lock);
    Sound *s=&sounds[slot];
    snprintf(s->name,sizeof(s->name),"%s",name);
    s->samples=samples; s->frames=frames; s->channels=channels; s->rate=rate;
    pthread_mutex_unlock(&lock);
    return 1;
}
/* Master volume, 0..100 percent; applied to the mixed output in snd_frame. */
static int volume = 80;
void snd_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    volume = percent;
}
int snd_volume(void) { return volume; }
int snd_play(const char *name) {
    if (!name) return 0;
    pthread_mutex_lock(&lock);
    Sound *sound=NULL;
    for (int i=0;i<MAX_SOUNDS;i++) if (sounds[i].samples && !strcmp(sounds[i].name,name)) sound=&sounds[i];
    if (!sound) { pthread_mutex_unlock(&lock); return 0; }
    int slot=0;
    for (int i=0;i<MAX_VOICES;i++) if (!voices[i].sound) { slot=i; break; }
    voices[slot]=(Voice){sound,0};
    pthread_mutex_unlock(&lock);
    return 1;
}
void snd_frame(int16_t *out,int frames) {
    if (!out || frames<=0) return;
    pthread_mutex_lock(&lock);
    for (int i=0;i<frames;i++) {
        int left=0,right=0;
        for (int v=0;v<MAX_VOICES;v++) {
            Voice *voice=&voices[v]; Sound *s=voice->sound;
            if (!s) continue;
            size_t p=(size_t)voice->position;
            if (p>=s->frames) { voice->sound=NULL; continue; }
            size_t next=p+1<s->frames ? p+1 : p;
            float t=(float)(voice->position-p);
            int l=s->samples[p*s->channels],ln=s->samples[next*s->channels];
            int r=s->samples[p*s->channels+s->channels-1],rn=s->samples[next*s->channels+s->channels-1];
            left+=(int)(l+(ln-l)*t); right+=(int)(r+(rn-r)*t);
            voice->position+=(double)s->rate/SND_RATE;
        }
        if (volume != 100) {
            float g = (float)volume / 100.0f;
            left = (int)((float)left * g);
            right = (int)((float)right * g);
        }
        out[i*2]=(int16_t)(left<-32768 ? -32768 : left>32767 ? 32767 : left);
        out[i*2+1]=(int16_t)(right<-32768 ? -32768 : right>32767 ? 32767 : right);
    }
    pthread_mutex_unlock(&lock);
}
int audio_init(AAssetManager *a) { assets=a; return snd_backend_start(); }
void audio_shutdown(void) {
    snd_backend_stop();
    pthread_mutex_lock(&lock);
    for (int i=0;i<MAX_SOUNDS;i++) free(sounds[i].samples);
    memset(sounds,0,sizeof(sounds)); memset(voices,0,sizeof(voices)); assets=NULL;
    pthread_mutex_unlock(&lock);
}
void audio_pause(void) { snd_backend_pause(); }
void audio_resume(void) { snd_backend_resume(); }
void audio_set_java_vm(void *vm) { snd_set_java_vm(vm); }
