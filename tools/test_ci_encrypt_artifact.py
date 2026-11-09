# SPDX-License-Identifier: GPL-3.0-only
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
try:
    from ci_encrypt_artifact import encrypt, decrypt
    from cryptography.exceptions import InvalidTag
except ImportError:
    encrypt = None

@unittest.skipIf(encrypt is None, 'cryptography is required for encrypted artifacts')
class ArtifactEncryptionTest(unittest.TestCase):
    def test_roundtrip_and_random_nonce(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            stage = root / 'stage'
            stage.mkdir()
            (stage / 'binary').write_bytes(b'private test fixture')
            key = os.urandom(32)
            a, b, plain = root/'a.enc', root/'b.enc', root/'out.tar.gz'
            encrypt(stage, a, key)
            encrypt(stage, b, key)
            self.assertNotEqual(a.read_bytes(), b.read_bytes())
            decrypt(a, plain, key)
            with tarfile.open(plain) as tf:
                self.assertEqual(tf.extractfile('Skydream/binary').read(), b'private test fixture')
            with self.assertRaises(FileExistsError):
                decrypt(a, plain, key)

    def test_wrong_key_and_tampering_do_not_write_plaintext(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            stage = root / 'stage'
            stage.mkdir()
            (stage / 'binary').write_bytes(b'private test fixture')
            key = os.urandom(32)
            a, plain = root/'a.enc', root/'out.tar.gz'
            encrypt(stage, a, key)
            with self.assertRaises(InvalidTag):
                decrypt(a, plain, os.urandom(32))
            self.assertFalse(plain.exists())
            data = bytearray(a.read_bytes())
            data[-1] ^= 1
            a.write_bytes(data)
            with self.assertRaises(InvalidTag):
                decrypt(a, plain, key)
            self.assertFalse(plain.exists())

if __name__ == '__main__':
    unittest.main()
