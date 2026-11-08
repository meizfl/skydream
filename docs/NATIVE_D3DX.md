# Native D3DX9 helper replacement

Skyrim's 18 imported D3DX9 functions are implemented by native host code.
`d3dx9_42.dll.pe` is no longer loaded, linked as lifted code, needed for
regeneration, or copied by the Skyrim packer. The remaining guest memory
image is TESV.exe.pe. The renderer remains native DXVK translating D3D9 to Vulkan.

## Implementation

- Alias-safe vector, plane and matrix mathematics, including inverse/determinant.
- DDS metadata and mip/face/slice parsing for 2D, cube and volume textures.
- Direct uploads of BC1-3/DXT and common integer/float DDS formats. Two-dimensional
  uploads fall back to BGRA conversion when a host format is unavailable.
- Native raster decoding using stb_image; common pixel formats, block decoding,
  nearest/bilinear scaling and color keys for surface uploads. Surface copies
  first try the host D3D9 device, then a CPU staging path.
- Native ELF vkd3d-shader 2.0 compiles HLSL to SM1-3 bytecode for DXVK. The bridge
  implements ID3DXBuffer and CTAB reflection/setters for flat scalar, vector,
  matrix and array constants, including vertex/pixel float, integer and boolean
  registers. Guest macros and ID3DXInclude callbacks cross the stdcall boundary.

This is the imported Skyrim subset, not a complete D3DX SDK. Paletted textures,
encoding/recompression to BC formats, nested struct reflection/setters and
arbitrary D3DX shader-compiler flags are not implemented. vkd3d-shader's compiler
is not guaranteed to produce identical bytecode or accept every legacy HLSL
extension. Unsupported conversions return errors rather than fabricated data.
Native cube/volume BC emulation on GPUs without BC support is not covered.

## Build and test

Build the compiler before packaging:

```sh
sudo apt install build-essential bison flex libjson-perl wine64-tools curl xz-utils libvulkan-dev pkg-config
JOBS=16 sh tools/build-native-shader.sh
JOBS=16 sh desktop/build.sh -DSKYDREAM_SKSE=ON -DSKYDREAM_SKYUI=ON
python3 -m unittest tools.test_d3dx -v
./build/lb /path/to/game /tmp/d3dx-profile --d3dx-test
```

WIDL from wine64-tools is only a compiler build tool. Playing and HLSL compilation
use native ELF code, without Wine or a Windows compiler DLL. The build script
pins source archives and verifies SHA-256 hashes. Corresponding LGPL compiler
sources, build inputs and script accompany the portable runtime under `sources/`.
`SKYDREAM_VKD3D_SHADER` can override the native shared-library path for diagnostics.

Validation on 2026-11-09: six math/DDS tests, 24-test Debian 11 regression suite
including the private Bink movie, guest ABI HLSL/CTAB tests, and saved-game/SkyUI
smoke runs with only TESV.exe.pe. The Debian 11 runtime's maximum referenced glibc
symbol version is GLIBC_2.30; it targets glibc 2.31 or newer. Further gameplay
and mod testing is still needed for parity beyond these exercised paths.
