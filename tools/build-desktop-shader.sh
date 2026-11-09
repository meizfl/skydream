#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Native shader compiler only: no Wine/Direct3D runtime in the output.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CACHE="$ROOT/build/desktop-shader-source"
PREFIX="$ROOT/build/native-shader"
mkdir -p "$CACHE" "$PREFIX/lib" "$PREFIX/bin" "$PREFIX/licenses" "$PREFIX/sources"
fetch() {
    url=$1; dest=$2; digest=$3
    [ -f "$dest" ] || curl -fL --retry 3 "$url" -o "$dest"
    python -c 'import hashlib,sys; p=sys.argv[1]; assert hashlib.sha256(open(p,"rb").read()).hexdigest()==sys.argv[2], "source SHA256 mismatch"' "$dest" "$digest"
}
fetch https://dl.winehq.org/vkd3d/source/vkd3d-2.0.tar.xz "$CACHE/vkd3d-2.0.tar.xz" 9ad29bb236808186a47ec66e853b21e6fca59b9dc62a9474b05d5e3eda2710ef
fetch https://codeload.github.com/KhronosGroup/SPIRV-Headers/tar.gz/6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1 "$CACHE/spirv.tar.gz" d035de73b4085301bf921dc66997ce993fbf7c0d78f3308e67b6be2513a8b88c
fetch https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.3.228 "$CACHE/vulkan.tar.gz" e5a4dcdfb9361ee8d1b9401ed337b65e7c34a54224b99a9cde303a650a9cb350
fetch https://cpan.metacpan.org/authors/id/I/IS/ISHIGAKI/JSON-4.10.tar.gz "$CACHE/json.tar.gz" df8b5143d9a7de99c47b55f1a170bd1f69f711935c186a6dc0ab56dd05758e35
[ -d "$CACHE/vkd3d-2.0" ] || tar -xJf "$CACHE/vkd3d-2.0.tar.xz" -C "$CACHE"
[ -d "$CACHE/SPIRV-Headers-6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1" ] || tar -xzf "$CACHE/spirv.tar.gz" -C "$CACHE"
[ -d "$CACHE/Vulkan-Headers-1.3.228" ] || tar -xzf "$CACHE/vulkan.tar.gz" -C "$CACHE"
[ -d "$CACHE/JSON-4.10" ] || tar -xzf "$CACHE/json.tar.gz" -C "$CACHE"
export PERL5LIB="$CACHE/JSON-4.10/lib${PERL5LIB:+:$PERL5LIB}"
export CPPFLAGS="${CPPFLAGS:-} -I$CACHE/Vulkan-Headers-1.3.228/include -I$CACHE/SPIRV-Headers-6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1/include"
case $(uname -s) in
    MINGW*|MSYS*) host_arg=--host=x86_64-w64-mingw32; export CC=gcc; export SONAME_LIBVULKAN=vulkan-1.dll; output=libvkd3d-shader-1.dll; dest=bin ;;
    Darwin) host_arg=; export CC=clang; export SONAME_LIBVULKAN=libvulkan.1.dylib; output=libvkd3d-shader.1.dylib; dest=lib ;;
    *) host_arg=; export SONAME_LIBVULKAN=libvulkan.so.1; output=libvkd3d-shader.so.1; dest=lib ;;
esac
mkdir -p "$CACHE/build"
cd "$CACHE/build"
# Release archives contain the generated IDL headers. WIDL is not a runtime dependency.
WIDL=no "$CACHE/vkd3d-2.0/configure" $host_arg --prefix="$PREFIX" --disable-doxygen-doc --disable-demos --disable-tests --without-xcb --without-ncurses --without-opengl
make include/private/vkd3d_version.h
make -j"${JOBS:-3}" libvkd3d-shader.la
cp -L ".libs/$output" "$PREFIX/$dest/$output"
cp "$CACHE/vkd3d-2.0/COPYING" "$PREFIX/licenses/vkd3d-LGPL-2.1.txt"
cp "$CACHE/vkd3d-2.0.tar.xz" "$PREFIX/sources/"
