import base64
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from ci_encrypt_artifact import encrypt
import prepare_desktop_release as release


class ReleasePreparation(unittest.TestCase):
    def exercise(self, forbidden=False):
        with tempfile.TemporaryDirectory() as tmp:
            old = Path.cwd()
            os.chdir(tmp)
            try:
                key = bytes(range(32))
                env = {'GITHUB_REPOSITORY': 'owner/repo', 'GITHUB_SHA': 'abc',
                       'RELEASE_TAG': '2026-11-09-desktop', 'WINDOWS_RUN': '1',
                       'MACOS_RUN': '2', 'SKYDREAM_ARTIFACT_KEY': base64.b64encode(key).decode()}
                archives = {}
                for i, target in enumerate(('Windows-x86_64', 'macOS-arm64', 'macOS-x86_64'), 1):
                    stage = Path(target); stage.mkdir()
                    (stage / ('Skydream.exe' if i == 1 else 'Skydream')).write_bytes(b'native fixture')
                    if forbidden:
                        (stage / 'Data').mkdir(); (stage / 'Data/Skyrim.esm').write_bytes(b'private fixture')
                    enc = Path(f'Skydream-{target}.tar.gz.enc'); encrypt(stage, enc, key)
                    data = io.BytesIO()
                    with zipfile.ZipFile(data, 'w') as zf: zf.write(enc, enc.name)
                    archives[i] = data.getvalue()
                def api(path, binary=False):
                    if '/artifacts/' in path: return archives[int(path.split('/')[-2])]
                    if path.endswith('/artifacts'):
                        indexes = [1] if '/runs/1/' in path else [2, 3]
                        return {'artifacts': [{'id': i, 'name': 'skydream-encrypted-' + ('Windows' if i == 1 else 'macOS') + '-fixture', 'expired': False} for i in indexes]}
                    return {'conclusion': 'success', 'head_branch': 'main', 'path': '.github/workflows/full-game.yml'}
                with patch.dict(os.environ, env), patch.object(release, 'api', side_effect=api), patch.object(release.subprocess, 'run') as publish:
                    if forbidden:
                        with self.assertRaisesRegex(ValueError, 'private resources'): release.main()
                        publish.assert_not_called()
                    else:
                        release.main()
                        publish.assert_called_once()
                        command = publish.call_args.args[0]
                        self.assertIn('--draft', command)
                        self.assertEqual(len(list(Path('build/release-assets').glob('*.tar.gz'))), 3)
                        self.assertEqual(len(Path('build/release-assets/SHA256SUMS-desktop.txt').read_text().splitlines()), 3)
                        self.assertFalse(list(Path('build/release-assets').glob('*.enc')))
            finally: os.chdir(old)

    def test_authenticated_three_platform_draft(self): self.exercise()
    def test_private_data_rejected_before_publish(self): self.exercise(True)

if __name__ == '__main__': unittest.main()
