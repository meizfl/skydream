/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Source format numbers are the public D3DFORMAT ABI, not host pointers. */
typedef struct {
  uint32_t width, height, depth, mips, format, type, file_format;
  size_t data_offset;
  uint32_t block_bytes, pixel_bytes, base_pitch;
} NativeD3DXImage;
int native_d3dx_image_info(const void *, size_t, NativeD3DXImage *);
int native_d3dx_dds_level(const NativeD3DXImage *, size_t, uint32_t face,
                          uint32_t level, size_t *offset, uint32_t *width,
                          uint32_t *height, uint32_t *depth, uint32_t *pitch,
                          uint32_t *rows);
uint8_t *native_d3dx_decode_rgba(const void *, size_t, int *, int *);
void native_d3dx_free_image(void *);
#ifdef __cplusplus
}
#endif
