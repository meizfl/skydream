# Fixed-step Havok scheduling (TESV 1.9.32)

## Current behavior

The default physics mode uses a native double-precision accumulator, a fixed
**1/60 simulation-second** solver step, and monotonic real elapsed time measured
at the physics scheduling boundary. Rendering is not capped to 60 FPS. At high
render rates some frames perform no solver step; at low rates several steps may
run. Game-time scaling changes how fast time enters the accumulator, not the size
of a solver step. A 0.1 slow-motion scale therefore produces fewer ticks rather
than a different integration quantum.

`runtime/physics_timing.c` owns the pure host-width policy. State is explicit in
`SkydreamPhysicsClock`; independent simulation timelines need separate states.
The bridge owns the single TESV scheduling timeline on the existing game thread.
No new worker pool or timer thread is created. The existing world locks,
per-step callbacks, serial solver and Havok job-system synchronization remain.
This does not replace the solver itself or its guest object layouts with a
64-bit physics engine.

The fixed scheduler replaces the arithmetic at entry `0xD0DB50` and publishes:

- `0x12D18E0`: fixed step duration;
- `0x12D18DC`: zero or an integral multiple of that step for this batch;
- `0x1BB5E20`: diagnostic mirror of the native accumulator remainder.

The existing world update at `0xD12BE0` runs the corresponding complete steps.
Both ordinary and complex modes use the same integration quantum. The old
variable complex-step budget is not used in fixed mode. Profiling wrappers at
`0xD4EE40` and `0xD50ED0` observe the actual serial/parallel solver arguments,
including indirect calls, without changing solver behavior.

## Frame motion is a separate clock

The first fixed-step prototype exposed the batch duration to every consumer of
`0x12D18DC`. Reverse engineering found four motion/velocity functions using that
value to convert **per-frame** displacement into velocity: `0xD1DA60`,
`0xD369E0`, `0xD64D70`, `0xD65860`. Dividing frame displacement by zero/one/multiple
solver ticks caused irregular movement and running animations.

`tools/prepare_physics_motion.py` now redirects only the five audited loads in
those functions to a dedicated frame-motion snapshot. Positive submitted frame
time is retained; a high-resolution scaled interval handles active sub-ms frames
where the old integer-millisecond timer produced zero. Paused motion is zero.
Solver and other physics reads are unchanged. Atomic snapshot access avoids
adding a shared mutable scratch value to worker-local CPU state. The tool
requires exact expected load counts and fails on an unexpected input layout;
it is idempotent and is called by the normal generated-code preparation step.

This keeps character motion on its frame clock while rigid-body integration
uses fixed ticks. General render interpolation between previous/current rigid
body poses is **not implemented** by this patch. The accumulator exposes an
`alpha` value for that future work; it does not fabricate interpolated transforms
or claim that all visible object motion updates at the render frequency.

## Pauses, stalls and work limits

The timer instance's pause count and zero submitted/nonzero unscaled time gates
suppress accumulation during menus/loading. On resume the bridge establishes a
new time origin instead of catching up menu time. A partial-step remainder is
retained. Invalid elapsed values/scales leave state unchanged.

Catch-up is limited to eight complete solver steps per rendered update, with at
most 0.25 seconds of accepted real elapsed time and 0.25 simulation-seconds of
pending debt. Excess time is explicitly counted in the native state's
`discarded` field. After a long stall, real-time synchronization can be sacrificed
to bounded work; no large physics step or unbounded catch-up loop is introduced.
This policy is deliberate and tested, not a claim of exact conservation through
arbitrarily long hangs. Ordinary accepted time is consumed or retained.

## Modes and unlocked presentation

- Default: fixed-step accumulator, no SKSE required.
- `SKYDREAM_PHYSICS_MODE=adaptive`: return to the previously tested adaptive
  high-resolution maximum-step correction.
- `SKYDREAM_HAVOK_FIX=0`: unchanged original scheduler/INI timing behavior.
- `SKYDREAM_PROFILE_PHYSICS=1`: optional atomic timing and real solver-step
  counters in watchdog reports. `nonfixed` counts solver arguments differing
  from the fixed quantum. Counters are disabled by default.

Desktop presentation is uncapped by the binary itself, on both CreateDevice and
Reset (including fullscreen transitions), without `dxvk.conf` or environment
settings. `SKYDREAM_VSYNC=1` explicitly opts back into VSync. Android presentation
behavior is unchanged. Explicit external DXVK/driver/overlay/compositor limits
can still override presentation independently. The fixed-step test folder no
longer contains a `dxvk.conf` file.

## Validation and limits

44 source regression tests passed (one optional private Bink-movie test skipped).
Fixed-clock tests cover 15..10000 render updates/sec, constant 60 simulation
steps/sec, pause/resume, slow motion, separate timeline state, stalls, bounded
work, invalid inputs and time accounting. A deterministic **real bridge** test
covers 30..2000 Hz with the original zero/one-ms timer pattern and preserved ABI.
Motion-clock tests ensure that character updates receive frame time even when
the solver batch is zero; preparation tests check scope, idempotence and failure
without partial writes. These are tests of code and scheduling, not collision
simulations or claims that the game rendered at every tested rate.

`python3 tools/check_physics_timing.py --gen /path/to/private/gen` retains the
128,000-case oracle for **adaptive** mode against the original lifted scheduler.
It is not an equivalence oracle for fixed mode, whose batching deliberately
changes. Original code is only compiled in a private temporary directory.

A 95-second Wayland saved-game smoke run reached SkyUI initialization and
observed 4,612 real solver calls, with zero nonfixed arguments. The first
prototype's animation regression was corrected by the separate motion clock;
the user subsequently reported that running animations looked normal. This
feedback is a targeted gameplay check, not complete validation of all quests,
ragdolls, moving platforms, cell transitions, mods or long sessions.

## Original diagnosis and adaptive fallback

The original scheduler used fixed `fMaxTime:HAVOK` and
`fMaxTimeComplex:HAVOK` values despite short render frames. The known LE problem
and dynamic-limit approach are described by
[Havok Fix's author](https://www.nexusmods.com/skyrim/mods/91598?tab=description).
The old timer also derives floats from integer milliseconds, losing precision
at e.g. 2000 FPS (0.5 ms/frame).

Our adaptive fallback measures elapsed seconds with Linux `CLOCK_MONOTONIC`,
using the original unscaled timer only at startup/clock failure. Its normal
limit is `min(elapsed, 1/60)` and complex limit is `normal/(1-30*normal)`, retaining
a 30-Hz rate offset. That rate-offset policy is independently documented by
[SSE Display Tweaks](https://github.com/SlavicPotato/SSEDisplayTweaks/blob/master/SSETweaks/havok.cpp).
No SE plugin code or addresses are used. There is no refresh-derived lower
interval bound, and no 240/1000/2000-FPS ceiling in either native timing policy.

The final binary was additionally launched without a dxvk.conf file and with
DXVK_CONFIG / DXVK_CONFIG_FILE / SKYDREAM_VSYNC unset. DXVK selected
VK_PRESENT_MODE_IMMEDIATE_KHR before and after the scripted F11 fullscreen
transition. Fixed solver monitoring continued reporting zero nonfixed steps.
