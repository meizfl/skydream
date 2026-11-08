// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct XWmaDecoder XWmaDecoder;
XWmaDecoder *xwma_create(unsigned tag, int channels, int rate, int average_bytes,
                        int block_align, int bits, const uint8_t *extra, size_t extra_size);
void xwma_destroy(XWmaDecoder *decoder);
void xwma_reset(XWmaDecoder *decoder);
int xwma_set_rate(XWmaDecoder *decoder, int rate);
/* The caller owns the returned interleaved signed-16 PCM (free, not av_free).
 * Keep one decoder per voice so bit reservoirs survive streaming buffers. */
int xwma_decode(XWmaDecoder *decoder, const uint8_t *data, size_t bytes,
               unsigned packets, int end_of_stream, int16_t **pcm, uint32_t *frames);
