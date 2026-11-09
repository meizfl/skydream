#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Private scheduler oracle: high-resolution clock selection, preserved ABI and
real lifted accumulator behavior at 15..2000 Hz, time scaling and ms quantization.
No original code or game image is published by this tool.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gen', type=Path, default=ROOT/'gen')
    args=ap.parse_args()
    body=None
    for file in sorted(args.gen.glob('exe_*.c')):
        text=file.read_text()
        match=re.search(r'void f_D0DB50(?:_guest)?\(CPU \*c\) \{',text)
        if match:
            body=text[match.start():text.index('\n}\n',match.end())+3]
            break
    if body is None or 'SKYDREAM_DEDUP:' in body:
        raise RuntimeError('original lifted D0DB50 body required')
    body=re.sub(r'void f_D0DB50(?:_guest)?', 'void oracle', body, count=1)
    harness=r'''
#include "rt.h"
#include "lift_prelude.h"
#include "physics_timing.h"
#include <assert.h>
#include <time.h>
uint8_t *MEM; int g_log_level;
void rt_call(CPU *c,uint32_t t){(void)c;(void)t;abort();}
static uint64_t clock_ns;
int fake_clock(clockid_t id, struct timespec *value) {
 assert(id==CLOCK_MONOTONIC); value->tv_sec=clock_ns/1000000000;
 value->tv_nsec=clock_ns%1000000000; return 0;
}
''' + body + r'''
void physics_timing_init(void); void f_D0DB50(CPU *);
void f_D4EE40_guest(CPU *c){(void)c;abort();}
void f_D50ED0_guest(CPU *c){(void)c;abort();}
static CPU cpu(float delta,unsigned complex) {
 CPU c={0}; c.r[4]=0x20000;
 for(int k=0;k<8;k++) if(k!=4)c.r[k]=0x12340+k;
 M32(c.r[4])=0xabcdef; MF32(c.r[4]+4)=delta; M32(c.r[4]+8)=complex;
 return c;
}
int main(void) {
 MEM=calloc(1,0x2000000); assert(MEM);
 setenv("SKYDREAM_PHYSICS_MODE","adaptive",1);
 unsigned addresses[]={0x12D18E4,0x12D18E0,0x12D18DC,0x1BB5E20};
 double rates[]={15,30,60,100,144,240,1000,2000};
 double scales[]={0,0.1,1,2}; unsigned cases=0;
 for(unsigned r=0;r<8;r++) for(unsigned s=0;s<4;s++) for(unsigned mode=0;mode<2;mode++) {
   unsetenv("SKYDREAM_HAVOK_FIX"); physics_timing_init(); clock_ns=1000000000;
   MF32(0x12D19E8)=1.0f/60; MF32(0x12D19C4)=1.0f/30;
   MF32(0x12C2410)=1; M32(0x12D19AC)=3; M32(0x12D19B8)=1;
   MF32(0x1BB5E20)=0; MF32(0x1B4ADE4)=0;
   CPU warm=cpu(0,mode); f_D0DB50(&warm); /* initialize timestamp */
   uint64_t previous=clock_ns; double simulated=0,submitted=0;
   for(unsigned i=0;i<2000;i++) {
     /* Alternate actual rates abruptly too; engine delta remains ms-quantized. */
     double hz=(i%101==0)?2000:rates[r]; uint64_t tick=(uint64_t)(1e9/hz);
     clock_ns+=tick; double seconds=(double)tick*1e-9;
     float quantized=(float)((clock_ns/1000000)-(previous/1000000))*0.001f;
     previous=clock_ns; float delta=(float)(quantized*scales[s]);
     float remainder=MF32(0x1BB5E20),normal,complex;
     assert(skydream_physics_limits(seconds,&normal,&complex));
     MF32(0x12D19E8)=normal; MF32(0x12D19C4)=complex;
     MF32(0x12C2410)=(float)scales[s];
     CPU a=cpu(delta,mode),b=a; oracle(&a);
     uint32_t expected[4];for(int k=0;k<4;k++)expected[k]=M32(addresses[k]);
     MF32(0x1BB5E20)=remainder;
     /* A quantized zero must not force the previous frame's limits at 2000 Hz. */
     MF32(0x1B4ADE4)=quantized;
     MF32(0x12D19E8)=1.0f/60; MF32(0x12D19C4)=1.0f/30;
     (void)cpu(delta,mode); /* original uses caller argument slots as scratch */
     f_D0DB50(&b);
     assert(MF32(0x12D19E8)==normal);assert(MF32(0x12D19C4)==complex);
     for(int k=0;k<4;k++)assert(M32(addresses[k])==expected[k]);
     assert(a.r[0]==b.r[0]);for(int k=3;k<8;k++)assert(a.r[k]==b.r[k]);
     assert(a.top==b.top);assert(a.df==b.df);
     simulated+=MF32(0x12D18DC);submitted+=delta;++cases;
   }
   assert(fabs(submitted-simulated-MF32(0x1BB5E20))<1e-5*fmax(1.0,submitted));
 }
 /* Disabled fix must leave user settings entirely untouched. */
 setenv("SKYDREAM_HAVOK_FIX","0",1);physics_timing_init();
 MF32(0x12D19E8)=0.007f;MF32(0x12D19C4)=0.009f;
 CPU a=cpu(0,0);f_D0DB50(&a);
 assert(MF32(0x12D19E8)==0.007f&&MF32(0x12D19C4)==0.009f);
 printf("PASS: %u original-scheduler cases; realtime clock, 2000 Hz quantization, slow motion, ABI, time conservation\n",cases);
 free(MEM);
}
'''
    with tempfile.TemporaryDirectory() as folder:
        tmp=Path(folder);(tmp/'oracle.c').write_text(harness)
        subprocess.run(['cc','-O2','-DGAME_SKYRIM','-Dclock_gettime=fake_clock',
                        '-Df_D0DB50_guest=oracle','-I',str(ROOT/'runtime'),
                        str(tmp/'oracle.c'),str(ROOT/'runtime/physics_timing.c'),
                        str(ROOT/'runtime/physics_timing_bridge.c'),'-lm','-o',str(tmp/'oracle')],check=True)
        subprocess.run([str(tmp/'oracle')],check=True)
if __name__=='__main__':main()
