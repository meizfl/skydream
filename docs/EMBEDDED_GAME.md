# Link-time Skyrim game data

Skyrim no longer opens TESV.exe, TESV_unpacked.exe or guest/TESV.exe.pe while
starting. The desktop game build always links its initial compatibility data
into the ELF together with the statically recompiled game functions. There is
no runtime fallback to the Skyrim PE loader. Other game targets retain theirs.

The original matching, unpacked TESV 1.9.32 is still a **build input**. Generation
converts it into `gen/image/skyrim_image.bin`, native C metadata and an audit
JSON file. The generator validates PE32 headers, bounds, section overlap,
imports and TLS; SteamStub inputs are rejected. CMake links the blob into ELF
read-only data using a target-assembled `.incbin`, so no x86 object file is
introduced into a non-x86 build. CMake regenerates the assembler path when a
source tree moves. Generated files remain private under ignored `gen/`.

At startup native code copies initialized ranges into the zeroed compatibility
address space, binds the 266 imports through host shims, registers the module
and initializes static TLS. No PE file is opened or parsed at runtime. Existing
TLS callback behavior is preserved; callbacks are not newly executed.

This preserves the old 32-bit memory model, Windows-style in-memory headers,
resource tables, vtables and original section bytes needed by existing code.
Some original x86 instruction bytes therefore remain as compatibility data;
the CPU runs the recompiled native functions, not these bytes. This is not a
rewrite of the engine or elimination of every PE-shaped structure in memory.

## Build

Ordinary `tools/regen_gen.sh GAME` generates code and link-time data from the
same unpacked executable. For an existing generated-code checkout:

```sh
python3 tools/embed_game_image.py /path/to/TESV_unpacked.exe gen/image
JOBS=16 LB_GEN_JOBS=16 sh desktop/build.sh -DSKYDREAM_SKSE=ON -DSKYDREAM_SKYUI=ON
```

Alternatively configure with `-DSKYDREAM_GAME_IMAGE=/path/to/TESV_unpacked.exe`.
The supplied image must match the executable used to lift the game code.
Synthetic tests and `LB_BUILD_GAME=OFF` require no game data.

## Packaging and validation

Playable private folders need `Skydream`, native shared libraries and `Data/`.
A `guest/` directory and Windows executables/DLLs are unnecessary. INIs, saves
and the portable-profile mechanism work as before. Do not delete an existing
installation's files merely to test this: use a separate folder.

Validation includes synthetic compilation/linking with an original fixture
removed before execution, malformed-input rejection, independent real-TESV
memory/import parity, and saved-game/SkyUI smoke tests. Debian 11 regression
suite: 26 tests passed. Cross-architecture runtime validation is still separate.

The ELF now includes original TESV-derived initialization data and resources
in addition to statically recompiled game code. This is not all newly authored
GPL code. Source archives exclude generated data; existing NOTICE.md license
boundaries continue to apply to private binary packages.
