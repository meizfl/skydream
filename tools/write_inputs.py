#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record build inputs by hash without publishing local install paths."""
import hashlib
import json
import pathlib
import sys

names = ('TESV.exe', 'd3dx9_42.dll', 'binkw32.dll', 'X3DAudio1_7.dll')
if len(sys.argv) != 6:
    raise SystemExit('usage: write_inputs.py OUTPUT EXE D3DX BINK X3DA')
inputs = {}
for name, argument in zip(names, sys.argv[2:]):
    path = pathlib.Path(argument)
    inputs[name] = {'sha1': hashlib.sha1(path.read_bytes()).hexdigest()}
output = pathlib.Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(inputs, indent=2) + '\n')
