/* SPDX-License-Identifier: GPL-3.0-only
 * Skyrim's Bink stdcall imports, implemented with native FFmpeg and SDL. */
#include "rt.h"
#include "bink_native.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <time.h>
#ifdef GAME_SKYRIM
#define MAX_MOVIES 16
typedef struct {
    uint32_t guest,frame,decoded;NativeBink *decoder;SDL_AudioDeviceID audio;
    uint64_t start,pause_at,audio_samples;int started,paused;
} Movie;
static Movie movies[MAX_MOVIES];
static pthread_mutex_t movie_lock=PTHREAD_MUTEX_INITIALIZER;
static int sound_enabled=1;
static uint64_t ticks(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static Movie *lookup(uint32_t handle) {
    for(unsigned i=0;i<MAX_MOVIES;i++) if(movies[i].guest==handle && handle) return &movies[i];
    rt_fatal("native Bink: invalid handle %08x",handle);return NULL;
}
SHIM(BinkOpen) {
    char path[4096],error[256];uint32_t flags=ARG(1);
    rt_host_path(GSTR(ARG(0)),path,sizeof path);
    NativeBink *decoder=native_bink_open(path,error,sizeof error);
    if(!decoder) {LOG("native BinkOpen failed: %s: %s",path,error);RET(0,2);}
    pthread_mutex_lock(&movie_lock);Movie *m=NULL;
    for(unsigned i=0;i<MAX_MOVIES;i++) if(!movies[i].guest){m=&movies[i];break;}
    if(!m){pthread_mutex_unlock(&movie_lock);native_bink_close(decoder);RET(0,2);}
    memset(m,0,sizeof *m);m->decoder=decoder;m->frame=1;m->guest=gmalloc(64);
    memset(GPTR(m->guest),0,64);
    M32(m->guest)=native_bink_width(decoder);M32(m->guest+4)=native_bink_height(decoder);
    M32(m->guest+8)=native_bink_frames(decoder);M32(m->guest+12)=1;
    M32(m->guest+20)=native_bink_rate(decoder);M32(m->guest+24)=native_bink_rate_div(decoder);
    M32(m->guest+32)=flags;
    if(sound_enabled && native_bink_audio_rate(decoder)>0) {
        SDL_AudioSpec want={0};want.freq=native_bink_audio_rate(decoder);want.format=AUDIO_F32SYS;
        want.channels=2;want.samples=1024;
        rt_host_affinity_begin();m->audio=SDL_OpenAudioDevice(NULL,0,&want,NULL,0);rt_host_affinity_end();
        if(!m->audio) LOG("native Bink audio device unavailable: %s",SDL_GetError());
    }
    uint32_t handle=m->guest;
    LOG("Bink: native FFmpeg opened %s: %ux%u, %u frames, %u/%u fps, audio %d Hz device=%u, flags=%08x; no PE image",path,M32(handle),M32(handle+4),M32(handle+8),M32(handle+20),M32(handle+24),native_bink_audio_rate(decoder),m->audio,flags);
    pthread_mutex_unlock(&movie_lock);RET(handle,2);
}
SHIM(BinkDoFrame) {
    pthread_mutex_lock(&movie_lock);Movie *m=lookup(ARG(0));
    if(!m->paused && m->decoded!=m->frame) {
        int result=native_bink_decode(m->decoder);
        if(result<0) {pthread_mutex_unlock(&movie_lock);rt_fatal("native Bink: video/audio decode failed at frame %u",m->frame);}
        if(result>0) {
            m->decoded=m->frame;M32(m->guest+16)=m->frame;
            if(!m->started) {m->started=1;m->start=ticks();}
            size_t samples;const float *pcm=native_bink_audio(m->decoder,&samples);
            m->audio_samples+=samples;
            if(m->audio && samples && SDL_QueueAudio(m->audio,pcm,samples*sizeof(float))<0)
                LOG("native Bink audio queue failed: %s",SDL_GetError());
            if(m->audio) SDL_PauseAudioDevice(m->audio,0);
        }
    }
    pthread_mutex_unlock(&movie_lock);RET(0,1);
}
SHIM(BinkWait) {
    pthread_mutex_lock(&movie_lock);Movie *m=lookup(ARG(0));
    uint64_t target=(uint64_t)(m->frame-1)*native_bink_rate_div(m->decoder)*1000/native_bink_rate(m->decoder);
    int waiting=m->paused || (m->started && ticks()-m->start<target);
    pthread_mutex_unlock(&movie_lock);RET(waiting,1);
}
SHIM(BinkNextFrame) {
    pthread_mutex_lock(&movie_lock);Movie *m=lookup(ARG(0));
    m->frame++;M32(m->guest+12)=m->frame;
    pthread_mutex_unlock(&movie_lock);RET(0,1);
}
static uint32_t copy_frame(CPU *c,int rect) {
    uint32_t handle=ARG(0),dest=ARG(1);int32_t pitch=(int32_t)ARG(2);
    uint32_t dh=ARG(3),dx=ARG(4),dy=ARG(5),sx=rect?ARG(6):0,sy=rect?ARG(7):0;
    uint32_t flags=ARG(rect?10:6);
    pthread_mutex_lock(&movie_lock);Movie *m=lookup(handle);
    uint32_t width=native_bink_width(m->decoder),height=native_bink_height(m->decoder);
    uint32_t w=rect?ARG(8):width,h=rect?ARG(9):height;
    /* Skyrim requests BINKSURFACE32, optionally COPYALL. Unsupported formats
     * fail visibly rather than returning plausible but wrongly packed pixels. */
    if((flags&15)!=3 || !dest || !m->decoded || pitch<=0 || dx>(uint32_t)pitch/4 || sx>width || sy>height || dy>dh || (uint64_t)dest+(uint64_t)dh*pitch>UINT64_C(0x100000000)) {
        pthread_mutex_unlock(&movie_lock);LOG("native Bink: unsupported/invalid copy flags=%08x rect=%ux%u pitch=%d",flags,w,h,pitch);return 0;
    }
    /* Skyrim uploads 256x256 tiles, including partial tiles at the movie's
     * bottom edge. Bink clips these requests to the decoded image. */
    const uint8_t *pixels=native_bink_pixels(m->decoder);
    if(m->frame==90 && sx==0 && sy==0) {
        const char *capture=getenv("LB_BINK_CAPTURE");
        if(capture && *capture) {
            extern void bridge_request_capture(const char *);
            bridge_request_capture(capture);
            LOG("Bink capture: frame 90, src %u,%u %ux%u, dest %u,%u height=%u pitch=%d flags=%08x center=%02x%02x%02x%02x",sx,sy,w,h,dx,dy,dh,pitch,flags,pixels[((size_t)height/2*width+width/2)*4],pixels[((size_t)height/2*width+width/2)*4+1],pixels[((size_t)height/2*width+width/2)*4+2],pixels[((size_t)height/2*width+width/2)*4+3]);
        }
    }
    int result=native_bink_copy_bgra(MEM+dest,(size_t)dh*pitch,pitch,dh,dx,dy,pixels,width,height,sx,sy,w,h);
    pthread_mutex_unlock(&movie_lock);return result;
}
SHIM(BinkCopyToBufferRect){uint32_t result=copy_frame(c,1);RET(result,11);}
SHIM(BinkCopyToBuffer){uint32_t result=copy_frame(c,0);RET(result,7);}
SHIM(BinkPause) {
    pthread_mutex_lock(&movie_lock);Movie *m=lookup(ARG(0));int pause=!!ARG(1);
    if(pause!=m->paused) {
        if(pause) m->pause_at=ticks();else if(m->started) m->start+=ticks()-m->pause_at;
        m->paused=pause;if(m->audio) SDL_PauseAudioDevice(m->audio,pause);
    }
    pthread_mutex_unlock(&movie_lock);RET(pause,2);
}
SHIM(BinkClose) {
    pthread_mutex_lock(&movie_lock);Movie *m=lookup(ARG(0));
    LOG("Bink: native close after %u decoded frames, elapsed %llu ms, audio %llu samples",m->decoded,(unsigned long long)(m->started?ticks()-m->start:0),(unsigned long long)m->audio_samples);
    if(m->audio) SDL_CloseAudioDevice(m->audio);
    native_bink_close(m->decoder);gfree(m->guest);memset(m,0,sizeof *m);
    pthread_mutex_unlock(&movie_lock);RET(0,1);
}
SHIM(BinkSetSoundSystem){sound_enabled=ARG(0)!=0;RET(1,2);}
SHIM(BinkOpenDirectSound){RET(1,1);}
void bink_init(void) {
    static const ShimDef defs[]={
#define B(name,n) {"binkw32.dll","_" #name "@" #n,shim_##name}
        B(BinkOpen,8),B(BinkDoFrame,4),B(BinkWait,4),B(BinkNextFrame,4),B(BinkPause,8),
        B(BinkClose,4),B(BinkCopyToBufferRect,44),B(BinkCopyToBuffer,28),
        B(BinkSetSoundSystem,8),B(BinkOpenDirectSound,4),{NULL,NULL,NULL}
#undef B
    };rt_register_shims(defs);
}
#else
void bink_init(void) {}
#endif
