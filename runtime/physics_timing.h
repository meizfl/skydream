/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* Pure host-width timing policy. Returns 0 for paused/invalid frame durations;
 * callers then retain their last valid settings. No global state or workers. */
int skydream_physics_limits(double unscaled_seconds, float *normal, float *complex);

#include <stdint.h>
/* One state per simulation timeline, owned by its scheduling thread. */
typedef struct {
    double remainder;
    double discarded;
    uint64_t ticks;
} SkydreamPhysicsClock;
typedef struct {
    unsigned steps;
    double seconds;
    double alpha;
} SkydreamPhysicsBatch;
#define SKYDREAM_PHYSICS_HZ 60u
#define SKYDREAM_PHYSICS_MAX_STEPS 8u
#define SKYDREAM_PHYSICS_STEP (1.0 / SKYDREAM_PHYSICS_HZ)
void skydream_physics_reset(SkydreamPhysicsClock *clock);
SkydreamPhysicsBatch skydream_physics_advance(SkydreamPhysicsClock *clock,
                                             double elapsed, double scale,
                                             int active);
