# Generated TESV function families and exact duplicates

The first conservative audit examined 90,389 generated TESV entry points.
It found 2,501 redundant bodies in 543 exact groups. The implementation retains
all guest addresses and function symbols, replacing duplicate bodies with short
forwarding entries to one canonical implementation per group. There are 87,888
non-forwarding generated implementations after this pass; this is not a count
of independent engine algorithms or a claim that all remaining bodies differ.

The largest groups include 600 empty cdecl returns, 182 returns popping one
argument, 88 zero-return functions and 66 returns popping two arguments. Other
groups include constant returns, memory-access patterns, numeric operations
and tail-call thunks. These categories are syntax heuristics, not recovered
class or method names. The audit also lists 3,094 broader structural families
(including exact groups), which are **review candidates only**.

## What is safe to share

Only local C label identifiers are alpha-renamed before comparison. The rest
of the generated body must match exactly, including:

- all integer constants, guest addresses and field offsets;
- call targets, translated stores and reads, stack cleanup and register effects;
- string/character literals and comments, including diagnostics.

A hash groups candidates, but their full normalized bodies are compared again
before any replacement is written. Native override bodies (`_guest`), the
link-wrapped SKSE entry and functions with local static state, setjmp or compiler
introspection are excluded. This first pass excludes 32,287 such bodies. In
particular per-call-site indirect caches are not accidentally shared.

Different field offsets, vtable slots, addresses or constants are not automatically
merged, even when the instruction shape is otherwise the same. Such functions
may belong to one parameterizable family, but require reverse engineering and
ABI analysis first. Structural family matching never authorizes a rewrite.

## Entry points and build

`exe_table.c` is unchanged byte for byte. Guest-visible function identity, saved
addresses and virtual-table slots remain distinct. Each duplicate retains a
normal C forwarding symbol, so host function identity also remains distinct.
Canonical bodies receive a noinline prototype to prevent wrappers from simply
re-expanding the body. This keeps a single implementation while retaining the
old interfaces. It may add a host tail jump; no FPS improvement is asserted.

The tool is idempotent: existing forwarding entries are recognized and retained.
It does not rewrite initialized game memory, the ELF data blob or runtime shims.
It does not itself migrate the shared implementations to 64-bit object layouts;
the separate native-primitives work remains enabled.

```sh
# Inspect only. The report contains addresses/metadata, not generated bodies.
python3 tools/deduplicate_generated.py gen --report analysis/duplicate-audit.json
# Apply only exact matches.
python3 tools/deduplicate_generated.py gen --apply --report analysis/deduplication.json
```

The normal regeneration script applies the conservative pass after lifting.
Reports and modified generated code remain under ignored analysis/ and gen/.
The first pass removes 836,823 bytes of generated C after accounting for the
forwarding entries and noinline prototypes. This is source size, not ELF savings.

Validation covers literal/constant differences, protected/stateful functions,
label renaming, repeat application, cross-file forwarding, separate native entry
identity and matching CPU-state/stack results. Real-game builds and saved-game
smoke checks are required in addition to these synthetic tests.

Measured with the same Debian 11 / GCC 10 build configuration:
ELF size fell from 131,055,464 to 130,928,488 bytes (126,976 bytes saved).
The .text section shrank by 118,304 bytes and .eh_frame by 6,760 bytes.
All 32 Debian regression tests passed, as did the optional 20,000-case native
primitive oracle after deduplication. Host saved-game/SkyUI initialization was
smoke-tested; these checks do not establish complete gameplay parity.
