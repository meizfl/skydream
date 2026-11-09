#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish authenticated CI output as plaintext assets in a draft release."""
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import zipfile
from ci_encrypt_artifact import decrypt, key_bytes


def api(path, binary=False):
    data = subprocess.check_output(['gh', 'api', path])
    return data if binary else json.loads(data)


def main():
    repo = os.environ['GITHUB_REPOSITORY']
    tag = os.environ['RELEASE_TAG']
    if not re.fullmatch(r'\d{4}-\d{2}-\d{2}[A-Za-z0-9._-]*', tag):
        raise ValueError('a date-based release tag is required')
    out = Path('build/release-assets')
    out.mkdir(parents=True, exist_ok=False)
    key = key_bytes()
    outputs = []
    for variable, prefix, count in [('WINDOWS_RUN', 'Windows', 1), ('MACOS_RUN', 'macOS', 2)]:
        run = os.environ[variable]
        if not run.isdecimal():
            raise ValueError('invalid run ID')
        meta = api(f'repos/{repo}/actions/runs/{run}')
        if meta['conclusion'] != 'success' or meta['head_branch'] != 'main' or meta['path'] != '.github/workflows/full-game.yml':
            raise ValueError('only successful main full-game runs are accepted')
        artifacts = api(f'repos/{repo}/actions/runs/{run}/artifacts')['artifacts']
        selected = [a for a in artifacts if a['name'].startswith(f'skydream-encrypted-{prefix}-') and not a['expired']]
        if len(selected) != count:
            raise ValueError('unexpected artifact count')
        for artifact in selected:
            archive = api(f'repos/{repo}/actions/artifacts/{artifact["id"]}/zip', binary=True)
            with zipfile.ZipFile(io.BytesIO(archive)) as zf:
                names = zf.namelist()
                if len(names) != 1 or not re.fullmatch(r'Skydream-(Windows-x86_64|macOS-arm64|macOS-x86_64)\.tar\.gz\.enc', names[0]):
                    raise ValueError('unexpected artifact contents')
                enc = out / names[0]
                enc.write_bytes(zf.read(names[0]))
            plain = out / enc.name.removesuffix('.enc')
            decrypt(enc, plain, key)
            with tarfile.open(plain, 'r:gz') as tf:
                members = tf.getmembers()
                for member in members:
                    path = PurePosixPath(member.name)
                    if path.is_absolute() or '..' in path.parts or path.parts[0] != 'Skydream' or not (member.isdir() or member.isfile()):
                        raise ValueError('unsafe package member')
                    if any(p.lower() in ('data', 'artifact.key', '.git') for p in path.parts):
                        raise ValueError('unexpected private resources in package')
                executable = 'Skydream/Skydream.exe' if prefix == 'Windows' else 'Skydream/Skydream'
                if executable not in {m.name for m in members}:
                    raise ValueError('missing root game executable')
            enc.unlink()
            outputs.append(plain)
            print(f'Authenticated and checked {plain.name}', flush=True)
    checksums = out / 'SHA256SUMS-desktop.txt'
    checksums.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in outputs))
    notes = out / 'RELEASE-NOTES.md'
    notes.write_text('''Ready native desktop packages. Add your own Skyrim Legendary Edition 1.9.32 Data directory beside Skydream / Skydream.exe.

Windows x86-64, macOS Intel and macOS ARM include SDL, FFmpeg, native vkd3d-shader, Vulkan Loader and their non-system dependencies. Mac includes MoltenVK and requires macOS 15+. Homebrew/MSYS2 are not required to run these packages. Windows requires a compatible Vulkan graphics driver.

CI passed full compilation, embedded game-image execution, native shader compilation and relocated dependency checks. This does not validate full gameplay or physical GPU rendering on Windows/macOS.

These release archives are decrypted; no key is required. CI artifacts remain encrypted. No Data resources are included. See README for Linux, Windows and Mac build commands.
''')
    subprocess.run(['gh', 'release', 'create', tag, '--repo', repo, '--target', os.environ['GITHUB_SHA'], '--draft', '--title', tag, '--notes-file', str(notes), *map(str, outputs), str(checksums)], check=True)


if __name__ == '__main__':
    main()
