#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inventory a user-provided classic SKSE source tree; never copies its code.

Usage: python3 tools/audit_skse.py /path/to/SKSE --output /tmp/skse-audit.json
The report records interface names, counts, hook locations and input hashes.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


def audit(directory):
    code = directory / 'src/skse/skse'
    if not (code / 'Hooks_Papyrus.cpp').is_file():
        raise ValueError('Expected classic SKSE distribution with src/skse/skse')
    functions, hooks = [], []
    for path in sorted(code.glob('*.cpp')):
        text = path.read_text(errors='replace')
        for match in re.finditer(r'new\s+(?:Latent)?NativeFunction\d+\s*<[^;]+?>\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"', text):
            name, cls = match.groups()
            functions.append({'class': cls, 'name': name, 'source': path.name})
        if path.name.startswith('Hooks_'):
            for match in re.finditer(r'\b(SafeWrite(?:8|16|32|Buf)|WriteRel(?:Call|Jump|Jnz|Jle|Je))\s*\(\s*([^,;\n]+)', text):
                hooks.append({'kind': match[1], 'target': match[2].strip(), 'source': path.name})
    binaries = {}
    for path in sorted(directory.glob('skse_*')):
        if path.suffix.lower() in ('.dll', '.exe'):
            binaries[path.name] = {'size': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    return {'format': 1, 'target': 'classic Skyrim 1.9.32, SKSE 1.7.3',
            'note': 'Static inventory, not proof that a hook or API is implemented. Source code is not redistributed.',
            'native_registration_count': len(functions),
            'classes': dict(sorted(Counter(f['class'] for f in functions).items())),
            'native_registrations': functions, 'patch_site_count': len(hooks),
            'patch_sites': hooks, 'binaries': binaries,
            'licensing': {'readme': 'skse_readme.txt: modified redistribution requires author contact; do not put original SKSE under GPL.',
                          'skse_notice': 'src/skse/skse_license.txt', 'common_notice': 'src/common/common_license.txt'}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = audit(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"{report['native_registration_count']} literal native registrations, {len(report['classes'])} classes, {report['patch_site_count']} patch sites")


if __name__ == '__main__':
    main()
