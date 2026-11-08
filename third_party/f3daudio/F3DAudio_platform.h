/* SPDX-License-Identifier: GPL-3.0-only
 * Skydream standalone platform adapter; not an upstream FAudio file. */
#pragma once
#include <assert.h>
#include <math.h>
#include <string.h>
#define FAudio_assert assert
#define FAudio_zero(p,n) memset((p),0,(n))
#define FAudio_acosf acosf
#define FAudio_atan2f atan2f
#define FAudio_cosf cosf
#define FAudio_fabsf fabsf
#define FAudio_sinf sinf
#define FAudio_sqrtf sqrtf
#define FAudio_max(a,b) ((a)>(b)?(a):(b))
#define FAudio_clamp(v,lo,hi) ((v)>(hi)?(hi):((v)<(lo)?(lo):(v)))
#define ALIGN(type,boundary) type __attribute__((aligned(boundary)))
#define FAUDIO_E_INVALID_CALL 0x88960001u
