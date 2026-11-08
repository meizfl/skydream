/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
typedef struct { uint32_t address, offset, size; } SkydreamImageSegment;
typedef struct { uint32_t address; const char *dll, *name; } SkydreamImageImport;
typedef struct {
    uint32_t base, size, entry, tls_start, tls_end, tls_index, tls_zero, tls_callbacks;
    const char *sha256;
    uint32_t segment_count, import_count;
    const SkydreamImageSegment *segments;
    const SkydreamImageImport *imports;
} SkydreamImage;
extern const SkydreamImage g_skyrim_image;
extern const unsigned char g_skyrim_image_blob[];
void rt_load_embedded_game(void);
