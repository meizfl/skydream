/* SPDX-License-Identifier: GPL-3.0-only
 * Transitional TESV cdecl boundary; only this layer uses guest addresses.
 * Init runs before game threads. Optional counters are atomic and off by default.
 */
#include "engine_strings.h"
#include "rt.h"
#include <string.h>
#ifdef GAME_SKYRIM
static int native_enabled = 1, profile_enabled;
static uint64_t calls[4];
void engine_strings_init(void) {
    const char *mode = getenv("SKYDREAM_NATIVE_STRINGS");
    native_enabled = !mode || strcmp(mode, "0");
    profile_enabled = getenv("SKYDREAM_PROFILE_STRINGS") != NULL;
    LOG("engine strings: %s (length, character/byte search, padded copy)",
        native_enabled ? "native 64-bit cores" : "original recompiled bodies");
}
void engine_strings_report(void) {
    if (!profile_enabled) return;
    LOG("engine string calls: F5B670=%llu F520F0=%llu F55A90=%llu F59140=%llu",
        (unsigned long long)__atomic_load_n(&calls[0], __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&calls[1], __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&calls[2], __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&calls[3], __ATOMIC_RELAXED));
}
#define BEGIN(address, index) \
    extern void f_##address##_guest(CPU *); \
    void f_##address(CPU *c) { \
        if (profile_enabled) __atomic_fetch_add(&calls[index], 1, __ATOMIC_RELAXED); \
        if (!native_enabled) { f_##address##_guest(c); return; }
BEGIN(F5B670, 0)
    RETC(skydream_string_length(GPTR(ARG(0))));
}
BEGIN(F520F0, 1)
    uint32_t base = ARG(0);
    const char *text = GPTR(base);
    const char *found = skydream_string_find(text, ARG(1));
    RETC(found ? base + (size_t)(found - text) : 0);
}
BEGIN(F55A90, 2)
    uint32_t base = ARG(0);
    const unsigned char *data = GPTR(base);
    const unsigned char *found = skydream_bytes_find(data, ARG(1), ARG(2));
    RETC(found ? base + (size_t)(found - data) : 0);
}
BEGIN(F59140, 3)
    uint32_t destination = ARG(0);
    skydream_string_copy_padded(GPTR(destination), GPTR(ARG(1)), ARG(2));
    RETC(destination);
}
#endif
