/* sound/sound_internal.h — shared interface of the audio module.
 * The mixer and WAV decoder live in sound.c, the output (AudioTrack JNI) in
 * sound_android.c. The PC preview does not build this module (stubs in
 * tools/preview/host_compat.c). */
#ifndef SOUND_INTERNAL_H
#define SOUND_INTERNAL_H

#include "engine.h"

#define SND_RATE 44100

/* fill the buffer with mixed audio (stereo frames, 2*frames int16) */
void snd_frame(int16_t *out, int frames);

/* output backend (sound_android.c) */
void snd_set_java_vm(void *vm);
int snd_backend_start(void);
void snd_backend_stop(void);
void snd_backend_pause(void);
void snd_backend_resume(void);

#endif
