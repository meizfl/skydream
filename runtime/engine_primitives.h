/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Native pointers and size_t; checksum width remains part of the file/table
 * ABI. */
void *skydream_engine_move(void *destination, const void *source, size_t bytes);
uint32_t skydream_engine_crc32(const void *data, size_t bytes);
/* Translation table is biased: valid indices are -128..127, as in TESV. */
uint32_t skydream_engine_crc32_mapped(const void *data, size_t limit,
                                      const uint8_t *translation,
                                      size_t *processed);
uint32_t skydream_engine_crc32_u32(uint32_t value);
