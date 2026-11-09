# Incremental replacement of TESV functions

The executable is already native ELF, but most game logic still uses lifted x86
registers and 32-bit guest addresses. This track replaces specific TESV entry
points with independently implemented host algorithms, keeping a small adapter
until callers and object layouts can migrate too. It does not make the entire
engine's objects or saves 64-bit in one step.

## First batch: frequently used memory and hashing primitives

Selection used the sampling profile from the embedded-data build, then real
entry counters in a saved-game run. Sampling estimates CPU time, not invocation
counts; this is not a global ranking of every TESV function. The two memory
routines and mapped CRC appeared among load-time hotspots. The fixed-width CRC
functions were included as the related hashing family.

| TESV 1.9.32 entry | Recovered behavior | Calls in one test run |
|---|---|---:|
| 0xF52E30 | overlap-safe memory move | 20,846,659 |
| 0xA4A570 | CRC of a little-endian 32-bit key | 14,015,379 |
| 0xF537F0 | second overlap-safe memory move entry | 1,531,162 |
| 0xA4A4E0 | translated bounded-string CRC, stop on translated zero | 266,049 |
| 0xA4A530 | bounded raw-buffer CRC | 18,804 |

Counts are cumulative at the last profiler report of a ~100-second load/gameplay
smoke run; they depend on the save, scene and input, and are not a benchmark.

`engine_primitives.c` uses host pointers and `size_t`, with no CPU context,
32-bit address conversion, guest allocation or original CRC lookup table. Memory
moves use the host's overlap-safe memmove. CRC uses polynomial 0xedb88320, initial
zero and no final complement. CRC output intentionally remains 32 bits: changing
checksum width or hashing eight bytes instead of a four-byte key would change
existing table behavior, not upgrade the implementation's pointer width.

The translated-string function indexes its translation table with **signed**
input bytes (-128..127) and stops when the translated byte is zero. This detail
was recovered from the executable's movsx operations, not assumed to be ordinary
ASCII lowercasing. The native API takes a correspondingly biased host pointer.

`engine_primitives_bridge.c` is the temporary cdecl boundary: it converts guest
addresses, stores the result in the old format, and preserves callee-saved guest
registers and stack cleanup. Direct calls and function-table/indirect calls bind
to the native entry wrappers. Fresh lift output and existing generated units
use the same host-override mechanism. No SKSE option is required.

Original lifted bodies remain as a test/reference fallback. Set
`SKYDREAM_NATIVE_PRIMITIVES=0` to select them for a diagnostic run. Default is
native. `SKYDREAM_PROFILE_PRIMITIVES=1` enables optional per-entry counters printed
by the sampling profiler reporter. Counters are disabled by default to avoid
adding an atomic operation to every hot call.

## Validation

- Synthetic tests: native pointers above 4 GiB, overlap in both directions,
  alignment/size boundaries, guard bytes, empty input, binary data, signed
  translation indices and little-endian key encoding.
- Optional private differential oracle: 20,000 cases over all five entry points,
  comparing buffer contents, documented returns, cdecl stack cleanup and
  callee-saved registers. Includes the original scalar and SSE selection paths,
  real jump-table landing pads and original lookup data. No Wine is involved.
- Native-default saved-game gameplay reached SkyUI configuration initialization
  and executed the counters above without an external PE image.

Run the source-only tests normally. For the original-code comparison, supply
matching private generated code and your unpacked TESV build input:

```sh
python3 tools/check_engine_primitives.py /path/to/TESV_unpacked.exe
```

The oracle emits/compiles original bodies only in a temporary directory and
never adds them to the public source tree. It checks preserved ABI, not equality
of every caller-clobbered x86 scratch register or SSE register. No controlled FPS
comparison has been performed. A full ARM64 game build remains unvalidated.

## Second batch: byte-string primitives

Four related TESV 1.9.32 entry points now use independent native cores in
`runtime/engine_strings.c`:

| Entry | Recovered contract | Native API |
|---|---|---|
| 0xF5B670 | length excluding terminating zero | `skydream_string_length` |
| 0xF520F0 | first matching unsigned byte, including terminator | `skydream_string_find` |
| 0xF55A90 | first matching byte in a bounded binary buffer | `skydream_bytes_find` |
| 0xF59140 | bounded string copy, zero-pad remainder, return destination | `skydream_string_copy_padded` |

These cores use host libc algorithms with native pointers and `size_t` throughout.
There is no CPU context, original lookup data, locale dependency or 32-bit pointer
conversion in the core. The cdecl bridge alone converts guest pointers/results;
existing game objects and callers still have their original 32-bit layout.
Inlined copies of the old algorithms elsewhere in lifted code are not replaced
by these four entry hooks. This batch is not the entire string library.

Character arguments are reduced to an unsigned byte. Searching for zero in a
string returns its terminator; a missing character returns null. The bounded
binary search does not stop on zero. Copying exactly `n` bytes does not append a
terminator if the source is at least `n` bytes long, and otherwise zero-pads the
remaining destination. Zero-length bounded operations access neither pointer.
Other inputs must satisfy the libc storage contracts; copy ranges must not
overlap. This does not add encoding conversion or reinterpret strings as UTF-8.

### Concurrency

The native cores have no mutable shared state, locks, per-call allocation or
worker creation. Independent callers can execute concurrently; immutable input
may be shared, but writes to the same output still need caller synchronization.
Host libc can select its optimized implementation for the running architecture.
We avoid a new worker pool for small string operations: game task scheduling
remains the responsibility of callers. No multithreaded FPS improvement or full
64-bit engine/object migration is claimed.

`SKYDREAM_NATIVE_STRINGS=0` selects original lifted bodies for diagnostics.
Native is the default, independent of SKSE. `SKYDREAM_PROFILE_STRINGS=1` enables
atomic per-entry counters in profiler reports; counters are off by default.
Configuration is read at startup before game worker threads are created, not
changed concurrently while they run.

### Validation

- `python3 -m unittest discover -s tools -p test_engine_strings.py`: native
  pointers above 4 GiB; binary/high-bit bytes; int-to-byte conversion; truncation,
  padding and output guards; protected-page boundaries; zero-length operations;
  160,000 test rounds across 16 simultaneous pthread workers with private output
  and shared immutable input. This stress test is not a formal race detector.
- `python3 tools/check_engine_strings.py --gen /path/to/private/gen`: 32,000
  differential cases against self-contained original lifted bodies, comparing
  return values, writes, cdecl cleanup, direction flag and callee-saved registers.
  As with the first batch, scratch registers and old stack-local writes are not
  required to match. No original game image, Wine or public generated code is
  needed for this oracle; only private generated references are used temporarily.

The host game build passed all 34 regression tests (one optional private movie
case skipped). A 65-second Wayland saved-game smoke run reached SkyUI
`OnConfigManagerReady`; the 60-second report counted 187,529 length calls,
127,867 character searches and 950 bounded byte searches. Padded copy was not
exercised by that scene (its counter was zero); its behavior is covered by the
differential and concurrent tests above. These counts describe this run only.
