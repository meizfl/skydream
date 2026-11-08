"""Exercise build-time PE conversion and native initialization without game assets."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
embed=module('embed_game_image');fixture=module('test_discover')

class EmbeddedImageTests(unittest.TestCase):
    def fixture(self,path):
        fixture.make_pe(path);data=bytearray(path.read_bytes())
        struct.pack_into('<II',data,0x98+168,0x2300,24)
        struct.pack_into('<6I',data,0x700,0x402340,0x402344,0x402348,0x402350,8,0)
        data[0x740:0x744]=b'abcd'
        struct.pack_into('<III',data,0x440,0x2080,0x80000007,0)
        return data
    def test_metadata_and_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'fixture.exe';data=self.fixture(path);parsed=embed.parse_image(data)
            self.assertEqual(parsed['entry'],0x401000)
            self.assertEqual(parsed['imports'],[(0x402050,'KERNEL32.dll','ExitProcess'),(0x402054,'KERNEL32.dll','#7')])
            self.assertEqual(parsed['tls'],[0x402340,0x402344,0x402348,8,0x402350])
            for cut in (0,1,63,127,511,len(data)-1):
                with self.assertRaises(ValueError):embed.parse_image(data[:cut])
            for at,value in [(0x3c,0xffffffff),(0x98+56,0xffffffff),(0x98+104,0xffffffff),(0x98+224+12,0x100)]:
                bad=data.copy();struct.pack_into('<I',bad,at,value)
                with self.assertRaises(ValueError):embed.parse_image(bad)
            bad=data.copy();bad[0x98+224:0x98+224+8]=b'.bind\0\0\0'
            with self.assertRaisesRegex(ValueError,'SteamStub'):embed.parse_image(bad)
    def test_linked_initialization_without_original_file(self):
        with tempfile.TemporaryDirectory(prefix='embedded image ') as tmp:
            base=Path(tmp);source=base/'fixture.exe';source.write_bytes(self.fixture(source));out=base/'generated'
            embed.generate(source,out)
            expected=b''.join(chunk for _,chunk in embed.parse_image(source.read_bytes())['segments'])
            self.assertEqual((out/'skyrim_image.bin').read_bytes(),expected)
            harness=base/'test.c';harness.write_text(r'''
#define _GNU_SOURCE
#include "rt.h"
#include "embedded_image.h"
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
uint8_t *MEM;int g_log_level=0;uint32_t g_entry,g_image_size;unsigned imports,tls,modules;
uint32_t rt_import(const char *dll,const char *name){assert(!strcmp(dll,"KERNEL32.dll"));assert(!strcmp(name,imports?"#7":"ExitProcess"));return 0xff000000+4*imports++;}
void rt_module_add(const char *name,uint32_t base){assert(!strcmp(name,"TESV.exe")&&base==0x400000);modules++;}
void rt_set_static_tls(uint32_t start,uint32_t end,uint32_t index,uint32_t zero){assert(start==0x402340&&end==0x402344&&index==0x402348&&zero==8);assert(!memcmp(MEM+start,"abcd",4));tls++;}
int main(void){MEM=mmap(0,0x500000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(MEM!=MAP_FAILED);rt_load_embedded_game();assert(g_entry==0x401000&&g_image_size==0x3000);assert(imports==2&&tls==1&&modules==1);assert(M32(0x402050)==0xff000000&&M32(0x402054)==0xff000004);assert(M8(0x401000)==0xe8);assert(M8(0x400800)==0);return 0;}
''')
            executable=base/'test';subprocess.run(['cc','-DSKYDREAM_EMBEDDED_IMAGE','-DGAME_SKYRIM','-I',str(ROOT/'runtime'),str(harness),str(ROOT/'runtime/embedded_image.c'),str(out/'skyrim_image.c'),str(out/'skyrim_image.S'),'-o',str(executable)],check=True)
            source.unlink();subprocess.run([str(executable)],cwd='/',check=True)

if __name__=='__main__':unittest.main()
