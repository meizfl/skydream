# Desktop builds: Linux, Windows and macOS

The build entry point is now shared across 64-bit desktop hosts. Runtime OS calls
are isolated in `runtime/host.c`, with POSIX and Win32 implementations. This is
not a promise that every platform has been gameplay-tested. The public CI builds
host runtime/renderer code without proprietary game input; a full local game
build still requires generated code and initialization data from Skyrim 1.9.32.

| Target | Compiler | Validation |
|---|---|---|
| Linux x86-64 | GCC or Clang | Local full game build, regression tests and host-platform CTest |
| Windows x86-64 | MinGW-w64 GCC (MSYS2 UCRT64 recommended) | Local cross-build; native CI build and platform test configured |
| macOS Intel | Apple Clang | Separate `macos-15-intel` CI job, SDK/build not available locally |
| macOS Apple Silicon | Apple Clang | Separate `macos-15` arm64 CI job, SDK/build not available locally |

MSVC is not supported: the recompiled code and native ABI headers use GNU C/C++
extensions. CMake emits a clear diagnostic rather than selecting incompatible
MSVC flags. Windows builds create an ordinary native 64-bit Windows executable;
Linux/macOS builds do not acquire a Wine runtime dependency.

## Common commands

Install SDL2 development files, Vulkan headers, FFmpeg development libraries
(libavcodec, libavutil, libswresample, libavformat, libswscale), pkg-config,
Python 3.10+, Ninja, and a supported C/C++17 toolchain. Then:

```sh
# No game assets: compile the host runtime, renderer and platform test.
cmake --preset host-check
cmake --build --preset host-check --parallel 8
ctest --test-dir build/host-check/desktop --output-on-failure

# Full local game, after generating gen/ from your own installation:
cmake --preset desktop -DSKYDREAM_SKSE=ON -DSKYDREAM_SKYUI=ON
cmake --build --preset desktop --parallel 8
```

Presets require CMake 3.21+. The direct build remains compatible with CMake 3.18
(e.g. Debian 11): `cmake -S desktop -B desktop/build -G Ninja ...`, followed by
`cmake --build desktop/build`. Generated-code compilation has a separate
`LB_GEN_JOBS` limit because large units can require gigabytes of RAM each.

The existing Linux `desktop/build.sh`, `desktop/run.sh`, and Linux packaging
workflow remain available. `desktop/package.py` packages Linux dependencies;
it is not a Windows/macOS application-bundle packager.

## Linux

The existing dependency instructions still apply. Vulkan functions now resolve
through SDL's loader rather than a hard-coded `-lvulkan` link. Linux keeps its
Wayland/X11 auto-selection, existing XDG save paths, null-page guard, lazy 4-GiB
mapping, sampling profiler and portable-profile detection.

## Windows

Use an **MSYS2 UCRT64** shell, not the MSYS compiler environment:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-ffmpeg
cmake --preset host-check
cmake --build --preset host-check --parallel 8
ctest --test-dir build/host-check/desktop --output-on-failure
```

The Win32 backend reserves guest address space without eagerly committing 4 GiB.
A vectored exception handler commits individual data pages on demand, excluding
the guest null guard and execute faults. Decommit makes subsequently reallocated
pages zero-filled. The host test touches both ends of the 32-bit address range
and concurrently commits pages from 16 threads.

POSIX-style positioned reads/writes preserve descriptor position under per-hash
locks. The Windows build uses winpthreads, not a new guest thread implementation.
Named-FIFO testing input and Linux signal sampling are unavailable on Windows;
SDL window/input/audio and normal runtime counters remain enabled. Path and
filesystem behavior, particularly non-ASCII installations, needs native Windows
coverage beyond the current compilation/platform test.

For cross-compilation from Linux, use `cmake/toolchains/mingw64.cmake` and provide
**target** SDL2/FFmpeg/pkg-config paths and Vulkan headers. Never reuse Linux
FFmpeg libraries for this build. `SKYDREAM_FFMPEG=OFF` is available for compiler
checks only: videos and compressed voices will be unavailable. It is not a
complete playable package. Run a native Windows build with FFmpeg enabled for
normal use. Windows runtime DLLs (SDL2, FFmpeg, MinGW C++/pthread runtime and DXVK)
must be available beside the executable or on PATH; the DXVK DLL is copied next
to the local game executable by the build.

## macOS: Intel and Apple Silicon

```sh
brew install cmake ninja pkg-config sdl2 vulkan-headers ffmpeg
cmake --preset host-check -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DCMAKE_OSX_ARCHITECTURES=arm64  # x86_64 on Intel
cmake --build --preset host-check --parallel 8
ctest --test-dir build/host-check/desktop --output-on-failure
```

Build each architecture with dependencies of that architecture. A universal
binary is not produced by linking Intel libraries into an arm64 build. CI uses
two native runners and checks the resulting dylib architecture with `lipo`.

For graphics execution install a Vulkan loader and MoltenVK from the official
Vulkan SDK. SDL performs Vulkan library loading; DXVK enables portability
enumeration and the portability-subset device extension when advertised. This
handles discovery but does not emulate unsupported GPU features. MoltenVK
feature compatibility, actual game rendering, audio, signing, notarization and
an `.app` bundle have not been validated by a source-only CI build.

Mach-O uses its own embedded-data section/symbol prefix and `@loader_path`
install paths. Darwin page discard explicitly remaps anonymous pages because
`MADV_DONTNEED` does not promise Linux-style zero fill. The Linux-only sampling
profiler is disabled; runtime counters remain available.

## Runtime files and shared compiler

An existing `portable-profile` beside the executable wins on every desktop.
Otherwise Windows/macOS use SDL's native per-user application-data location;
Linux preserves `$XDG_DATA_HOME/skyrim-native/save` or the existing HOME fallback.
`--x11` / `--wayland` are Linux backend options, not Windows/macOS backends.

Native HLSL compilation still requires a **target-OS vkd3d-shader 2.0 shared
library**, supplied with `SKYDREAM_SHADER_LIBRARY` for installation or
`SKYDREAM_VKD3D_SHADER` at runtime. Default names are `libvkd3d-shader.so.1`,
`libvkd3d-shader.1.dylib`, and `libvkd3d-shader-1.dll`. The existing pinned build
script is a Linux dependency builder, not a cross-platform compiler installer.
Do not package its ELF output inside Windows/macOS builds.

SKSE entry interception now uses the same normal host-override mechanism as
other native replacements. It no longer depends on GNU ld `--wrap`. Public
artifacts still contain no Skyrim assets or generated proprietary source.
