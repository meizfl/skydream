#!/bin/sh
# Build only the native ELF HLSL compiler, never the Windows or D3D12 runtime.
# Debian dependencies: build-essential bison flex libjson-perl wine64-tools
# libvulkan-dev pkg-config curl xz-utils. WIDL is a build tool, not a runtime dependency.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CACHE=${SKYDREAM_SHADER_CACHE:-"$ROOT/build/native-shader-source"}
PREFIX=${SKYDREAM_SHADER_PREFIX:-"$ROOT/build/native-shader"}
mkdir -p "$CACHE" "$PREFIX"
fetch() {
    url=$1; dest=$2; digest=$3
    if [ ! -f "$dest" ]; then curl -fL --retry 3 "$url" -o "$dest.tmp"; mv "$dest.tmp" "$dest"; fi
    printf '%s  %s\n' "$digest" "$dest" | sha256sum -c -
}
fetch https://dl.winehq.org/vkd3d/source/vkd3d-2.0.tar.xz "$CACHE/vkd3d-2.0.tar.xz" 9ad29bb236808186a47ec66e853b21e6fca59b9dc62a9474b05d5e3eda2710ef
fetch https://codeload.github.com/KhronosGroup/SPIRV-Headers/tar.gz/6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1 "$CACHE/spirv-headers.tar.gz" d035de73b4085301bf921dc66997ce993fbf7c0d78f3308e67b6be2513a8b88c
fetch https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.3.228 "$CACHE/vulkan-headers.tar.gz" e5a4dcdfb9361ee8d1b9401ed337b65e7c34a54224b99a9cde303a650a9cb350
[ -d "$CACHE/Vulkan-Headers-1.3.228" ] || tar -xzf "$CACHE/vulkan-headers.tar.gz" -C "$CACHE"
[ -d "$CACHE/vkd3d-2.0" ] || tar -xJf "$CACHE/vkd3d-2.0.tar.xz" -C "$CACHE"
[ -d "$CACHE/SPIRV-Headers-6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1" ] || tar -xzf "$CACHE/spirv-headers.tar.gz" -C "$CACHE"
mkdir -p "$CACHE/build"
cd "$CACHE/build"
if [ ! -f Makefile ]; then
    CPPFLAGS="${CPPFLAGS:-} -I$CACHE/Vulkan-Headers-1.3.228/include -I$CACHE/SPIRV-Headers-6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1/include" \
      "$CACHE/vkd3d-2.0/configure" --prefix="$PREFIX" --disable-doxygen-doc --without-xcb --without-ncurses --without-opengl
fi
make include/private/vkd3d_version.h
make -j"${JOBS:-2}" libvkd3d-shader.la
mkdir -p "$PREFIX/lib" "$PREFIX/licenses"
cp -L .libs/libvkd3d-shader.so.1 "$PREFIX/lib/libvkd3d-shader.so.1"
cp "$CACHE/vkd3d-2.0/COPYING" "$PREFIX/licenses/vkd3d-LGPL-2.1.txt"
cp "$CACHE/SPIRV-Headers-6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1/LICENSE" "$PREFIX/licenses/SPIRV-Headers-LICENSE"
cp "$CACHE/Vulkan-Headers-1.3.228/LICENSE.txt" "$PREFIX/licenses/Vulkan-Headers-LICENSE"
printf 'Native HLSL compiler: %s/lib/libvkd3d-shader.so.1\n' "$PREFIX"
