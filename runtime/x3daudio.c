/* SPDX-License-Identifier: GPL-3.0-only
 * Guest cdecl entry points backed by native spatial audio math. */
#include "rt.h"
#include "x3daudio_native.h"
#ifdef GAME_SKYRIM
SHIM(X3DAudioInitialize) {
    int status=x3d_native_initialize(MEM,(size_t)UINT32_MAX+1,ARG(0),ARGF(1),ARG(2));
    if(status) rt_fatal("native X3DAudioInitialize failed: %d",status);
    LOG("X3DAudio: native F3DAudio initialized (mask=%08x, speed=%g); no PE image",ARG(0),ARGF(1));
    RETC(0);
}
SHIM(X3DAudioCalculate) {
    uint32_t flags=ARG(3);
    int status=x3d_native_calculate(MEM,(size_t)UINT32_MAX+1,ARG(0),ARG(1),ARG(2),flags,ARG(4));
    if(status) rt_fatal("native X3DAudioCalculate failed: %d (flags=%08x)",status,flags);
    static unsigned seen;
    unsigned previous=__atomic_fetch_or(&seen,flags|0x80000000u,__ATOMIC_RELAXED);
    if((previous&(flags|0x80000000u))!=(flags|0x80000000u))
        LOG("X3DAudio: native spatial calculation (flags=%08x)",flags);
    RETC(0);
}
void x3daudio_init(void) {
    static const ShimDef defs[]={
        {"X3DAudio1_7.dll","X3DAudioInitialize",shim_X3DAudioInitialize},
        {"X3DAudio1_7.dll","X3DAudioCalculate",shim_X3DAudioCalculate},{NULL,NULL,NULL}
    };
    rt_register_shims(defs);
}
#else
void x3daudio_init(void) {}
#endif
