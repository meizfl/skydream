/* SPDX-License-Identifier: GPL-3.0-only */
#include "physics_timing.h"
#include <math.h>

int skydream_physics_limits(double seconds, float *normal, float *complex) {
    if (!isfinite(seconds) || seconds <= 0.0)
        return 0;
    /* Retain 60-Hz normal / 30-Hz complex budgets at low FPS. At high
     * FPS shrink both, retaining the 30-Hz separation between their rates.
     * No lower bound tied to monitor refresh: FPS can exceed refresh rate. */
    double step = fmin(seconds, 1.0 / 60.0);
    float n = (float)step;
    float c = (float)(step / (1.0 - 30.0 * step));
    if (!isfinite(n) || n <= 0.0f || !isfinite(c) || c <= 0.0f)
        return 0;
    *normal = n;
    *complex = c;
    return 1;
}

void skydream_physics_reset(SkydreamPhysicsClock *clock) {
    *clock = (SkydreamPhysicsClock){0};
}
SkydreamPhysicsBatch skydream_physics_advance(SkydreamPhysicsClock *clock,
                                             double elapsed, double scale,
                                             int active) {
    SkydreamPhysicsBatch result = {0};
    if (!active || !isfinite(elapsed) || elapsed < 0.0 ||
        !isfinite(scale) || scale <= 0.0)
        return result;
    /* Bound both catch-up work and debt after stalls. Never send a large or
     * fractional step to Havok. Discarded time is explicit and observable. */
    double accepted = fmin(elapsed, 0.25);
    clock->discarded += (elapsed - accepted) * scale;
    double submitted = accepted * scale;
    if (!isfinite(submitted) || submitted > 0.25) {
        clock->discarded += submitted - 0.25;
        submitted = 0.25;
    }
    double pending = clock->remainder + submitted;
    if (pending > 0.25) {
        clock->discarded += pending - 0.25;
        pending = 0.25;
    }
    unsigned steps = (unsigned)floor((pending + 1e-12) / SKYDREAM_PHYSICS_STEP);
    if (steps > SKYDREAM_PHYSICS_MAX_STEPS)
        steps = SKYDREAM_PHYSICS_MAX_STEPS;
    clock->remainder = fmax(0.0, pending - steps * SKYDREAM_PHYSICS_STEP);
    clock->ticks += steps;
    result.steps = steps;
    result.seconds = steps * SKYDREAM_PHYSICS_STEP;
    result.alpha = fmin(clock->remainder / SKYDREAM_PHYSICS_STEP, 1.0);
    return result;
}
