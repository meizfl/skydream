/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded DDS metadata/subresource parsing and native raster decoding. */
#include "d3dx_image.h"
#include <limits.h>
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "../third_party/stb/stb_image.h"
static uint32_t word(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
int native_d3dx_image_info(const void *data, size_t bytes,
                           NativeD3DXImage *out) {
  if (!data || !out || bytes < 4 || bytes > INT_MAX)
    return 0;
  const uint8_t *p = data;
  NativeD3DXImage i = {0};
  if (!memcmp(p, "DDS ", 4)) {
    if (bytes < 128 || word(p + 4) != 124 || word(p + 76) != 32)
      return 0;
    i.width = word(p + 16);
    i.height = word(p + 12);
    i.depth = (word(p + 112) & 0x200000) ? word(p + 24) : 1;
    i.mips = word(p + 28);
    if (!i.mips)
      i.mips = 1;
    if (!i.width || !i.height || !i.depth || i.width > 16384 ||
        i.height > 16384 || i.depth > 2048 || i.mips > 15)
      return 0;
    i.type = (word(p + 112) & 0x200) ? 5 : (word(p + 112) & 0x200000) ? 4 : 3;
    i.file_format = 4;
    i.data_offset = 128;
    uint32_t flags = word(p + 80), fourcc = word(p + 84), bits = word(p + 88),
             r = word(p + 92), g = word(p + 96), b = word(p + 100),
             a = word(p + 104);
    if (flags & 4) {
      if (fourcc == 0x31545844u) {
        i.format = fourcc;
        i.block_bytes = 8;
      } else if (fourcc == 0x32545844u || fourcc == 0x33545844u ||
                 fourcc == 0x34545844u || fourcc == 0x35545844u) {
        i.format = fourcc;
        i.block_bytes = 16;
      } else if (fourcc >= 111 && fourcc <= 116) {
        static const uint8_t widths[] = {2, 4, 8, 4, 8, 16};
        i.format = fourcc;
        i.pixel_bytes = widths[fourcc - 111];
      } else if (fourcc == 36 || fourcc == 110) {
        i.format = fourcc;
        i.pixel_bytes = 8;
      } else
        return 0;
    } else if ((flags & 0x20000) && bits == 8 && r == 255) {
      i.format = 50;
      i.pixel_bytes = 1;
    } else if ((flags & 0x20000) && bits == 16 && r == 65535) {
      i.format = 81;
      i.pixel_bytes = 2;
    } else if ((flags & 0x20000) && bits == 16 && r == 255 && a == 0xff00) {
      i.format = 51;
      i.pixel_bytes = 2;
    } else if ((flags & 2) && bits == 8) {
      i.format = 28;
      i.pixel_bytes = 1;
    } else if (bits == 32 && r == 0xff0000 && g == 0xff00 && b == 255) {
      i.format = a ? 21 : 22;
      i.pixel_bytes = 4;
    } else if (bits == 32 && r == 255 && g == 0xff00 && b == 0xff0000) {
      i.format = a ? 32 : 33;
      i.pixel_bytes = 4;
    } else if (bits == 24 && r == 0xff0000 && g == 0xff00 && b == 255) {
      i.format = 20;
      i.pixel_bytes = 3;
    } else if (bits == 16 && r == 0xf800 && g == 0x7e0 && b == 31) {
      i.format = 23;
      i.pixel_bytes = 2;
    } else if (bits == 16 && r == 0x7c00 && g == 0x3e0 && b == 31) {
      i.format = a ? 25 : 24;
      i.pixel_bytes = 2;
    } else if (bits == 16 && r == 0xf00 && g == 0xf0 && b == 15) {
      i.format = 26;
      i.pixel_bytes = 2;
    } else if ((flags & 0x80000) && bits == 16 && r == 255 && g == 0xff00) {
      i.format = 60;
      i.pixel_bytes = 2;
    } else
      return 0;
    if (!i.block_bytes && (word(p + 8) & 8))
      i.base_pitch = word(p + 20);
    if (i.base_pitch && i.base_pitch < i.width * i.pixel_bytes)
      return 0;
  } else {
    int w, h, ch;
    if (!stbi_info_from_memory(p, (int)bytes, &w, &h, &ch))
      return 0;
    i.width = w;
    i.height = h;
    i.depth = 1;
    i.mips = 1;
    i.type = 3;
    i.format = 21;
    i.pixel_bytes = 4;
    i.file_format = bytes >= 8 && !memcmp(p, "\x89PNG", 4)     ? 3
                    : bytes >= 2 && p[0] == 'B' && p[1] == 'M' ? 0
                    : p[0] == 255 && p[1] == 216               ? 1
                                                               : 2;
  }
  *out = i;
  return 1;
}
int native_d3dx_dds_level(const NativeD3DXImage *i, size_t bytes, uint32_t face,
                          uint32_t level, size_t *offset, uint32_t *width,
                          uint32_t *height, uint32_t *depth, uint32_t *pitch,
                          uint32_t *rows) {
  if (!i || i->file_format != 4 || level >= i->mips ||
      face >= (i->type == 5 ? 6u : 1u))
    return 0;
  size_t at = i->data_offset;
  for (uint32_t f = 0; f <= face; f++)
    for (uint32_t l = 0; l < i->mips; l++) {
      uint32_t w = i->width >> l, h = i->height >> l, d = i->depth >> l;
      if (!w)
        w = 1;
      if (!h)
        h = 1;
      if (!d)
        d = 1;
      uint32_t p = i->block_bytes ? ((w + 3) / 4) * i->block_bytes
                                  : w * i->pixel_bytes,
               r = i->block_bytes ? (h + 3) / 4 : h;
      if (l == 0 && i->base_pitch)
        p = i->base_pitch;
      uint64_t size = (uint64_t)p * r * d;
      if (at > bytes || size > bytes - at)
        return 0;
      if (f == face && l == level) {
        *offset = at;
        *width = w;
        *height = h;
        *depth = d;
        *pitch = p;
        *rows = r;
        return 1;
      }
      at += (size_t)size;
    }
  return 0;
}
uint8_t *native_d3dx_decode_rgba(const void *p, size_t n, int *w, int *h) {
  if (n > INT_MAX)
    return NULL;
  return stbi_load_from_memory(p, (int)n, w, h, NULL, 4);
}
void native_d3dx_free_image(void *p) { stbi_image_free(p); }
