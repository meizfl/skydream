/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Explicit guest addresses: never cast a guest structure with pointers to a
 * host F3DAUDIO structure. Zero indicates success. No PE image is used. */
enum { X3D_OK=0, X3D_BAD_MEMORY=1, X3D_BAD_CHANNELS=2, X3D_UNSUPPORTED_DELAY=3, X3D_BAD_INIT=4 };
int x3d_native_initialize(uint8_t *,size_t,uint32_t,float,uint32_t);
int x3d_native_calculate(uint8_t *,size_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
