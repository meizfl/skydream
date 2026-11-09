#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare or fetch private generated game inputs; never log download URLs."""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import tarfile
import tempfile
import urllib.request

REQUIRED = {'gen/exe_table.c', 'gen/funcs_exe.h', 'gen/image/skyrim_image.c', 'gen/image/skyrim_image.bin'}
LIMIT = 1024 * 1024 * 1024

def valid(name):
    p = PurePosixPath(name)
    return (not p.is_absolute() and '..' not in p.parts and len(p.parts) >= 2
            and p.parts[0] == 'gen' and p.suffix in {'.c', '.h', '.bin'})

def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

def pack(root, output):
    paths = sorted(p for p in (root / 'gen').rglob('*') if p.is_file() and valid(p.relative_to(root).as_posix()))
    names = {p.relative_to(root).as_posix() for p in paths}
    if not REQUIRED <= names or any(p.is_symlink() for p in paths):
        raise ValueError('missing generated game inputs or symbolic links present')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, 'w:gz') as tf:
        for p in paths:
            tf.add(p, arcname=p.relative_to(root).as_posix(), recursive=False)
    sha = digest(output)
    output.with_suffix(output.suffix + '.sha256').write_text(sha + '\n')
    print(f'Private archive: {output}\nSHA256: {sha}')

def fetch(root):
    url = os.environ.get('SKYDREAM_GAME_INPUT_URL', '')
    sha = os.environ.get('SKYDREAM_GAME_INPUT_SHA256', '')
    if not url.startswith('https://') or not re.fullmatch(r'[a-fA-F0-9]{64}', sha):
        raise ValueError('configure HTTPS input URL and SHA256 repository secrets')
    if (root / 'gen').exists():
        raise ValueError('refusing to overwrite existing gen directory')
    with tempfile.TemporaryDirectory() as td:
        archive = Path(td) / 'input.tar.gz'
        # Suppress transport exceptions, which can include the secret URL.
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'Skydream-CI/1.0'})
            with urllib.request.urlopen(request, timeout=120) as r, archive.open('wb') as out:
                if not r.geturl().startswith('https://'):
                    raise ValueError('HTTPS required')
                size = 0
                for b in iter(lambda: r.read(1024 * 1024), b''):
                    size += len(b)
                    if size > LIMIT:
                        raise ValueError('download too large')
                    out.write(b)
        except Exception:
            raise ValueError('private input download failed (URL withheld)') from None
        if digest(archive) != sha.lower():
            raise ValueError('private input SHA256 mismatch')
        with tarfile.open(archive, 'r:gz') as tf:
            members = tf.getmembers()
            names = [m.name for m in members]
            if (not REQUIRED <= set(names) or len(set(names)) != len(names)
                    or sum(m.size for m in members) > LIMIT
                    or any(not m.isfile() or not valid(m.name) for m in members)):
                raise ValueError('invalid private input archive')
            for m in members:
                dest = root / m.name
                dest.parent.mkdir(parents=True, exist_ok=True)
                with tf.extractfile(m) as src, dest.open('wb') as out:
                    import shutil
                    shutil.copyfileobj(src, out)
    print('Private generated inputs verified and extracted; contents not logged.')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['pack', 'fetch'])
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--output', type=Path, default=Path('build/private-ci/skydream-game-input.tar.gz'))
    a = p.parse_args()
    try:
        pack(a.root, a.output) if a.mode == 'pack' else fetch(a.root)
    except Exception as e:
        p.exit(1, f'{e}\n')
