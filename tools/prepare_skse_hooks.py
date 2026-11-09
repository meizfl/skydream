#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Upgrade existing generated code for native host hooks (SKSE and engine primitives), idempotently.
Fresh lift.py output already uses the same host-override convention.
"""
from pathlib import Path
import sys

def prepare(folder):
    targets = {0xA60200, 0x842140, 0x873D70, 0xF537F0, 0xF52E30, 0xA4A4E0, 0xA4A530, 0xA4A570, 0xF5B670, 0xF520F0, 0xF55A90, 0xF59140}
    found = set()
    changes = []
    for p in sorted(Path(folder).glob('exe_*.c')):
        text = p.read_text()
        new = text
        for address in targets:
            old = f'void f_{address:X}(CPU *c) {{'
            renamed = f'void f_{address:X}_guest(CPU *c) {{'
            if old in new:
                if new.count(old) != 1:
                    raise ValueError(f'duplicate definition: {p}: {old}')
                new = new.replace(old, renamed)
                found.add(address)
            elif renamed in new:
                found.add(address)
        if new != text:
            changes.append((p, new))
    if found != targets:
        raise ValueError('Missing generated functions: ' + ', '.join(f'{x:X}' for x in targets-found))
    for p, new in changes:
        p.write_text(new)
    return len(changes)

if __name__ == '__main__':
    print('Native host hooks: updated', prepare(sys.argv[1]), 'generated units')
