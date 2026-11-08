# Attribution and license boundaries

This Linux source tree derives from **Skyrim_Android 0.30.6** by ttom795:
https://github.com/ttom795/Skyrim_Android

The supplied snapshot's README is preserved in `docs/UPSTREAM_README.md`. The original snapshot did not include a top-level license file for its runtime and recompilation tools. Our original Linux-port code, tools, tests and documentation are offered under GNU GPL version 3 only (SPDX: GPL-3.0-only); see LICENSE. This does not relicense inherited code or represent it as newly authored. Permission for redistribution and licensing of the inherited runtime/recompiler needs to be established with the upstream author; no blanket GPL grant for those inherited portions is asserted.

`third_party/dxvk` is an altered DXVK-native source distribution, based on the snapshot's patched native-1.9.2b tree:
https://github.com/Joshua-Ashton/dxvk-native

Its original zlib/libpng license has been restored as `third_party/dxvk/LICENSE`. Linux changes include drawable-size queries and full-surface swap-chain extents. Other modifications inherited from the Android snapshot remain present. Individual bundled headers and source files retain their own notices, including Wine-derived DirectX headers, Vulkan/SPIR-V headers and OpenVR headers; the DXVK license does not replace those notices.

SDL2 and FFmpeg are linked from the system installation rather than vendored here. Their respective licenses remain applicable.

The Elder Scrolls, Skyrim and Bethesda names and game assets belong to their respective owners. This repository contains host runtime/tool sources, not the game or generated game code. Build inputs, generated outputs and private game packages remain local and are ignored by Git.

The optional `SKYDREAM_SKSE` feature is an independent, incomplete implementation of classic Papyrus extension interfaces. No original SKSE code, binaries or scripts are included or relicensed. The original SKSE package has separate distribution restrictions. See docs/SKSE_COMPATIBILITY.md.

Native spatial audio uses a vendored F3DAudio subset from FAudio 26.10, commit 6839b88e304a046ae1a609ff14a085371e02a4e0, under its original zlib license. It is not relicensed as original Skydream code. See third_party/f3daudio/LICENSE and README.skydream for attribution and modifications.

Native D3DX image decoding uses stb_image (MIT/public domain, original notices in third_party/stb). Native HLSL compilation uses the separately built ELF libvkd3d-shader 2.0 (LGPL-2.1-or-later). Public headers and license are in third_party/vkd3d; tools/build-native-shader.sh retrieves hash-pinned corresponding sources and build headers. WIDL is used only while building the compiler. No Wine runtime is required by this compiler.

Private game builds also link TESV-derived initialization data and resource sections into the ELF. Removing external PE files does not make those original data newly authored GPL code; generated game code and initialization blobs remain excluded from this source repository.
