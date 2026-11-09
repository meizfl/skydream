# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import ci_game_input as ci

class Response(io.BytesIO):
    def geturl(self):
        return 'https://private.invalid/input'

class PrivateInputTest(unittest.TestCase):
    def archive(self, extra=None):
        out = io.BytesIO()
        with tarfile.open(fileobj=out, mode='w:gz') as tf:
            for name in sorted(ci.REQUIRED):
                member = tarfile.TarInfo(name)
                member.size = 1
                tf.addfile(member, io.BytesIO(b'x'))
            if extra:
                tf.addfile(extra, io.BytesIO(b'x' * extra.size))
        return out.getvalue()

    def fetch(self, data, root, sha=None):
        env = {'SKYDREAM_GAME_INPUT_URL': 'https://private.invalid/token',
               'SKYDREAM_GAME_INPUT_SHA256': sha or hashlib.sha256(data).hexdigest()}
        with patch.dict(os.environ, env), patch.object(ci.urllib.request, 'urlopen', return_value=Response(data)):
            ci.fetch(root)

    def test_regular_input(self):
        with tempfile.TemporaryDirectory() as td:
            self.fetch(self.archive(), Path(td))
            self.assertEqual((Path(td) / 'gen/exe_table.c').read_bytes(), b'x')

    def test_bad_digest(self):
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
                self.fetch(self.archive(), Path(td), '0' * 64)
            self.assertFalse((Path(td) / 'gen').exists())

    def test_traversal_and_links_and_duplicates(self):
        for name, kind in [('../escape.c', tarfile.REGTYPE), ('gen/link.c', tarfile.SYMTYPE), ('gen/exe_table.c', tarfile.REGTYPE)]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = '/etc/passwd'
                with self.assertRaisesRegex(ValueError, 'invalid private input'):
                    self.fetch(self.archive(member), Path(td))
                self.assertFalse((Path(td) / 'gen').exists())

    def test_url_not_leaked(self):
        with tempfile.TemporaryDirectory() as td, patch.dict(os.environ, {'SKYDREAM_GAME_INPUT_URL': 'https://private.invalid/token', 'SKYDREAM_GAME_INPUT_SHA256': '0'*64}), patch.object(ci.urllib.request, 'urlopen', side_effect=RuntimeError('https://private.invalid/token')):
            with self.assertRaises(ValueError) as err:
                ci.fetch(Path(td))
            self.assertNotIn('private.invalid', str(err.exception))

if __name__ == '__main__':
    unittest.main()
