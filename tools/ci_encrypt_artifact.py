#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authenticated encryption for private CI build archives (AES-256-GCM)."""
import argparse
import base64
import io
import os
from pathlib import Path
import tarfile
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

MAGIC = b'SKYDREAM-ARCHIVE-1\n'

def key_bytes(key_file=None):
    value = Path(key_file).read_text().strip() if key_file else os.environ.get('SKYDREAM_ARTIFACT_KEY', '')
    key = base64.b64decode(value, validate=True)
    if len(key) != 32:
        raise ValueError('artifact key must be base64-encoded 32 random bytes')
    return key

def encrypt(directory, output, key):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w:gz') as tf:
        tf.add(directory, arcname='Skydream')
    nonce = os.urandom(12)
    payload = MAGIC + nonce + AESGCM(key).encrypt(nonce, buf.getvalue(), MAGIC)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)

def decrypt(source, output, key):
    data = source.read_bytes()
    if not data.startswith(MAGIC):
        raise ValueError('unsupported archive format')
    nonce = data[len(MAGIC):len(MAGIC)+12]
    # Authenticate before creating any plaintext output.
    plaintext = AESGCM(key).decrypt(nonce, data[len(MAGIC)+12:], MAGIC)
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as f:
        f.write(plaintext)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['encrypt', 'decrypt'])
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--key-file', type=Path)
    a = p.parse_args()
    try:
        k = key_bytes(a.key_file)
        (encrypt if a.mode == 'encrypt' else decrypt)(a.source, a.output, k)
    except Exception:
        p.exit(1, 'Archive operation failed: verify key, input and output path.\n')
