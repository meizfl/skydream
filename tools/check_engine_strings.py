#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Private differential oracle for four lifted TESV byte-string entry points.
No image is needed: these reference functions use no original global tables.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ['F5B670', 'F520F0', 'F55A90', 'F59140']

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gen', type=Path, default=ROOT / 'gen')
    args = ap.parse_args()
    bodies = {}
    for file in sorted(args.gen.glob('exe_*.c')):
        text = file.read_text()
        for address in TARGETS:
            match = re.search(r'void f_' + address + r'(?:_guest)?\(CPU \*c\) \{', text)
            if not match:
                continue
            end = text.index('\n}\n', match.end()) + 3
            body = text[match.start():end]
            # Fail rather than silently compare a dedup forwarder or helper stub.
            if 'SKYDREAM_DEDUP:' in body or re.search(r'\bf_[0-9A-F]+\(c\)', body):
                raise RuntimeError('reference must be a self-contained original body: ' + address)
            bodies[address] = re.sub(r'void f_' + address + r'(?:_guest)?',
                                     'void oracle_' + address, body, count=1)
    if set(bodies) != set(TARGETS):
        raise RuntimeError('missing original bodies: ' + str(set(TARGETS) - set(bodies)))
    harness = r'''
#include "rt.h"
#include "lift_prelude.h"
#include <assert.h>
uint8_t *MEM; int g_log_level;
void rt_call(CPU *c, uint32_t target) { (void)c; (void)target; abort(); }
''' + '\n'.join(bodies[a] for a in TARGETS) + r'''
void f_F5B670(CPU *); void f_F520F0(CPU *);
void f_F55A90(CPU *); void f_F59140(CPU *);
void engine_strings_init(void);
static uint32_t seed=567891;
static uint32_t random32(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
int main(int argc, char **argv) {
 (void)argc; (void)argv;
 MEM=calloc(1,0x100000); assert(MEM); engine_strings_init();
 void (*original[])(CPU *)={oracle_F5B670,oracle_F520F0,oracle_F55A90,oracle_F59140};
 void (*native[])(CPU *)={f_F5B670,f_F520F0,f_F55A90,f_F59140};
 unsigned sizes[]={0,1,2,3,4,7,8,15,16,31,32,63,64,127,128,255,256,257,4095,4096};
 unsigned char before[12288],expected[12288]; unsigned total=0;
 for(unsigned i=0;i<8000;i++) for(unsigned t=0;t<4;t++) {
   unsigned n=i<20?sizes[i]:random32()%4097;
   for(unsigned k=0;k<sizeof before;k++) before[k]=(unsigned char)random32();
   memcpy(MEM+0x30000,before,sizeof before);
   unsigned src=0x30020+i%32,dst=0x32020+(i/32)%32;
   unsigned length=random32()%4097;
   /* Include embedded zero, high-bit bytes, missing byte and terminator search. */
   M8(src+length)=0;
   if(i%3==0) memset(MEM+src,0xa5,length);
   unsigned byte=i<256?i:random32();
   if(i%7==0) byte=0; if(i%11==0) byte=0x1a5;
   CPU a={0}; for(int k=0;k<8;k++) a.r[k]=random32();
   a.r[4]=0x20000; a.df=i&1; M32(a.r[4])=0x12345678;
   M32(a.r[4]+4)=t==3?dst:src;
   M32(a.r[4]+8)=t==3?src:byte;
   M32(a.r[4]+12)=n;
   CPU b=a; memcpy(before,MEM+0x30000,sizeof before);
   original[t](&a); memcpy(expected,MEM+0x30000,sizeof expected);
   memcpy(MEM+0x30000,before,sizeof before); native[t](&b);
   if(a.r[0]!=b.r[0] || a.r[4]!=b.r[4] || memcmp(expected,MEM+0x30000,sizeof expected)) {
     fprintf(stderr,"string mismatch target=%u iteration=%u n=%u return=%x/%x\n",t,i,n,a.r[0],b.r[0]); return 1;
   }
   for(int k=3;k<8;k++) assert(a.r[k]==b.r[k]);
   assert(a.df==b.df); assert(M32(0x20000)==0x12345678); ++total;
 }
 printf("PASS: %u differential cases, four string entries; results, writes, preserved ABI\n",total);
 free(MEM); return 0;
}
'''
    with tempfile.TemporaryDirectory() as folder:
        tmp = Path(folder)
        (tmp / 'oracle.c').write_text(harness)
        subprocess.run(['cc', '-O2', '-DGAME_SKYRIM', '-I', str(ROOT / 'runtime'),
                        *['-Df_' + a + '_guest=oracle_' + a for a in TARGETS],
                        str(tmp / 'oracle.c'), str(ROOT / 'runtime/engine_strings.c'),
                        str(ROOT / 'runtime/engine_strings_bridge.c'), '-lm',
                        '-o', str(tmp / 'oracle')], check=True)
        subprocess.run([str(tmp / 'oracle')], check=True)

if __name__ == '__main__':
    main()
