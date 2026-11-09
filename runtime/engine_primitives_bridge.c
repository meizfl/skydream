/* SPDX-License-Identifier: GPL-3.0-only
 * Temporary cdecl adapters. Guest addresses stop at this boundary. */
#include "engine_primitives.h"
#include "rt.h"
#include <string.h>
#ifdef GAME_SKYRIM
static int native_enabled = 1, profile_enabled;
static uint64_t calls[5];
void engine_primitives_init(void) {
  const char *mode = getenv("SKYDREAM_NATIVE_PRIMITIVES");
  native_enabled = !mode || strcmp(mode, "0");
  profile_enabled = getenv("SKYDREAM_PROFILE_PRIMITIVES") != NULL;
  LOG("engine primitives: %s (two memory-move and three CRC entry points)",
      native_enabled ? "native 64-bit cores" : "original recompiled bodies");
}
void engine_primitives_report(void) {
  if (!profile_enabled)
    return;
  LOG("engine primitive calls: F537F0=%llu F52E30=%llu A4A4E0=%llu A4A530=%llu "
      "A4A570=%llu",
      (unsigned long long)__atomic_load_n(&calls[0], __ATOMIC_RELAXED),
      (unsigned long long)__atomic_load_n(&calls[1], __ATOMIC_RELAXED),
      (unsigned long long)__atomic_load_n(&calls[2], __ATOMIC_RELAXED),
      (unsigned long long)__atomic_load_n(&calls[3], __ATOMIC_RELAXED),
      (unsigned long long)__atomic_load_n(&calls[4], __ATOMIC_RELAXED));
}
#define BEGIN(address, index)                                                  \
  extern void f_##address##_guest(CPU *);                                      \
  void f_##address(CPU *c) {                                                   \
    if (profile_enabled)                                                       \
      __atomic_fetch_add(&calls[index], 1, __ATOMIC_RELAXED);                  \
    if (!native_enabled) {                                                     \
      f_##address##_guest(c);                                                  \
      return;                                                                  \
    }
#define MOVE(address, index)                                                   \
  BEGIN(address, index) uint32_t destination = ARG(0);                         \
  skydream_engine_move(GPTR(destination), GPTR(ARG(1)), ARG(2));               \
  c->df = 0;                                                                   \
  RETC(destination);                                                           \
  }
MOVE(F537F0, 0)
MOVE(F52E30, 1)
BEGIN(A4A4E0, 2)
uint32_t out = ARG(0);
size_t processed;
uint32_t crc = skydream_engine_crc32_mapped(GPTR(ARG(1)), ARG(2), GPTR(ARG(3)),
                                            &processed);
M32(out) = crc;
c->r[1] = out;
c->r[2] = crc;
RETC(processed);
}
BEGIN(A4A530, 3)
uint32_t out = ARG(0), size = ARG(2),
         crc = skydream_engine_crc32(GPTR(ARG(1)), size);
M32(out) = crc;
c->r[1] = crc;
c->r[2] = size ? 0 : out;
RETC(size ? out : c->r[0]);
}
BEGIN(A4A570, 4)
uint32_t out = ARG(0), crc = skydream_engine_crc32_u32(ARG(1));
M32(out) = crc;
c->r[1] = crc;
RETC(out);
}
#endif
