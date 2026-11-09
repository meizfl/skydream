/* Native Linux xWMA decoding. Android can still build without FFmpeg. */
#include "xwma.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef LB_HAVE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>

struct XWmaDecoder {
    AVCodecContext *codec;
    AVFrame *frame;
    SwrContext *swr;
    unsigned tag;
    int channels, rate, average_bytes, block_align, bits, finished;
    int64_t next_pts;
    uint8_t *extra;
    size_t extra_size;
};

static int open_codec(XWmaDecoder *d) {
    enum AVCodecID id = d->tag == 0x161 ? AV_CODEC_ID_WMAV2 : AV_CODEC_ID_WMAPRO;
    const AVCodec *codec = avcodec_find_decoder(id);
    if (!codec || !(d->codec = avcodec_alloc_context3(codec))) return -1;
    AVCodecContext *c = d->codec;
    c->sample_rate = d->rate;
    c->pkt_timebase = (AVRational){1, d->rate};
    c->bit_rate = (int64_t)d->average_bytes * 8;
    c->block_align = d->block_align;
    c->bits_per_coded_sample = d->bits;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    av_channel_layout_default(&c->ch_layout, d->channels);
#else
    c->channels = d->channels;
    c->channel_layout = av_get_default_channel_layout(d->channels);
#endif
    /* xWMA omits codec options and sometimes reports an alias bitrate.
     * Match FFmpeg's xWMA container interpretation:
     * https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/xwma.c */
    if (id == AV_CODEC_ID_WMAV2 && !d->extra_size) {
        int rate = d->rate, ch = d->channels;
        int64_t br = c->bit_rate;
        if (ch == 1 && (rate == 22050 || rate == 32000) && (br == 48000 || br == 192000)) c->bit_rate = 20000;
        else if (ch == 1 && rate == 44100 && (br == 96000 || br == 192000)) c->bit_rate = 48000;
        else if (ch == 2 && rate == 22050 && (br == 48000 || br == 192000)) c->bit_rate = 32000;
        else if (ch == 2 && rate == 32000 && br == 192000) c->bit_rate = 48000;
    }
    size_t extra_size = d->extra_size ? d->extra_size : id == AV_CODEC_ID_WMAV2 ? 6 : 18;
    c->extradata = av_mallocz(extra_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!c->extradata) return -1;
    c->extradata_size = (int)extra_size;
    if (d->extra_size) memcpy(c->extradata, d->extra, extra_size);
    else if (id == AV_CODEC_ID_WMAV2) c->extradata[4] = 31;
    else { c->extradata[0] = d->bits; c->extradata[14] = 224; }
    return avcodec_open2(c, codec, NULL);
}

XWmaDecoder *xwma_create(unsigned tag, int channels, int rate, int average_bytes,
                        int block_align, int bits, const uint8_t *extra, size_t extra_size) {
    if ((tag != 0x161 && tag != 0x162) || channels < 1 || channels > 8 || rate <= 0 ||
        average_bytes <= 0 || block_align <= 0 || extra_size > 65535 || (extra_size && !extra)) return NULL;
    XWmaDecoder *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->tag = tag; d->channels = channels; d->rate = rate;
    d->average_bytes = average_bytes; d->block_align = block_align; d->bits = bits;
    if (extra_size) {
        d->extra = malloc(extra_size);
        if (!d->extra) { xwma_destroy(d); return NULL; }
        memcpy(d->extra, extra, extra_size);
        d->extra_size = extra_size;
    }
    d->frame = av_frame_alloc();
    if (!d->frame || open_codec(d) < 0) { xwma_destroy(d); return NULL; }
    return d;
}

void xwma_destroy(XWmaDecoder *d) {
    if (!d) return;
    swr_free(&d->swr);
    av_frame_free(&d->frame);
    avcodec_free_context(&d->codec);
    free(d->extra);
    free(d);
}

void xwma_reset(XWmaDecoder *d) {
    if (!d || !d->codec) return;
    avcodec_flush_buffers(d->codec);
    swr_free(&d->swr);
    d->finished = 0;
    d->next_pts = 0;
}

int xwma_set_rate(XWmaDecoder *d, int rate) {
    if (!d || rate <= 0) return -1;
    if (rate == d->rate && d->codec && avcodec_is_open(d->codec)) return 0;
    swr_free(&d->swr);
    avcodec_free_context(&d->codec);
    d->rate = rate;
    d->finished = 0;
    d->next_pts = 0;
    return open_codec(d);
}

