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
