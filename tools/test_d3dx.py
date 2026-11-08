"""Native D3DX math and bounded DDS parsing; no game or helper DLL required."""
import ctypes as C
from pathlib import Path
import random, struct, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class Image(C.Structure):
    _fields_=[(x,C.c_uint32) for x in ('width','height','depth','mips','format','type','file_format')]+[('data_offset',C.c_size_t)]+[(x,C.c_uint32) for x in ('block_bytes','pixel_bytes','base_pitch')]
class NativeD3DX(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();so=Path(cls.tmp.name)/'d3dx.so'
        subprocess.run(['cc','-shared','-fPIC','-O2',str(ROOT/'runtime/d3dx_math.c'),str(ROOT/'runtime/d3dx_image.c'),'-lm','-o',str(so)],check=True)
        cls.lib=C.CDLL(str(so));cls.lib.native_d3dx_matrix_inverse.restype=C.c_void_p
        cls.lib.native_d3dx_image_info.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Image)]
        cls.lib.native_d3dx_dds_level.argtypes=[C.POINTER(Image),C.c_size_t,C.c_uint32,C.c_uint32,C.POINTER(C.c_size_t)]+[C.POINTER(C.c_uint32)]*5
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_inverse_and_aliasing(self):
        rng=random.Random(42)
        for _ in range(100):
            a=[rng.uniform(-1,1)+(4 if r==c else 0) for r in range(4) for c in range(4)]
            mat=(C.c_float*16)(*a);inverse=(C.c_float*16)(*a);det=C.c_float()
            self.assertTrue(self.lib.native_d3dx_matrix_inverse(inverse,C.byref(det),inverse))
            for r in range(4):
                for c in range(4):self.assertAlmostEqual(sum(a[r*4+k]*inverse[k*4+c] for k in range(4)),float(r==c),places=5)
            result=(C.c_float*16)();self.lib.native_d3dx_matrix_multiply(result,mat,inverse)
            self.lib.native_d3dx_matrix_multiply(mat,mat,inverse)
            self.assertEqual(list(mat),list(result))
    def test_singular_preserves_output(self):
        out=(C.c_float*16)(*range(16));zero=(C.c_float*16)();det=C.c_float(9)
        self.assertFalse(self.lib.native_d3dx_matrix_inverse(out,C.byref(det),zero));self.assertEqual(det.value,0);self.assertEqual(list(out),list(range(16)))
    def test_projective_coord_and_normal(self):
        m=(C.c_float*16)(2,0,0,0,0,3,0,0,0,0,4,0,10,20,30,2);v=(C.c_float*3)(1,2,3)
        self.lib.native_d3dx_vec3_coord(v,v,m);self.assertEqual(list(v),[6,13,21])
        v=(C.c_float*3)(1,2,3);self.lib.native_d3dx_vec3_normal(v,v,m);self.assertEqual(list(v),[2,6,12])
    def test_zero_normalize(self):
        for name,n in [('vec3',3),('plane',4)]:
            v=(C.c_float*n)();getattr(self.lib,'native_d3dx_'+name+'_normalize')(v,v);self.assertEqual(list(v),[0]*n)
    def dds(self,cube=False,volume=False):
        h=[0]*32;h[0]=0x20534444;h[1]=124;h[2]=0x21007;h[3]=h[4]=8;h[6]=4 if volume else 0;h[7]=4;h[19]=32;h[20]=4;h[21]=0x31545844;h[28]=0xfe00 if cube else 0x200000 if volume else 0
        size=sum(((max(1,8>>l)+3)//4)**2*8*(max(1,4>>l) if volume else 1) for l in range(4))*(6 if cube else 1)
        return struct.pack('<32I',*h)+bytes(size)
    def test_dds_subresources_and_bounds(self):
        for cube,volume in [(False,False),(True,False),(False,True)]:
            data=self.dds(cube,volume);buf=C.create_string_buffer(data);info=Image()
            self.assertTrue(self.lib.native_d3dx_image_info(buf,len(data),C.byref(info)))
            self.assertEqual(info.type,5 if cube else 4 if volume else 3)
            for face in range(6 if cube else 1):
                for level in range(4):
                    at=C.c_size_t();dims=[C.c_uint32() for _ in range(5)];args=[C.byref(info),len(data),face,level,C.byref(at),*[C.byref(v) for v in dims]]
                    self.assertTrue(self.lib.native_d3dx_dds_level(*args));w,h,d,pitch,rows=[v.value for v in dims];end=at.value+pitch*rows*d
                    self.assertLessEqual(end,len(data));args[1]=end-1;self.assertFalse(self.lib.native_d3dx_dds_level(*args))
    def test_invalid_dds_headers(self):
        data=self.dds()
        for at,value in [(4,0),(76,0),(16,0),(12,0xffffffff),(28,32),(84,0x30315844)]:
            bad=bytearray(data);struct.pack_into('<I',bad,at,value);buf=C.create_string_buffer(bytes(bad));self.assertFalse(self.lib.native_d3dx_image_info(buf,len(bad),C.byref(Image())))
if __name__=='__main__':unittest.main()
