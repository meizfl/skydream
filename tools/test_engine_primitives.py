# SPDX-License-Identifier: GPL-3.0-only
"""Native 64-bit primitive API: no guest image or original lookup tables."""
import ctypes as C
from pathlib import Path
import mmap,random,subprocess,tempfile,unittest,zlib
ROOT=Path(__file__).resolve().parents[1]
class EnginePrimitives(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();so=Path(cls.tmp.name)/'engine.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',str(ROOT/'runtime/engine_primitives.c'),'-o',str(so)],check=True)
        cls.lib=C.CDLL(str(so));cls.lib.skydream_engine_move.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t];cls.lib.skydream_engine_move.restype=C.c_void_p
        cls.lib.skydream_engine_crc32.argtypes=[C.c_void_p,C.c_size_t];cls.lib.skydream_engine_crc32.restype=C.c_uint32
        cls.lib.skydream_engine_crc32_u32.argtypes=[C.c_uint32];cls.lib.skydream_engine_crc32_u32.restype=C.c_uint32
        cls.lib.skydream_engine_crc32_mapped.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(C.c_size_t)];cls.lib.skydream_engine_crc32_mapped.restype=C.c_uint32
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_overlap_and_native_pointers(self):
        self.assertEqual(C.sizeof(C.c_void_p),8)
        for size in [0,1,3,16,255,256,257,4096]:
            for source,dest in [(8,9),(9,8),(8,8),(8,4200)]:
                data=bytes(i%251 for i in range(8400));buf=mmap.mmap(-1,len(data));buf.write(data);address=C.addressof(C.c_char.from_buffer(buf))
                self.assertGreater(address,0xffffffff)
                expected=bytearray(data);expected[dest:dest+size]=data[source:source+size]
                result=self.lib.skydream_engine_move(address+dest,address+source,size)
                self.assertEqual(result,address+dest);self.assertEqual(buf[:],expected);buf.close()
        self.assertIsNone(self.lib.skydream_engine_move(None,None,0))
    def test_raw_crc_and_fixed_width_key(self):
        rng=random.Random(234)
        for n in [0,1,2,3,4,7,255,256,4096,65536]:
            data=rng.randbytes(n);buf=mmap.mmap(-1,max(1,n));buf.write(data);address=C.addressof(C.c_char.from_buffer(buf))
            self.assertGreater(address,0xffffffff)
            self.assertEqual(self.lib.skydream_engine_crc32(address,n),zlib.crc32(data,0xffffffff)^0xffffffff);buf.close()
        self.assertEqual(self.lib.skydream_engine_crc32(C.create_string_buffer(b'123456789'),9),0x2dfd2d88)
        for value in [0,1,0x80000000,0xffffffff,0x12345678]:
            self.assertEqual(self.lib.skydream_engine_crc32_u32(value),zlib.crc32(value.to_bytes(4,'little'),0xffffffff)^0xffffffff)
    def test_signed_translation_index_and_stop(self):
        table=(C.c_uint8*256)(*range(256));table[128+ord('Q')]=0
        for data in [b'abcQrest',b'\x80\xff\x7f',b'\x00\x01',b'']:
            translated=bytes(table[(byte+128)&255] for byte in data).split(b'\0')[0];buf=C.create_string_buffer(data);count=C.c_size_t(999)
            value=self.lib.skydream_engine_crc32_mapped(buf,len(data),C.addressof(table)+128,C.byref(count))
            self.assertEqual(count.value,len(translated));self.assertEqual(value,zlib.crc32(translated,0xffffffff)^0xffffffff)
if __name__=='__main__':unittest.main()
