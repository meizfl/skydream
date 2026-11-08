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
MODULES = ('TESV.exe', 'd3dx9_42.dll')


def find_file(source, name):
    directories = (source, source / 'redist', source / 'Redist', source / 'REDIST',
                   source / 'lib/skyrim-native/guest')
    names = [name]
    if name == 'TESV.exe':
        names = ['TESV.exe.unpacked.exe', 'TESV_unpacked.exe', name]
    for directory in directories:
        if not directory.is_dir():
            continue
        entries = {p.name.lower(): p for p in directory.iterdir()}
        for candidate in names:
            for suffix in ('', '.pe'):
                path = entries.get((candidate + suffix).lower())
                if path and path.is_file():
                    return path
    raise ValueError(f'Missing required image: {name}')


def package(source, destination):
    source = source.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists():
        raise ValueError(f'Destination already exists: {destination}')
    if not (source / 'Data/Skyrim.esm').is_file():
        raise ValueError('Source must contain Skyrim 2011 Data/Skyrim.esm')
    if source == destination or source in destination.resolve().parents:
        raise ValueError('Destination must be outside the source game folder')
    manifest = json.loads((ROOT / 'analysis/linux-inputs.json').read_text())
    images = {}
    for name in MODULES:
        path = find_file(source, name)
        digest = hashlib.sha1(path.read_bytes()).hexdigest()
        if digest != manifest[name]['sha1']:
            raise ValueError(f'{path}: image differs from the version used to build this port')
        images[name] = path
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.skyrim-package-', dir=destination.parent) as tmp:
        work = pathlib.Path(tmp)
        install = work / 'install'
        subprocess.run(['cmake', '--install', str(ROOT / 'desktop/build'), '--prefix', str(install)], check=True)
        output = work / 'game'
        guest = output / 'lib/skyrim-native/guest'
        guest.mkdir(parents=True)
        shutil.copy2(install / 'bin/lb', output / 'Skyrim')
        libraries = list(install.glob('lib*/skyrim-native/libdxvk_d3d9.so'))
        if len(libraries) != 1:
            raise ValueError('Installed DXVK library not found')
        shutil.copy2(libraries[0], guest.parent / libraries[0].name)
        (output / 'licenses').mkdir()
        shutil.copy2(ROOT / 'third_party/f3daudio/LICENSE', output / 'licenses/F3DAudio-LICENSE')
        for name, path in images.items():
            target = guest / (name + '.pe')
            shutil.copy2(path, target)
            target.chmod(0o644)
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
            'lib/skyrim-native/guest contains required original memory images; do not remove.\n'
            'The game code runs as native compiled Linux code without Wine.\n')
        (output / 'linux-images.json').write_text(json.dumps(
            {name: {'sha1': manifest[name]['sha1']} for name in MODULES}, indent=2) + '\n')
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
