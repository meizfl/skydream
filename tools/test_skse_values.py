# SPDX-License-Identifier: GPL-3.0-only
"""Exercise real primitive compatibility functions with ABI-sized values."""
import ctypes
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


class Native(ctypes.Structure):
    _fields_ = [('cls', ctypes.c_char_p), ('name', ctypes.c_char_p),
                ('result', ctypes.c_uint8), ('argc', ctypes.c_uint8),
                ('args', ctypes.c_uint8 * 4), ('operation', ctypes.c_uint16)]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class SkseValuesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('C compiler required')
        cls.tmp = tempfile.TemporaryDirectory()
        output = Path(cls.tmp.name) / 'values.so'
        root = Path(__file__).resolve().parents[1]
        subprocess.run([compiler, '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(root / 'runtime/skse_compat_values.c'), '-lm', '-o', str(output)], check=True)
        cls.lib = ctypes.CDLL(str(output))
        cls.lib.skse_native_table.argtypes = [ctypes.POINTER(ctypes.c_uint)]
        cls.lib.skse_native_table.restype = ctypes.POINTER(Native)
        cls.lib.skse_native_eval.argtypes = [ctypes.POINTER(Native), ctypes.POINTER(ctypes.c_uint32),
                                           ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_uint32),
                                           ctypes.POINTER(ctypes.c_void_p)]
        n = ctypes.c_uint()
        table = cls.lib.skse_native_table(ctypes.byref(n))
        cls.functions = {(table[i].cls.decode(), table[i].name.decode()): table[i] for i in range(n.value)}
        cls.free = ctypes.CDLL(None).free
        cls.free.argtypes = [ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def call(self, group, name, *args):
        fn = self.functions[group, name]
        self.assertEqual(len(args), fn.argc)
        values, strings = (ctypes.c_uint32 * 4)(), (ctypes.c_char_p * 4)()
        for i, arg in enumerate(args):
            if fn.args[i] == 2:
                strings[i] = arg
            else:
                values[i] = fbits(arg) if fn.args[i] == 4 else arg
        value, string = ctypes.c_uint32(), ctypes.c_void_p()
        self.assertEqual(self.lib.skse_native_eval(ctypes.byref(fn), values, strings, ctypes.byref(value), ctypes.byref(string)), 1)
        if fn.result == 2:
            try:
                return ctypes.string_at(string)
            finally:
                self.free(string)
        return struct.unpack('<f', struct.pack('<I', value.value))[0] if fn.result == 4 else value.value

    def test_math_unsigned_and_shift_boundaries(self):
        self.assertEqual(self.call('Math', 'LeftShift', 1, 31), 0x80000000)
        self.assertEqual(self.call('Math', 'LeftShift', 1, 32), 0)
        self.assertEqual(self.call('Math', 'RightShift', 0x80000000, 31), 1)
        self.assertEqual(self.call('Math', 'RightShift', 123, 0xffffffff), 0)
        self.assertEqual(self.call('Math', 'LogicalNot', 0), 0xffffffff)
        self.assertEqual(self.call('Math', 'LogicalAnd', 0xabcdef00, 0xff), 0)
        self.assertEqual(self.call('Math', 'LogicalOr', 0x100, 3), 0x103)
        self.assertEqual(self.call('Math', 'LogicalXor', 0xff, 0x0f), 0xf0)
        self.assertAlmostEqual(self.call('Math', 'Log', 1.0), 0)

    def test_strings_boundaries_and_byte_indices(self):
        self.assertEqual(self.call('StringUtil', 'GetLength', b'abc'), 3)
        self.assertEqual(self.call('StringUtil', 'GetLength', None), 0)
        self.assertEqual(self.call('StringUtil', 'GetNthChar', b'abc', 2), b'c')
        self.assertEqual(self.call('StringUtil', 'GetNthChar', b'abc', 0xffffffff), b'')
        self.assertEqual(self.call('StringUtil', 'Substring', b'abcdef', 2, 0), b'cdef')
        self.assertEqual(self.call('StringUtil', 'Substring', b'abcdef', 2, 2), b'cd')
        self.assertEqual(self.call('StringUtil', 'Find', b'Skyrim SKYRIM', b'sKyRiM', 1), 7)
        self.assertEqual(self.call('StringUtil', 'Find', b'abc', b'longer', 0), 0xffffffff)
        self.assertEqual(self.call('StringUtil', 'AsChar', 256), b'')
        self.assertEqual(self.call('StringUtil', 'AsChar', 65), b'A')
        for name, arg, result in [('IsLetter', b'A', 1), ('IsDigit', b'9', 1),
                                  ('IsPunctuation', b'!', 1), ('IsPrintable', b'\n', 0),
                                  ('AsOrd', b'A', 65)]:
            self.assertEqual(self.call('StringUtil', name, arg), result)

    def test_argb_and_hsv(self):
        for name, result in [('GetAlpha', 0x80), ('GetRed', 0x12), ('GetGreen', 0x34), ('GetBlue', 0x56)]:
            self.assertEqual(self.call('ColorComponent', name, 0x80123456), result)
        self.assertEqual(self.call('ColorComponent', 'SetRed', 0x80123456, 255), 0x80ff3456)
        self.assertAlmostEqual(self.call('ColorComponent', 'GetHue', 0xff00ff00), 120)
        self.assertAlmostEqual(self.call('ColorComponent', 'GetSaturation', 0xff808080), 0)
        self.assertEqual(self.call('ColorComponent', 'SetHue', 0x80ff0000, 120.0), 0x8000ff00)
        self.assertEqual(self.call('ColorComponent', 'SetSaturation', 0x80ff0000, 0.0), 0x80ffffff)

    def test_does_not_claim_full_skse(self):
        self.assertEqual(self.call('SKSE', 'GetVersion'), 0)
        self.assertEqual(self.call('SKSE', 'GetVersionMinor'), 1)
        self.assertEqual(self.call('SKSE', 'GetPluginVersion', b'missing'), 0xffffffff)
