# Skydream — Skyrim for Linux

Experimental native Linux port of **The Elder Scrolls V: Skyrim (2011 / Legendary Edition, 1.9.32)**, separated from [ttom795's Skyrim_Android](https://github.com/ttom795/Skyrim_Android) 0.30.6.

This is static recompilation, not a reconstruction of Bethesda's original source code. The runtime translates Windows API calls itself and renders through patched DXVK to Vulkan. Wine, Proton and Android tooling are not required.

This source repository contains no game executable, DLL, assets, saves, lifted game code or compiled game binary. You must supply your own matching game files and generate the compiled code locally. Special Edition and Anniversary Edition are unsupported.

## Features

- Native Linux x86-64 executable, SDL2 window/input/audio and Vulkan graphics.
- Automatic native Wayland or X11 selection; explicit backend overrides.
- F11 fullscreen toggle. Render resources are allocated at monitor resolution on startup to avoid corrupting Skyrim's image-space targets during live resolution changes.
- Relative mouse camera control during gameplay; a free system cursor in menus and on focus loss.
- SDL gamepads, hotplug, sticks, triggers, buttons and vibration; switching between controller and mouse/keyboard input.
- PCM/ADPCM audio and FFmpeg decoding of xWMA dialogue/music.
- Native Bink 1 video/audio playback through FFmpeg and SDL; no Bink PE image.
- Native X3DAudio spatial math using the zlib-licensed F3DAudio module; no X3DAudio PE image. See [replacement scope and validation](docs/NATIVE_MODULES.md).
- Parallel function discovery through `DISCOVER_JOBS`.
- A relocatable game-folder packer with direct executable launch.

This remains experimental. Broad gameplay, mod compatibility, other executable versions and other Linux distributions have not been validated. Native dependencies are resolved from the host system; a packaged game folder is not a universal AppImage.

## Experimental SKSE compatibility

Build with `JOBS=16 LB_GEN_JOBS=16 sh desktop/build.sh -DSKYDREAM_SKSE=ON` for the independent classic Papyrus compatibility layer. Add `-DSKYDREAM_SKYUI=ON` to enable the experimental SkyUI interface without launch scripts or environment variables. Both options are disabled by default.

The layer registers 90 functions and provides native Scaleform, menu/input/mod-event bridges, extended inventory fields and translation loading. SkyUI 5.1 inventory and MCM have been tested, and the tester confirmed settings persistence. This remains partial compatibility: favorites, active effects and crafting require more work; Windows DLL plugins and SKSE co-saves are unsupported. See [scope, validation and build instructions](docs/SKSE_COMPATIBILITY.md).

For a direct portable launch, place a `portable-profile` directory beside the game binary to keep settings, plugin lists and saves there. Without it, the default XDG profile is used. An explicit SAVE_DIR overrides both.


Five hot TESV entry points now use independent native memory-move/CRC cores with 64-bit host pointers and size_t. The old guest ABI remains only at their adapters; this is not a fully migrated 64-bit object model. See [incremental engine replacement](docs/NATIVE_ENGINE.md) for call counts, differential tests and diagnostic fallback.

Four byte-string entry points now also use native host-width libc cores (length,
character/byte search, padded copy), with no mutable shared state or per-call
worker creation. Validation includes original-body differential checks, protected
memory boundaries and 16 simultaneous callers. The guest ABI remains 32-bit at
the adapter boundary; see [native engine progress](docs/NATIVE_ENGINE.md).


Generated TESV bodies are conservatively deduplicated by exact equality after local-label renaming. Guest entry addresses remain intact; structurally similar functions with differing constants are only reported for review. See [function-family audit](docs/DEDUPLICATION.md).

## Build dependencies

On Debian/Ubuntu:

```sh
sudo apt install clang cmake ninja-build pkg-config python3-venv \
    bison flex libjson-perl wine64-tools curl xz-utils \
    libsdl2-dev libvulkan-dev libavcodec-dev libavutil-dev libswresample-dev libavformat-dev libswscale-dev ffmpeg
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.txt
```

Use Python 3.10 or newer, a C++17 compiler, 64-bit Linux and a Vulkan 1.1-capable driver. `ffmpeg` is used by the audio regression test. Builds of lifted game code can need 1–2 GB of RAM per concurrent compiler process; the default generated-code build concurrency is two.

## Supply game inputs

Create a local `GAME` folder containing:

```text
GAME/
  TESV.exe
  TESV_unpacked.exe
  Skyrim_Default.ini
  low.ini
  Data/
    Skyrim.esm
    ...
```

`TESV_unpacked.exe` must be the unpacked 32-bit TESV 1.9.32 executable corresponding to your installation. If your executable is already unpacked, a local copy named `TESV_unpacked.exe` is sufficient. This repository does not provide or obtain game files. D3DX9, Bink and X3DAudio are implemented natively; their DLL images are no longer required. See [Native D3DX](docs/NATIVE_D3DX.md) for scope and limitations.

The local Linux build was exercised with TESV SHA-1 `67a597df3bb8c5db94b9032f8179d29631b1145d`. Runtime hooks target Skyrim 1.9.32; changing the executable version requires checking those hooks, not just regenerating C.

## Generate and compile

```sh
. .venv/bin/activate
JOBS=16 sh tools/build-native-shader.sh
DISCOVER_JOBS=6 sh tools/regen_gen.sh ./GAME
JOBS=2 LB_GEN_JOBS=2 sh desktop/build.sh -DLB_BUILD_GAME=ON
./desktop/run.sh ./GAME
```

Discovery and lifting produce local `analysis/` and `gen/` directories. The generation script also records hashes in `analysis/linux-inputs.json`; these record build-time provenance. Link-time initialization data is also generated under `gen/image/`. These directories are ignored by Git and must not be published.

To check the runtime and DXVK build without any game inputs:

```sh
sh desktop/build.sh -DLB_BUILD_GAME=OFF
```

This compiles the host runtime and graphics library; it does not produce a playable executable. Re-enable `LB_BUILD_GAME=ON` after generating the game code.

## Run

```sh
./desktop/run.sh ./GAME
./desktop/run.sh ./GAME ./save
./desktop/run.sh --wayland ./GAME
./desktop/run.sh --x11 ./GAME
```

The desktop session selects Wayland or X11 automatically, with the other backend as a startup fallback. `SDL_VIDEODRIVER`, `--wayland` and `--x11` override automatic selection. Graphics rendering uses Vulkan under either window backend.

Default saves and INIs are stored under `$XDG_DATA_HOME/skyrim-native/save`, or `$HOME/.local/share/skyrim-native/save`. `LB_LOG=1` enables runtime diagnostics on stderr. `LB_WINDOW=1280x720` selects the initial window size. F11 toggles fullscreen; monitor render resolution remains fixed for that process.

## Build a private game folder

```sh
python3 desktop/package.py ./GAME ./build/Skyrim-Linux
./build/Skyrim-Linux/Skyrim
```

The packer leaves the original installation intact and refuses to overwrite an existing output directory. It copies game data, INIs, the native executable and DXVK into a directory that can be moved as a whole. A direct executable launch locates the game beside itself, independently of the working directory. Saves remain in the XDG directory.

No external PE images are required at runtime. TESV-derived initialization data is linked into the ELF during the build; only `Data/` and native libraries accompany the executable. See [link-time game data](docs/EMBEDDED_GAME.md).

**The resulting package contains game files and compiled game code. It is for your own installation, not for committing or uploading to this source repository.**

## Tests

```sh
. .venv/bin/activate
python3 -m unittest discover -s tools -p 'test_*.py'
```

Tests use synthetic inputs and temporary files: discovery determinism, guest memory visibility, INI sections, launch paths/backend overrides, menu capture policy, session backend selection and xWMA decoding. They do not require or include Skyrim assets.

## Downloadable artifacts

Each successful GitHub Actions run uploads a runtime-check archive (DXVK only), a source archive and reports with build/test logs and SHA-256 checksums. Reports are also uploaded on failures. Artifacts are retained for 30 days. The runtime-check archive is not a playable game binary: CI has no original game inputs or generated game code.

A native executable built locally can be packaged separately, without the Data directory or external PE files:

```sh
cmake --install desktop/build --prefix "$PWD/build/install"
python3 desktop/make-artifact.py \
    --binary build/install/bin/lb \
    --dxvk build/install/lib/skyrim-native/libdxvk_d3d9.so \
    --inputs-manifest analysis/linux-inputs.json \
    --output dist/skydream-linux-x86_64.tar.gz
```

This separate archive contains the native `Skydream` executable, DXVK, build/input metadata, licensing notices and checksums. It contains statically recompiled game code and original TESV-derived initialization data/resources embedded in ELF; users still supply their matching Data folder. System SDL2, FFmpeg and Vulkan dependencies must match the build host ABI, as listed in the archive. The CI source archive does not contain this locally generated binary.

## Layout and provenance

- `runtime/`: host API shims, input, audio, graphics bridge and game hooks.
- `tools/`: discovery, lifting, bridge generation and tests.
- `desktop/`: CMake build, launch and packaging tools.
- `third_party/dxvk/`: modified vendored DXVK-native sources and shader headers.
- `docs/UPSTREAM_README.md`: preserved documentation from the supplied Android snapshot.

The Android launcher, bundled SDL2 tree, generated game code, local inputs and build products have deliberately been left out. Shared runtime sources retain Android conditional branches for provenance; this repository builds only the Linux target.

## License

Our original Linux-port contributions are licensed under **GNU GPL version 3 only** (`GPL-3.0-only`); see [LICENSE](LICENSE). Third-party components retain their own licenses and notices.

The inherited Android runtime/recompiler snapshot did not include a top-level license. This GPL grant covers our contributions, not rights in inherited code that we do not own. See [NOTICE.md](NOTICE.md) for provenance and the unresolved upstream license boundary.
