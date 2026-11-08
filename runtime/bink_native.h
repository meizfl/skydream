/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct NativeBink NativeBink;
int native_bink_available(void);
NativeBink *native_bink_open(const char *path,char *error,size_t error_size);
void native_bink_close(NativeBink *);
uint32_t native_bink_width(const NativeBink *);
uint32_t native_bink_height(const NativeBink *);
uint32_t native_bink_frames(const NativeBink *);
uint32_t native_bink_rate(const NativeBink *);
uint32_t native_bink_rate_div(const NativeBink *);
/* 1=frame, 0=EOF, -1=decode error. Pixels are top-down BGRA. */
int native_bink_decode(NativeBink *);
const uint8_t *native_bink_pixels(const NativeBink *);
const float *native_bink_audio(const NativeBink *,size_t *samples);
int native_bink_audio_rate(const NativeBink *);
int native_bink_copy_bgra(uint8_t *dest,size_t dest_size,int pitch,uint32_t dest_height,
    uint32_t dx,uint32_t dy,const uint8_t *pixels,uint32_t width,uint32_t height,
    uint32_t sx,uint32_t sy,uint32_t copy_width,uint32_t copy_height);
