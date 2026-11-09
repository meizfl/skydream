/* SPDX-License-Identifier: GPL-3.0-only
 * Portable interception; no ELF --wrap dependency. */
#include "rt.h"
#ifdef GAME_SKYRIM
extern void f_8F9720_guest(CPU *);
void f_8F9720(CPU *c) {
#ifdef SKYDREAM_SKSE
    uint32_t registry_ptr = ARG(0);
#endif
    f_8F9720_guest(c);
#ifdef SKYDREAM_SKSE
    extern void skse_register_natives(CPU *, uint32_t);
    if (!registry_ptr || !M32(registry_ptr))
        rt_fatal("SKSE compatibility: missing Papyrus registry");
    skse_register_natives(c, M32(registry_ptr));
#endif
}
#endif