static int receive_pcm(XWmaDecoder *d, int16_t **pcm, uint32_t *frames) {
    int rc;
    while ((rc = avcodec_receive_frame(d->codec, d->frame)) >= 0) {
        AVFrame *f = d->frame;
        if (!d->swr) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
            AVChannelLayout output;
            av_channel_layout_default(&output, d->channels);
            rc = swr_alloc_set_opts2(&d->swr, &output, AV_SAMPLE_FMT_S16, d->rate,
                                    &f->ch_layout, f->format, f->sample_rate, 0, NULL);
            av_channel_layout_uninit(&output);
#else
            int64_t input_layout = f->channel_layout;
            if (!input_layout) input_layout = av_get_default_channel_layout(f->channels);
            d->swr = swr_alloc_set_opts(NULL, av_get_default_channel_layout(d->channels),
                                       AV_SAMPLE_FMT_S16, d->rate, input_layout,
                                       f->format, f->sample_rate, 0, NULL);
            rc = d->swr ? 0 : AVERROR(ENOMEM);
#endif
            if (rc < 0 || swr_init(d->swr) < 0) return -1;
        }
        int count = swr_get_out_samples(d->swr, f->nb_samples);
        if (!count) { av_frame_unref(f); continue; }
        if (count < 0 || (uint64_t)*frames + count > UINT32_MAX ||
            (uint64_t)(*frames + (uint32_t)count) * d->channels > SIZE_MAX / sizeof(int16_t)) return -1;
        size_t samples = ((size_t)*frames + count) * (size_t)d->channels;
        int16_t *next = realloc(*pcm, samples * sizeof(int16_t));
        if (!next && samples) return -1;
        *pcm = next;
        uint8_t *out = (uint8_t *)(next + (size_t)*frames * d->channels);
        rc = swr_convert(d->swr, &out, count, (const uint8_t **)f->extended_data, f->nb_samples);
        av_frame_unref(f);
        if (rc < 0) return rc;
        *frames += (uint32_t)rc;
    }
    return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF ? 0 : rc;
}

int xwma_decode(XWmaDecoder *d, const uint8_t *data, size_t bytes,
               unsigned packets, int end_of_stream, int16_t **pcm, uint32_t *frames) {
    *pcm = NULL; *frames = 0;
    if (!d || !d->codec || (!data && bytes)) return -1;
    if (!bytes && !end_of_stream) return 0;
    if (d->finished) xwma_reset(d);
    size_t packet_size = packets && bytes ? bytes / packets : (size_t)d->block_align;
    if (!packet_size || packet_size > INT_MAX || (packets && bytes % packets)) return -1;
    AVPacket *packet = av_packet_alloc();
    if (!packet) return -1;
    int rc = 0;
    for (size_t offset = 0; offset < bytes; offset += packet_size) {
        size_t size = bytes - offset < packet_size ? bytes - offset : packet_size;
        if ((rc = av_new_packet(packet, (int)size)) < 0) break;
        memcpy(packet->data, data + offset, size);
        packet->pts = packet->dts = d->next_pts;
        uint32_t before = *frames;
        rc = avcodec_send_packet(d->codec, packet);
        av_packet_unref(packet);
        if (rc < 0 || (rc = receive_pcm(d, pcm, frames)) < 0) break;
        d->next_pts += *frames - before;
    }
    if (rc >= 0 && end_of_stream) {
        rc = avcodec_send_packet(d->codec, NULL);
        if (rc >= 0) rc = receive_pcm(d, pcm, frames);
        d->finished = 1;
    }
    av_packet_free(&packet);
    if (rc < 0) { free(*pcm); *pcm = NULL; *frames = 0; xwma_reset(d); }
    return rc;
}
#else
struct XWmaDecoder { int unused; };
XWmaDecoder *xwma_create(unsigned t, int c, int r, int a, int b, int bits, const uint8_t *e, size_t n) {
    (void)t; (void)c; (void)r; (void)a; (void)b; (void)bits; (void)e; (void)n; return NULL;
}
void xwma_destroy(XWmaDecoder *d) { (void)d; }
void xwma_reset(XWmaDecoder *d) { (void)d; }
int xwma_set_rate(XWmaDecoder *d, int r) { (void)d; (void)r; return -1; }
int xwma_decode(XWmaDecoder *d, const uint8_t *b, size_t n, unsigned p, int end, int16_t **out, uint32_t *f) {
    (void)d; (void)b; (void)n; (void)p; (void)end; *out = NULL; *f = 0; return -1;
}
#endif
