# SPDX-License-Identifier: GPL-3.0-only
"""Host-width string contracts, guard pages and concurrent independent callers."""
import ctypes as C
import mmap
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class EngineStrings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = Path(cls.tmp.name) / 'strings.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'runtime/engine_strings.c'), '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so))
        for name, args, result in [
            ('length', [C.c_void_p], C.c_size_t),
            ('find', [C.c_void_p, C.c_int], C.c_void_p),
            ('copy_padded', [C.c_void_p, C.c_void_p, C.c_size_t], C.c_void_p),
        ]:
            fn = getattr(cls.lib, 'skydream_string_' + name)
            fn.argtypes, fn.restype = args, result
        cls.lib.skydream_bytes_find.argtypes = [C.c_void_p, C.c_int, C.c_size_t]
        cls.lib.skydream_bytes_find.restype = C.c_void_p

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_native_pointers_and_byte_contracts(self):
        rng = random.Random(921)
        self.assertEqual(C.sizeof(C.c_void_p), 8)
        for size in [0, 1, 2, 3, 7, 16, 31, 256, 4096]:
            data = bytes(rng.randrange(1, 256) for _ in range(size))
            with mmap.mmap(-1, size + 64) as buf:
                buf.write(data + b'\0' + b'X' * 63)
                address = C.addressof(C.c_char.from_buffer(buf))
                self.assertGreater(address, 0xffffffff)
                self.assertEqual(self.lib.skydream_string_length(address), size)
                for byte in [0, 1, 127, 128, 255, -1, 0x180]:
                    needle = bytes([byte & 255])
                    index = (data + b'\0').find(needle)
                    expected = address + index if index >= 0 else None
                    self.assertEqual(self.lib.skydream_string_find(address, byte), expected)
                    for n in [0, size // 2, size, size + 1]:
                        index = (data + b'\0')[:n].find(needle)
                        expected = address + index if index >= 0 else None
                        self.assertEqual(self.lib.skydream_bytes_find(address, byte, n), expected)
                for n in [0, size // 2, size, size + 8]:
                    with mmap.mmap(-1, n + 32) as dst:
                        dst.write(b'G' * (n + 32))
                        out = C.addressof(C.c_char.from_buffer(dst)) + 16
                        self.assertGreater(out, 0xffffffff)
                        self.assertEqual(self.lib.skydream_string_copy_padded(out, address, n), out)
                        self.assertEqual(dst[:], b'G' * 16 + data[:n].ljust(n, b'\0') + b'G' * 16)
        self.assertIsNone(self.lib.skydream_bytes_find(None, 0, 0))
        self.assertIsNone(self.lib.skydream_string_copy_padded(None, None, 0))

    def test_guard_pages_and_sixteen_threads(self):
        # Run unsafe-boundary tests in a subprocess so a bad read fails the test
        # without killing the test runner. Shared input is read-only to workers.
        source = r'''
#include "engine_strings.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
static const char shared[]="immutable input shared by sixteen callers";
static pthread_barrier_t barrier;
static void *worker(void *argument) {
    size_t id=(uintptr_t)argument; char out[512],input[257];
    pthread_barrier_wait(&barrier);
    for(size_t i=0;i<10000;i++) {
        size_t length=(i+id)%256,n=(i*7+id)%sizeof(out);
        for(size_t j=0;j<length;j++) input[j]=(char)(1+(j+id)%255);
        input[length]=0;
        assert(skydream_string_length(input)==length);
        assert(skydream_string_find(input,0)==input+length);
        assert(skydream_bytes_find(input,0,length)==NULL);
        assert(skydream_bytes_find(input,0,length+1)==input+length);
        assert(skydream_string_copy_padded(out,input,n)==out);
        for(size_t j=0;j<n;j++) assert(out[j]==(j<length?input[j]:0));
        assert(skydream_string_length(shared)==sizeof(shared)-1);
        assert(skydream_string_find(shared,'s')==shared+16);
    }
    return NULL;
}
int main(void) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    char *p=mmap(NULL,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(p!=MAP_FAILED); assert((uintptr_t)p>UINT32_MAX);
    assert(!mprotect(p+page,page,PROT_READ|PROT_WRITE));
    for(size_t n=1;n<=128;n++) {
        char *s=p+2*page-n;
        for(size_t i=0;i<n;i++) s[i]='a'; s[n-1]=0;
        assert(skydream_string_length(s)==n-1);
        assert(skydream_string_find(s,0)==s+n-1);
        assert(skydream_string_find(s,'z')==NULL);
        assert(skydream_bytes_find(s,0,n)==s+n-1);
        assert(skydream_bytes_find(s,'z',n)==NULL);
        char out[128]; skydream_string_copy_padded(out,s,n);
        for(size_t i=0;i<n;i++) assert(out[i]==s[i]);
        /* Unterminated bounded copy must not read past its limit. */
        s[n-1]='a'; skydream_string_copy_padded(out,s,n);
        for(size_t i=0;i<n;i++) assert(out[i]=='a');
    }
    assert(!munmap(p,page*3));
    pthread_t threads[16]; assert(!pthread_barrier_init(&barrier,NULL,16));
    for(uintptr_t i=0;i<16;i++) assert(!pthread_create(&threads[i],NULL,worker,(void *)i));
    for(int i=0;i<16;i++) assert(!pthread_join(threads[i],NULL));
    assert(!pthread_barrier_destroy(&barrier));
    puts("PASS: guard pages and 160000 independent test rounds on 16 threads");
}
'''
        tmp = Path(self.tmp.name)
        (tmp / 'threads.c').write_text(source)
        subprocess.run(['cc', '-O2', '-pthread', '-I', str(ROOT / 'runtime'),
                        str(tmp / 'threads.c'), str(ROOT / 'runtime/engine_strings.c'),
                        '-o', str(tmp / 'threads')], check=True)
        subprocess.run([str(tmp / 'threads')], check=True, timeout=30)

if __name__ == '__main__':
    unittest.main()
