"""Spatial behavior and pointer translation with real 32-bit guest layouts."""
import ctypes
import math
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class X3DAudioTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = pathlib.Path(cls.tmp.name) / 'x3d.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', str(ROOT/'runtime/x3daudio_native.c'),
                        str(ROOT/'third_party/f3daudio/F3DAudio.c'), '-lm', '-o', str(so)], check=True)
        cls.lib = ctypes.CDLL(str(so))
        cls.lib.x3d_native_initialize.argtypes = [ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,ctypes.c_float,ctypes.c_uint32]
        cls.lib.x3d_native_calculate.argtypes = [ctypes.c_void_p,ctypes.c_size_t,*([ctypes.c_uint32]*5)]
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def fixture(self, mask=3, channels=1):
        mem = ctypes.create_string_buffer(8192)
        # Instance=256, listener=512, emitter=768, DSP=1024, matrix=1280.
        struct.pack_into('<12fI', mem,512,0,0,1,0,1,0,0,0,0,0,0,0,0)
        struct.pack_into('<I12f2fIf6I2f',mem,768,0,0,0,1,0,1,0,0,0,10,0,0,0,
                         0,0,channels,1,1536 if channels>1 else 0,0,0,0,0,0,100,1)
        if channels>1: struct.pack_into('<2f',mem,1536,math.pi/2,3*math.pi/2)
        dst = bin(mask).count("1")
        struct.pack_into('<4I8f',mem,1024,1280,0,channels,dst,*([-123.0]*8))
        self.assertEqual(self.lib.x3d_native_initialize(mem,len(mem),mask,343.5,256),0)
        return mem
    def calculate(self,mem,flags=0x7d):
        self.assertEqual(self.lib.x3d_native_calculate(mem,len(mem),256,512,768,flags,1024),0)
        src,dst=struct.unpack_from('<2I',mem,1032)
        return struct.unpack_from('<'+'f'*(src*dst),mem,1280),struct.unpack_from('<8f',mem,1040)
    def test_stereo_rotation_distance_and_doppler(self):
        mem=self.fixture();center,values=self.calculate(mem)
        self.assertAlmostEqual(center[0],center[1],places=6)
        self.assertAlmostEqual(values[3],1.0,places=6)
        self.assertAlmostEqual(values[5],10.0,places=5)
        struct.pack_into('<3f',mem,768+28,10,0,0)
        right,_=self.calculate(mem);self.assertGreater(right[1],right[0])
        struct.pack_into('<3f',mem,768+28,-10,0,0)
        left,_=self.calculate(mem);self.assertAlmostEqual(left[0],right[1],places=6)
        struct.pack_into('<3f',mem,512,0,0,-1)
        rotated,_=self.calculate(mem);self.assertGreater(rotated[1],rotated[0])
        struct.pack_into('<3f',mem,512,0,0,1)
        struct.pack_into('<3f',mem,768+28,0,0,200)
        far,_=self.calculate(mem);self.assertLess(sum(far),sum(center))
        struct.pack_into('<3f',mem,768+40,0,0,-30)
        _,approach=self.calculate(mem);self.assertGreater(approach[3],1)
        struct.pack_into('<3f',mem,768+40,0,0,30)
        _,retreat=self.calculate(mem);self.assertLess(retreat[3],1)
    def test_nested_curves_and_untouched_outputs(self):
        mem=self.fixture()
        struct.pack_into('<II',mem,1600,1700,2)
        struct.pack_into('<4f',mem,1700,0,0.25,1,0.25)
        struct.pack_into('<I',mem,768+72,1600)
        matrix,values=self.calculate(mem,1)
        self.assertAlmostEqual(sum(matrix),0.25,places=5)
        self.assertEqual(values[0],-123)
        self.assertEqual(values[3],-123)
        self.assertEqual(values[5],10)
    def test_surround_multichannel_and_bounds(self):
        mem=self.fixture(mask=0x3f,channels=2)
        matrix,_=self.calculate(mem)
        self.assertEqual(len(matrix),12)
        self.assertTrue(all(math.isfinite(x) and x>=0 for x in matrix))
        struct.pack_into('<I',mem,1024,8190)
        self.assertEqual(self.lib.x3d_native_calculate(mem,len(mem),256,512,768,1,1024),1)
        mem=self.fixture();struct.pack_into('<I',mem,768+72,8190)
        self.assertEqual(self.lib.x3d_native_calculate(mem,len(mem),256,512,768,1,1024),1)
        self.assertEqual(self.lib.x3d_native_calculate(mem,len(mem),256,512,768,2,1024),3)

if __name__=='__main__':unittest.main()
