/* SPDX-License-Identifier: GPL-3.0-only
 * Native Bink 1 decoding through FFmpeg, independent of guest memory/PE code. */
#include "bink_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int native_bink_copy_bgra(uint8_t *dest,size_t dest_size,int pitch,uint32_t dh,
    uint32_t dx,uint32_t dy,const uint8_t *pixels,uint32_t width,uint32_t height,
    uint32_t sx,uint32_t sy,uint32_t w,uint32_t h) {
    if(!dest || !pixels || pitch<=0 || (uint64_t)dh*(unsigned)pitch>dest_size ||
       dx>(unsigned)pitch/4 || dy>dh || sx>width || sy>height) return 0;
    if(w>width-sx) w=width-sx;
    if(h>height-sy) h=height-sy;
    if(w>(unsigned)pitch/4-dx) w=(unsigned)pitch/4-dx;
    if(h>dh-dy) h=dh-dy;
    for(uint32_t y=0;y<h;y++) memcpy(dest+(size_t)(dy+y)*pitch+dx*4,
        pixels+((size_t)(sy+y)*width+sx)*4,(size_t)w*4);
    return 1;
}
#ifdef LB_HAVE_FFMPEG
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
struct NativeBink {
    AVFormatContext *format;AVCodecContext *video,*audio;AVFrame *vf,*af;
    AVPacket *packet;struct SwsContext *scale;SwrContext *swr;
    int vi,ai,audio_rate,eof;uint32_t width,height,frames,rate,rate_div;
    uint8_t *pixels;float *pcm;size_t samples,capacity;
};
int native_bink_available(void) {
    return av_find_input_format("bink") && avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO) &&
        avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_DCT) && avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_RDFT);
}
static uint32_t le32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static AVCodecContext *open_stream(AVFormatContext *f,int stream) {
    const AVCodec *decoder=avcodec_find_decoder(f->streams[stream]->codecpar->codec_id);
    if(!decoder) return NULL;
    AVCodecContext *c=avcodec_alloc_context3(decoder);
    if(!c) return NULL;
    if(avcodec_parameters_to_context(c,f->streams[stream]->codecpar)<0 || avcodec_open2(c,decoder,NULL)<0) {avcodec_free_context(&c);return NULL;}
    return c;
}
NativeBink *native_bink_open(const char *path,char *error,size_t error_size) {
    NativeBink *b=calloc(1,sizeof *b);uint8_t header[36];FILE *file=fopen(path,"rb");
    const char *why="cannot open Bink movie";
    if(!b) {if(file) fclose(file);goto failed;}
    b->vi=b->ai=-1;
    if(!file) goto failed;
    size_t size=fread(header,1,sizeof header,file);fclose(file);
    if(size!=sizeof header || memcmp(header,"BIK",3)) {why="unsupported movie: expected Bink 1";goto failed;}
    b->frames=le32(header+8);b->width=le32(header+20);b->height=le32(header+24);
    b->rate=le32(header+28);b->rate_div=le32(header+32);
    if(!b->frames || !b->width || !b->height || b->width>8192 || b->height>8192 || !b->rate || !b->rate_div) {why="invalid Bink header";goto failed;}
    if(avformat_open_input(&b->format,path,av_find_input_format("bink"),NULL)<0 || avformat_find_stream_info(b->format,NULL)<0) {why="Bink demuxer unavailable or invalid movie";goto failed;}
    for(unsigned i=0;i<b->format->nb_streams;i++) {
        if(b->format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && b->vi<0) b->vi=i;
        if(b->format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && b->ai<0) b->ai=i;
    }
    if(b->vi<0 || !(b->video=open_stream(b->format,b->vi))) {why="Bink video decoder unavailable";goto failed;}
    if(b->ai>=0 && !(b->audio=open_stream(b->format,b->ai))) {why="Bink audio decoder unavailable";goto failed;}
    if(b->video->width!=(int)b->width || b->video->height!=(int)b->height) {why="inconsistent Bink dimensions";goto failed;}
    b->vf=av_frame_alloc();b->af=av_frame_alloc();b->packet=av_packet_alloc();
    b->pixels=malloc((size_t)b->width*b->height*4);
    if(!b->vf || !b->af || !b->packet || !b->pixels) {why="Bink allocation failed";goto failed;}
    b->audio_rate=b->audio?b->audio->sample_rate:0;
    return b;
failed:
    if(error && error_size) snprintf(error,error_size,"%s",why);
    native_bink_close(b);return NULL;
}
void native_bink_close(NativeBink *b) {
    if(!b) return;
    swr_free(&b->swr);sws_freeContext(b->scale);av_frame_free(&b->vf);av_frame_free(&b->af);
    av_packet_free(&b->packet);avcodec_free_context(&b->video);avcodec_free_context(&b->audio);
    avformat_close_input(&b->format);free(b->pixels);free(b->pcm);free(b);
}
static int receive_audio(NativeBink *b) {
    int result;
    while((result=avcodec_receive_frame(b->audio,b->af))>=0) {
        if(!b->swr) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&b->swr,&stereo,AV_SAMPLE_FMT_FLT,b->audio_rate,&b->af->ch_layout,b->af->format,b->af->sample_rate,0,NULL)<0) return -1;
