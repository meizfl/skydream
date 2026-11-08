#!/usr/bin/env python3
"""Create a relocatable Linux game folder without modifying the source install."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def package(source, destination):
    source = source.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists():
        raise ValueError(f'Destination already exists: {destination}')
    if not (source / 'Data/Skyrim.esm').is_file():
        raise ValueError('Source must contain Skyrim 2011 Data/Skyrim.esm')
    if source == destination or source in destination.resolve().parents:
        raise ValueError('Destination must be outside the source game folder')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.skyrim-package-', dir=destination.parent) as tmp:
        work = pathlib.Path(tmp)
        install = work / 'install'
        subprocess.run(['cmake', '--install', str(ROOT / 'desktop/build'), '--prefix', str(install)], check=True)
        output = work / 'game'
        native_lib = output / 'lib/skyrim-native'
        native_lib.mkdir(parents=True)
        binary=install/'bin/lb'
        if b'embedded Skyrim:' not in binary.read_bytes():
            raise ValueError('Package requires a binary with embedded game data')
        shutil.copy2(binary, output / 'Skyrim')
        libraries = list(install.glob('lib*/skyrim-native/libdxvk_d3d9.so'))
        if len(libraries) != 1:
            raise ValueError('Installed DXVK library not found')
        shutil.copy2(libraries[0], native_lib / libraries[0].name)
        shader=ROOT/'build/native-shader/lib/libvkd3d-shader.so.1'
        if not shader.is_file():
            raise ValueError('Build native HLSL compiler first: sh tools/build-native-shader.sh')
        shutil.copy2(shader,native_lib/shader.name)
        (output / 'licenses').mkdir()
        shutil.copy2(ROOT / 'third_party/f3daudio/LICENSE', output / 'licenses/F3DAudio-LICENSE')
        for dep,license_name in [('stb','stb-LICENSE'),('vkd3d','vkd3d-LGPL-2.1.txt')]:
            shutil.copy2(ROOT/'third_party'/dep/'LICENSE',output/'licenses'/license_name)
        sources=output/'sources'
        sources.mkdir()
        for name in ('vkd3d-2.0.tar.xz','spirv-headers.tar.gz','vulkan-headers.tar.gz'):
            shutil.copy2(ROOT/'build/native-shader-source'/name,sources/name)
        shutil.copy2(ROOT/'tools/build-native-shader.sh',sources/'build-native-shader.sh')
        for path in source.glob('*.ini'):
            shutil.copy2(path, output / path.name)
        print('Copying Data (reflink when supported, otherwise a full copy)...', flush=True)
        subprocess.run(['cp', '-aL', '--reflink=auto', '--', str(source / 'Data'), str(output / 'Data')], check=True)
        (output / 'LINUX-README.txt').write_text(
            'Run ./Skyrim, or open the Skyrim executable in your file manager.\n'
            'The whole folder can be moved; game files are resolved beside the executable.\n'
            'Display backend is selected automatically from the desktop session.\n'
            'Overrides: ./Skyrim --wayland or ./Skyrim --x11.\n'
            'F11: fullscreen. Saves: $XDG_DATA_HOME/skyrim-native/save or\n'
            '$HOME/.local/share/skyrim-native/save. Existing saves are not copied.\n'
            'Requires Linux x86-64, Vulkan graphics driver, SDL2, FFmpeg libraries.\n'
            'Game initialization data is linked into the ELF; no external PE images are required.\n'
            'The game code runs as native compiled Linux code without Wine.\n')
        shutil.copy2(ROOT/'gen/image/skyrim_image.json',output/'embedded-game.json')
        if any(path.is_symlink() for path in output.rglob('*')):
            raise ValueError('Package contains symlinks and would not be self-contained')
        output.rename(destination)
    print(f'Ready: {destination / "Skyrim"}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_dir', type=pathlib.Path)
    parser.add_argument('output_dir', type=pathlib.Path)
    args = parser.parse_args()
    try:
        package(args.game_dir, args.output_dir)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Package failed: {error}\n')


if __name__ == '__main__':
    main()
