/* SPDX-License-Identifier: GPL-3.0-only
 * TESV 1.9.32 physics scheduling boundary, before Havok jobs are dispatched.
 * Keep scheduler accumulation, game-time scaling and all physics jobs intact.
 */
#include "physics_timing.h"
#include "rt.h"
#include <string.h>
#include <time.h>
#ifdef GAME_SKYRIM
static int enabled = 1, profile, fixed = 1;
static int was_active;
static SkydreamPhysicsClock simulation;
static uint64_t actual_steps, invalid_steps;
static uint32_t motion_delta_bits;
static uint64_t updates;
/* Only the game thread enters this scheduling boundary. No render-thread writes. */
static struct timespec previous;
static int have_previous;
static uint32_t last_frame, last_normal, last_complex;
uint32_t physics_motion_delta_bits(void) {
    return enabled && fixed ? __atomic_load_n(&motion_delta_bits, __ATOMIC_RELAXED)
                            : M32(0x12D18DCu);
}
float physics_motion_delta(void) {
    uint32_t bits = physics_motion_delta_bits();
    float value; memcpy(&value, &bits, sizeof value); return value;
}
void physics_timing_init(void) {
    const char *mode = getenv("SKYDREAM_HAVOK_FIX");
    enabled = !mode || strcmp(mode, "0");
    profile = getenv("SKYDREAM_PROFILE_PHYSICS") != NULL;
    const char *policy = getenv("SKYDREAM_PHYSICS_MODE");
    fixed = !policy || strcmp(policy, "adaptive");
    have_previous = 0;
    was_active = 0;
    skydream_physics_reset(&simulation);
    LOG("Havok timing: %s", enabled ? (fixed ? "fixed 60-Hz simulation accumulator" : "dynamic high-resolution frame limits") : "original fixed limits");
}
void physics_timing_report(void) {
    if (!profile) return;
    float frame, normal, complex;
    uint32_t bits = __atomic_load_n(&last_frame, __ATOMIC_RELAXED);
    memcpy(&frame, &bits, sizeof frame);
    bits = __atomic_load_n(&last_normal, __ATOMIC_RELAXED);
    memcpy(&normal, &bits, sizeof normal);
    bits = __atomic_load_n(&last_complex, __ATOMIC_RELAXED);
    memcpy(&complex, &bits, sizeof complex);
    LOG("Havok timing updates=%llu unscaled=%.6g max=%.6g complex=%.6g",
        (unsigned long long)__atomic_load_n(&updates, __ATOMIC_RELAXED), frame, normal, complex);
    if (fixed) LOG("Havok fixed solver steps=%llu nonfixed=%llu",
        (unsigned long long)__atomic_load_n(&actual_steps, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&invalid_steps, __ATOMIC_RELAXED));
}
extern void f_D0DB50_guest(CPU *);
void f_D0DB50(CPU *c) {
    if (enabled) {
        /* One scheduling boundary per game frame. Use high-resolution elapsed
         * wall time: the original unscaled timer is quantized to milliseconds,
         * so at 2000 FPS it alternates between zero and one millisecond.
         * Present-only timing can be out of phase with simulation; scaled game
         * delta would incorrectly change this budget during slow motion. */
        float normal, complex;
        double frame = MF32(0x1B4ADE4u); /* startup / clock-failure fallback */
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
            if (have_previous) {
                double elapsed = (double)(now.tv_sec - previous.tv_sec)
                               + (double)(now.tv_nsec - previous.tv_nsec) * 1e-9;
                if (elapsed > 0.0) frame = elapsed;
            }
            previous = now;
            have_previous = 1;
        } else have_previous = 0;
        if (fixed) {
            float submitted = ARGF(0), unscaled = MF32(0x1B4ADE4u);
            double scale = MF32(0x12C2410u);
            /* The timer pause counter is authoritative even on sub-ms frames.
             * A zero submitted delta with nonzero unscaled delta is an engine
             * gate (menu/load), not millisecond quantization. */
            int active = M32(0x1B4ADF8u) == 0 && scale > 0.0 &&
                         !(submitted == 0.0f && unscaled > 0.0f);
            if (!was_active) frame = 0.0; /* no menu/load catch-up on resume */
            was_active = active;
            /* Character displacement belongs to this rendered update, not to
             * zero/one/multiple solver ticks. Avoid dividing frame displacement
             * by the accumulated fixed batch duration in the four motion readers. */
            float motion = active ? (submitted > 0.0f ? submitted : (float)(frame * scale)) : 0.0f;
            uint32_t motion_bits; memcpy(&motion_bits, &motion, sizeof motion_bits);
            __atomic_store_n(&motion_delta_bits, motion_bits, __ATOMIC_RELAXED);
            SkydreamPhysicsBatch batch = skydream_physics_advance(&simulation, frame, scale, active);
            normal = complex = (float)SKYDREAM_PHYSICS_STEP;
            MF32(0x12D19E8u) = normal;
            MF32(0x12D19C4u) = complex;
            MF32(0x12D18E4u) = submitted;
            MF32(0x12D18E0u) = normal;
            MF32(0x12D18DCu) = (float)batch.seconds;
            MF32(0x1BB5E20u) = (float)simulation.remainder;
            if (profile) {
                uint32_t bits;
                float frame_value = (float)frame;
                memcpy(&bits, &frame_value, sizeof bits);
                __atomic_store_n(&last_frame, bits, __ATOMIC_RELAXED);
                memcpy(&bits, &normal, sizeof bits);
                __atomic_store_n(&last_normal, bits, __ATOMIC_RELAXED);
                __atomic_store_n(&last_complex, bits, __ATOMIC_RELAXED);
                __atomic_fetch_add(&updates, 1, __ATOMIC_RELAXED);
            }
            uint32_t result;
            float seconds = (float)batch.seconds;
            memcpy(&result, &seconds, sizeof result);
            RETC(result);
        }
        if (skydream_physics_limits(frame, &normal, &complex)) {
            MF32(0x12D19E8u) = normal;  /* fMaxTime:HAVOK */
            MF32(0x12D19C4u) = complex; /* fMaxTimeComplex:HAVOK */
            if (profile) {
                uint32_t bits;
                float frame_value = (float)frame;
                memcpy(&bits, &frame_value, sizeof bits);
                __atomic_store_n(&last_frame, bits, __ATOMIC_RELAXED);
                memcpy(&bits, &normal, sizeof bits);
                __atomic_store_n(&last_normal, bits, __ATOMIC_RELAXED);
                memcpy(&bits, &complex, sizeof bits);
                __atomic_store_n(&last_complex, bits, __ATOMIC_RELAXED);
                __atomic_fetch_add(&updates, 1, __ATOMIC_RELAXED);
            }
        }
    }
    f_D0DB50_guest(c);
}
/* Observe the real serial and parallel solver entry points. The existing
 * callbacks, world locks and Havok worker synchronization remain in place. */
#define SOLVER(address, argument) \
extern void f_##address##_guest(CPU *); \
void f_##address(CPU *c) { \
    if (enabled && fixed && profile) { \
        __atomic_fetch_add(&actual_steps, 1, __ATOMIC_RELAXED); \
        if (ARGF(argument) != (float)SKYDREAM_PHYSICS_STEP) \
            __atomic_fetch_add(&invalid_steps, 1, __ATOMIC_RELAXED); \
    } \
    f_##address##_guest(c); \
}
SOLVER(D4EE40, 0)
SOLVER(D50ED0, 2)
#endif
