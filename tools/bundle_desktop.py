#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect native runtime dependencies; relocate Mach-O load commands."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def mac_deps(path):
    ids = {line.strip() for line in run('otool', '-D', path).splitlines()[1:]}
    return [dep for line in run('otool', '-L', path).splitlines()[1:] if (dep := line.strip().split(' (compatibility')[0]) not in ids]


def win_deps(path):
    return re.findall(r'DLL Name:\s*(\S+)', run('objdump', '-p', path), re.I)


def bundle(stage):
    stage = stage.resolve()
    mac = sys.platform == 'darwin'
    libdir = stage / 'lib/skyrim-native' if mac else stage
    libdir.mkdir(parents=True, exist_ok=True)
    exe = stage / ('Skydream' if mac else 'Skydream.exe')
    if not exe.exists():
        raise ValueError('root Skydream executable is required')
    copied = {}
    origins = {}
    queue = [exe] + list(libdir.glob('*.dylib' if mac else '*.dll'))
    for p in queue:
        copied[p.name.lower()] = p
        origins[p] = p
    if mac:
        prefix = Path(run('brew', '--prefix'))
        search = [libdir, prefix/'lib']
        extras = [Path(run('brew', '--prefix', pkg))/'lib'/name for pkg, name in [('molten-vk','libMoltenVK.dylib'), ('vulkan-loader','libvulkan.1.dylib')]]
        # SDL2-compat loads SDL3 with dlopen, so otool cannot discover it.
        sdl3 = Path(run('brew', '--prefix', 'sdl3'))/'lib/libSDL3.dylib'
        if sdl3.exists():
            extras.append(sdl3)
    else:
        # Called from UCRT64: avoid mixing MSYS runtime DLLs into native output.
        search = [Path(run('cygpath', '-w', '/ucrt64/bin')), stage]
        prefix = search[0].parent
        extras = [search[0]/'vulkan-1.dll']
        if (search[0]/'SDL3.dll').exists():
            extras.append(search[0]/'SDL3.dll')
    def add(src):
        src = src.resolve()
        name = src.name.lower()
        if name in copied:
            return copied[name]
        dest = libdir/src.name
        shutil.copy2(src, dest)
        dest.chmod(0o755)
        copied[name] = dest
        origins[dest] = src
        queue.append(dest)
        return dest
    for extra in extras:
        if not extra.exists():
            raise ValueError(f'missing graphics loader: {extra.name}')
        # Preserve stable requested dylib basename even if Homebrew symlink resolves.
        dest = libdir/extra.name
        shutil.copy2(extra.resolve(), dest)
        dest.chmod(0o755)
        copied[dest.name.lower()] = dest
        origins[dest] = extra.resolve()
        queue.append(dest)
    edges = {}
    for file in queue:
        src = origins[file]
        deps = mac_deps(src) if mac else win_deps(src)
        for dep in deps:
            if mac:
                if dep.startswith(('/usr/lib/', '/System/Library/')):
                    continue
                if dep.startswith('@rpath/') and Path(dep).name == src.name:
                    continue  # dylib install ID, not a dependency
                name = Path(dep).name
                if name.lower() in copied:
                    target = copied[name.lower()]
                else:
                    if dep.startswith('@loader_path/'):
                        candidates = [src.parent/dep[len('@loader_path/'):]]
                    elif dep.startswith('@executable_path/'):
                        candidates = [stage/dep[len('@executable_path/'):]]
                    elif dep.startswith('@rpath/'):
                        candidates = [d/name for d in search] + [p/name for p in (prefix/'opt').glob('*/lib')]
                    else:
                        candidates = [Path(dep)]
                    found = next((p for p in candidates if p.is_file()), None)
                    if not found:
                        raise ValueError(f'unresolved Mach-O dependency {dep} of {file.name}')
                    target = add(found)
                edges.setdefault(file, []).append((dep, target))
            else:
                name = dep.lower()
                if name.startswith(('api-ms-win-', 'ext-ms-win-')):
                    continue
                if name in copied:
                    continue
                found = next((p for directory in search for p in directory.iterdir() if p.name.lower() == name), None)
                if found:
                    add(found)
                else:
                    system = Path(os.environ['SystemRoot'])/'System32'/dep
                    if not system.exists():
                        raise ValueError(f'unresolved Windows dependency {dep} of {file.name}')
    if mac:
        for file in queue:
            if file != exe:
                subprocess.run(['install_name_tool', '-id', '@rpath/'+file.name, str(file)], check=True)
            for old, target in edges.get(file, []):
                new = '@loader_path/'+os.path.relpath(target, file.parent)
                if old != new:
                    subprocess.run(['install_name_tool', '-change', old, new, str(file)], check=True)
        manifest = stage/'share/vulkan/icd.d/MoltenVK_icd.json'
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({'file_format_version':'1.0.0','ICD':{'library_path':'../../../lib/skyrim-native/libMoltenVK.dylib','api_version':'1.2.0'}}))
        for file in [p for p in queue if p != exe] + [exe]:
            subprocess.run(['codesign', '--force', '--sign', '-', str(file)], check=True)
        for file in queue:
            for dep in mac_deps(file):
                if dep.startswith(('/usr/lib/', '/System/Library/', '@rpath/')):
                    continue
                if not dep.startswith('@loader_path/') or not (file.parent/dep[len('@loader_path/'):]).exists():
                    raise ValueError(f'nonportable dependency in {file.name}: {dep}')
    # Keep dependency identities and license material with the binaries.
    receipt = stage/'share/dependencies'
    receipt.mkdir(parents=True, exist_ok=True)
    if mac:
        (receipt/'homebrew.json').write_text(run('brew','info','--json=v2','--installed'))
        for formula in (prefix/'Cellar').glob('*/*'):
            for license in list(formula.glob('LICENSE*')) + list(formula.glob('COPYING*')):
                if license.is_file():
                    dest = receipt/'licenses'/formula.parent.name/license.name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(license,dest)
    else:
        (receipt/'msys2-packages.txt').write_text(run('pacman','-Q'))
        licenses = prefix/'share/licenses'
        if licenses.exists():
            shutil.copytree(licenses,receipt/'licenses',dirs_exist_ok=True)
    native = Path('build/native-shader')
    for sub in ('licenses','sources'):
        if (native/sub).exists():
            shutil.copytree(native/sub,stage/'share'/sub,dirs_exist_ok=True)
    (receipt/'binaries.json').write_text(json.dumps({str(p.relative_to(stage)):hashlib.sha256(p.read_bytes()).hexdigest() for p in queue},indent=2))
    print(f'Bundled {len(queue)} native executables/libraries; dependency closure verified.')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage',type=Path)
    bundle(parser.parse_args().stage)
