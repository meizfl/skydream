# SPDX-License-Identifier: GPL-3.0-only
"""Discovery regression test using a synthetic PE, without any game assets.

Run with the recompiler environment: python -m unittest discover -s tools -p 'test_*.py'
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent


def make_pe(path):
    """Two code roots, a switch, a callback, a noreturn import and code gaps."""
    data = bytearray(0x800)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x14c, 2, 0, 0, 0, 224, 0x102)
    opt = 0x98
    struct.pack_into('<H', data, opt, 0x10b)
    struct.pack_into('<III', data, opt + 16, 0x1000, 0x1000, 0x2000)
    struct.pack_into('<III', data, opt + 28, 0x400000, 0x1000, 0x200)
    struct.pack_into('<II', data, opt + 56, 0x3000, 0x200)
    struct.pack_into('<H', data, opt + 68, 3)
    struct.pack_into('<I', data, opt + 92, 16)
    struct.pack_into('<II', data, opt + 104, 0x2000, 40)
    for i, (name, size, rva, offset, flags) in enumerate([
        (b'.text', 0x200, 0x1000, 0x200, 0x60000020),
        (b'.rdata', 0x400, 0x2000, 0x400, 0x40000040),
    ]):
        struct.pack_into('<8sIIIIIIHHI', data, opt + 224 + i * 40,
                         name, size, rva, size, offset, 0, 0, 0, 0, flags)
    data[0x200:0x400] = b'\xcc' * 0x200
    struct.pack_into('<IIIII', data, 0x400, 0x2040, 0, 0, 0x2060, 0x2050)
    struct.pack_into('<II', data, 0x440, 0x2080, 0)
    struct.pack_into('<II', data, 0x450, 0x2080, 0)
    data[0x460:0x46d] = b'KERNEL32.dll\0'
    data[0x480:0x48e] = b'\0\0ExitProcess\0'

    def emit(rva, code):
        data[0x200 + rva:0x200 + rva + len(code)] = code

    def call(at, target):
        return b'\xe8' + struct.pack('<i', target - at - 5)

    emit(0, call(0, 0x20) + call(5, 0x60) + b'\x6a\x00\xff\x15\x50\x20\x40\x00')
    # cmp eax, 1; ja default; jmp [eax*4 + switch_table]
    emit(0x20, b'\x83\xf8\x01\x77\x1b\xff\x24\x85\x00\x21\x40\x00')
    emit(0x40, b'\xb8\x01\x00\x00\x00\xc3')
    emit(0x50, b'\xb8\x02\x00\x00\x00\xc3')
    emit(0x60, b'\xb8\x80\x10\x40\x00' + call(0x65, 0xa0) + b'\xc3')
    emit(0x80, b'\xff\xe0')
    emit(0xa0, b'\x31\xc0\xc3')
    emit(0xc0, b'\x40\xc3')
    struct.pack_into('<II', data, 0x500, 0x401040, 0x401050)
    struct.pack_into('<II', data, 0x520, 0x401020, 0x401060)
    path.write_bytes(data)


class ParallelDiscoveryTest(unittest.TestCase):
    def test_parallel_matches_serial(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            exe = tmp / 'fixture.exe'
            make_pe(exe)
            results = []
            for index, jobs in enumerate((1, 4, 4)):
                out = tmp / ('functions%d.json' % index)
                subprocess.run([sys.executable, str(ROOT / 'discover.py'), str(exe), str(out)],
                               env=dict(os.environ, DISCOVER_JOBS=str(jobs)),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=60)
                results.append((json.loads(out.read_text()), json.loads(Path(str(out) + '.gaps').read_text())))
            self.assertEqual(results[0], results[1])
            self.assertEqual(results[1], results[2])
            graph = results[0][0]
            self.assertEqual(graph['entry'], 0x401000)
            for addr in (0x401000, 0x401020, 0x401060, 0x401080, 0x4010a0, 0x4010c0):
                self.assertIn(hex(addr), graph['functions'])
            self.assertEqual(graph['jump_tables']['0x401025'], ['0x401040', '0x401050'])
            self.assertIn('0x401080', graph['indirect_jumps'])
            self.assertEqual(graph['iat']['0x402050'], 'KERNEL32.dll!ExitProcess')


if __name__ == '__main__':
    unittest.main()