#else
            b->swr=swr_alloc_set_opts(NULL,AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_FLT,b->audio_rate,
                b->af->channel_layout?b->af->channel_layout:av_get_default_channel_layout(b->af->channels),b->af->format,b->af->sample_rate,0,NULL);
#endif
            if(!b->swr || swr_init(b->swr)<0) return -1;
        }
        int frames=swr_get_out_samples(b->swr,b->af->nb_samples);
        if(frames<0 || (size_t)frames>16777216) return -1;
        size_t needed=b->samples+(size_t)frames*2;
        if(needed>16777216) return -1;
        if(needed>b->capacity) {
            float *p=realloc(b->pcm,needed*sizeof(float));if(!p) return -1;
            b->pcm=p;b->capacity=needed;
        }
        uint8_t *out=(uint8_t *)(b->pcm+b->samples);
        frames=swr_convert(b->swr,&out,frames,(const uint8_t **)b->af->extended_data,b->af->nb_samples);
        if(frames<0) return -1;
        b->samples+=(size_t)frames*2;av_frame_unref(b->af);
    }
    return result==AVERROR(EAGAIN)||result==AVERROR_EOF?0:-1;
}
int native_bink_decode(NativeBink *b) {
    b->samples=0;
    for(;;) {
        int result=avcodec_receive_frame(b->video,b->vf);
        if(result>=0) {
            b->scale=sws_getCachedContext(b->scale,b->width,b->height,b->vf->format,b->width,b->height,AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
            if(!b->scale) return -1;
            uint8_t *out[]={b->pixels,NULL,NULL,NULL};int pitch[]={(int)b->width*4,0,0,0};
            result=sws_scale(b->scale,(const uint8_t *const *)b->vf->data,b->vf->linesize,0,b->height,out,pitch);
            av_frame_unref(b->vf);return result==(int)b->height?1:-1;
        }
        if(result==AVERROR_EOF) return 0;
        if(result!=AVERROR(EAGAIN)) return -1;
        if(b->eof) return 0;
        result=av_read_frame(b->format,b->packet);
        if(result<0) {
            if(result!=AVERROR_EOF) return -1;
            b->eof=1;avcodec_send_packet(b->video,NULL);
            if(b->audio) {avcodec_send_packet(b->audio,NULL);if(receive_audio(b)<0) return -1;}
            continue;
        }
        if(b->packet->stream_index==b->vi) result=avcodec_send_packet(b->video,b->packet);
        else if(b->packet->stream_index==b->ai) {
            result=avcodec_send_packet(b->audio,b->packet);
            if(result>=0) result=receive_audio(b);
        } else result=0;
        av_packet_unref(b->packet);if(result<0) return -1;
    }
}
uint32_t native_bink_width(const NativeBink *b){return b->width;}
uint32_t native_bink_height(const NativeBink *b){return b->height;}
uint32_t native_bink_frames(const NativeBink *b){return b->frames;}
uint32_t native_bink_rate(const NativeBink *b){return b->rate;}
uint32_t native_bink_rate_div(const NativeBink *b){return b->rate_div;}
const uint8_t *native_bink_pixels(const NativeBink *b){return b->pixels;}
const float *native_bink_audio(const NativeBink *b,size_t *samples){*samples=b->samples;return b->pcm;}
int native_bink_audio_rate(const NativeBink *b){return b->audio_rate;}
#else
/* This runtime feature requires a native FFmpeg build on the target platform. */
int native_bink_available(void){return 0;}
NativeBink *native_bink_open(const char *p,char *e,size_t n){(void)p;if(e&&n)snprintf(e,n,"native Bink requires FFmpeg");return NULL;}
void native_bink_close(NativeBink *b){(void)b;}
int native_bink_decode(NativeBink *b){(void)b;return -1;}
#define ZERO(name) uint32_t name(const NativeBink *b){(void)b;return 0;}
ZERO(native_bink_width) ZERO(native_bink_height) ZERO(native_bink_frames) ZERO(native_bink_rate) ZERO(native_bink_rate_div)
const uint8_t *native_bink_pixels(const NativeBink *b){(void)b;return NULL;}
const float *native_bink_audio(const NativeBink *b,size_t *n){(void)b;*n=0;return NULL;}
int native_bink_audio_rate(const NativeBink *b){(void)b;return 0;}
#endif
