#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package a locally compiled native executable, without original game images."""
import argparse
import hashlib
import io
import json
import pathlib
import platform
import subprocess
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def elf64(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or int.from_bytes(data[18:20], 'little') != 62:
        raise ValueError(f'Expected an x86-64 Linux ELF: {path}')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=pathlib.Path,
                        help='Installed executable with relocatable RPATH')
    parser.add_argument('--dxvk', required=True, type=pathlib.Path)
    parser.add_argument('--inputs-manifest', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    binary, dxvk = elf64(args.binary), elf64(args.dxvk)
    dynamic = subprocess.check_output(['readelf', '-d', str(args.binary)], text=True)
    if '$ORIGIN/lib/skyrim-native' not in dynamic:
        raise ValueError('Use the cmake-installed executable: portable RPATH is missing')
    inputs = json.loads(args.inputs_manifest.read_text())
    required = ('TESV.exe',)
    hashes = {name: {'sha1': inputs[name]['sha1']} for name in required}
    for record in hashes.values():
        if len(record['sha1']) != 40 or any(c not in '0123456789abcdef' for c in record['sha1']):
            raise ValueError('Invalid build-input hash')
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    libc = ' '.join(platform.libc_ver())
    files = {
        'Skydream': binary,
        'lib/skyrim-native/libdxvk_d3d9.so': dxvk,
        'LICENSE': (ROOT / 'LICENSE').read_bytes(),
        'NOTICE.md': (ROOT / 'NOTICE.md').read_bytes(),
        'DXVK-LICENSE': (ROOT / 'third_party/dxvk/LICENSE').read_bytes(),
        'F3DAudio-LICENSE': (ROOT / 'third_party/f3daudio/LICENSE').read_bytes(),
        'build-inputs.json': (json.dumps(hashes, indent=2) + '\n').encode(),
        'BUILD-INFO.json': (json.dumps({'source_revision': revision, 'platform': 'linux-x86_64',
                                      'build_host_libc': libc,
                                      'skse_compatibility': ('experimental-papyrus-subset-0.1.0' if b'experimental Papyrus subset enabled (API 0.1.0)' in binary else 'disabled'),
                                      'skyui_interface_enabled': b'SkyUI: experimental interface enabled by default' in binary,
                                      'contains_statically_recompiled_game_code': True,
                                      'contains_original_pe_images_or_assets': False}, indent=2) + '\n').encode(),
        'README.txt': b'''Skydream native Linux binary (experimental, locally built)\n
Extract the archive and copy the contents of its Skydream folder into your
own matching Skyrim 2011 installation. Keep the Skydream executable beside
Data and the original TESV/DLL inputs. Run ./Skydream.
Alternatively copy Skydream and lib into an existing private Linux game pack.
F11 toggles fullscreen; Wayland/X11 are selected automatically.
Overrides: ./Skydream --wayland or ./Skydream --x11.

This archive contains statically recompiled game code. It does not contain
original PE images, game assets, saves, or the complete playable game.
Required image: TESV.exe (and unpacked variant if applicable).
D3DX uses native image/math code and vkd3d-shader; Bink uses native FFmpeg and X3DAudio uses native F3DAudio; their PE images are not needed. See build-inputs.json for exact build hashes.
Keep originals beside the binary, or *.pe images in lib/skyrim-native/guest.

Optional SKSE compatibility is recorded in BUILD-INFO.json and --help.
It is incomplete: Windows DLL plugins and SKSE co-saves are unsupported. This is not a full SKSE distribution.

This build uses the host's SDL2, Vulkan loader, FFmpeg and C/C++ runtime.
See NEEDED.txt for exact linked library SONAMEs. It is not a universal binary
for all distributions. In particular, FFmpeg ABI versions must match.
Default saves: $XDG_DATA_HOME/skyrim-native/save or
$HOME/.local/share/skyrim-native/save.

LICENSE covers original Linux contributions; NOTICE.md explains inherited
code and third-party license boundaries. No rights in game code are granted
by the project's GPL notice.
''',
    }
    shader = ROOT / 'build/native-shader/lib/libvkd3d-shader.so.1'
    files['lib/skyrim-native/libvkd3d-shader.so.1'] = elf64(shader)
    for dep in ('stb','vkd3d'):
        files[dep+'-LICENSE'] = (ROOT/'third_party'/dep/'LICENSE').read_bytes()
    for name in ('vkd3d-2.0.tar.xz','spirv-headers.tar.gz','vulkan-headers.tar.gz'):
        files['sources/'+name] = (ROOT/'build/native-shader-source'/name).read_bytes()
    files['sources/build-native-shader.sh'] = (ROOT/'tools/build-native-shader.sh').read_bytes()
    needed = []
    for label, path in [('Skydream', args.binary), ('libdxvk_d3d9.so', args.dxvk)]:
        output = subprocess.check_output(['readelf', '-d', str(path)], text=True)
        needed.append(label + ':\n' + '\n'.join(line.strip() for line in output.splitlines() if '(NEEDED)' in line))
    files['NEEDED.txt'] = ('\n\n'.join(needed) + '\n').encode()
    sums = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n' for name, data in sorted(files.items()))
    files['SHA256SUMS'] = sums.encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        raise ValueError(f'Refusing to overwrite {args.output}')
    with tempfile.NamedTemporaryFile(dir=args.output.parent, delete=False) as tmp:
        temporary = pathlib.Path(tmp.name)
    try:
        with tarfile.open(temporary, 'w:gz', compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                entry = tarfile.TarInfo('Skydream/' + name)
                entry.mode = 0o755 if name == 'Skydream' else 0o644
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
        temporary.rename(args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(args.output)


if __name__ == '__main__':
    main()
