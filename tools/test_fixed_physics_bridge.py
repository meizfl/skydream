# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real bridge with a deterministic clock and private guest memory."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FixedPhysicsBridge(unittest.TestCase):
    def test_realtime_ticks_pause_resume_and_abi(self):
        source=r'''
#include "rt.h"
#include "physics_timing.h"
#include <assert.h>
#include <time.h>
uint8_t *MEM;int g_log_level;
static uint64_t ns;static unsigned original_calls;
int fake_clock(clockid_t id,struct timespec *t){assert(id==CLOCK_MONOTONIC);t->tv_sec=ns/1000000000;t->tv_nsec=ns%1000000000;return 0;}
void f_D0DB50_guest(CPU *c){original_calls++;c->r[4]+=4;}
void f_D4EE40_guest(CPU *c){(void)c;}
void f_D50ED0_guest(CPU *c){(void)c;}
void physics_timing_init(void);void f_D0DB50(CPU *);
float physics_motion_delta(void);
static unsigned frame(float submitted,float unscaled){
 CPU c={0};c.r[4]=0x20000;
 for(int k=0;k<8;k++)if(k!=4)c.r[k]=0x123000+k;
 CPU before=c;M32(c.r[4])=0xabcdef;MF32(c.r[4]+4)=submitted;M32(c.r[4]+8)=0;
 MF32(0x1B4ADE4)=unscaled;f_D0DB50(&c);
 if(M32(0x1B4ADF8)==0 && submitted>0) assert(physics_motion_delta()==submitted);
 if(M32(0x1B4ADF8)!=0) assert(physics_motion_delta()==0);
 if(M32(0x1B4ADF8)==0 && submitted==0 && unscaled==0)
   assert(physics_motion_delta()>0); /* sub-ms active frame, not a zero solver batch */
 assert(c.r[4]==before.r[4]+4);for(int k=3;k<8;k++)if(k!=4)assert(c.r[k]==before.r[k]);
 assert(M32(0x20000)==0xabcdef);
 assert(MF32(0x12D18E0)==(float)SKYDREAM_PHYSICS_STEP);
 unsigned steps=(unsigned)lround(MF32(0x12D18DC)/SKYDREAM_PHYSICS_STEP);
 assert(steps<=SKYDREAM_PHYSICS_MAX_STEPS);
 assert(MF32(0x12D18DC)==(float)(steps*SKYDREAM_PHYSICS_STEP));return steps;
}
int main(void){
 MEM=calloc(1,0x2000000);assert(MEM);unsetenv("SKYDREAM_PHYSICS_MODE");unsetenv("SKYDREAM_HAVOK_FIX");
 unsigned rates[]={30,60,100,144,240,1000,2000};
 for(unsigned r=0;r<7;r++)for(unsigned slow=0;slow<2;slow++){
   physics_timing_init();ns=1000000000;M32(0x1B4ADF8)=0;float scale=slow?0.1f:1.0f;MF32(0x12C2410)=scale;
   assert(frame(0.001f*scale,0.001f)==0);uint64_t start=ns;unsigned ticks=0;
   for(unsigned i=0;i<rates[r]*2;i++){
     uint64_t old=ns;ns+=1000000000/rates[r];float quantized=(float)(ns/1000000-old/1000000)*0.001f;
     ticks+=frame(quantized*scale,quantized);
   }
   assert(ticks==(unsigned)floor(((double)(ns-start)*1e-9*scale+1e-12)/SKYDREAM_PHYSICS_STEP));
   M32(0x1B4ADF8)=1;ns+=10000000000ull;assert(frame(0,0)==0);
   M32(0x1B4ADF8)=0;ns+=10000000000ull;assert(frame(0.001f*scale,0.001f)==0);
   /* The resume frame establishes a new real-time origin, no 10-second jump. */
   ns+=1000000;assert(frame(0.001f*scale,0.001f)<=1);
 }
 assert(original_calls==0);
 setenv("SKYDREAM_HAVOK_FIX","0",1);physics_timing_init();CPU c={0};c.r[4]=0x20000;f_D0DB50(&c);assert(original_calls==1);
 free(MEM);puts("PASS: fixed bridge 30..2000 Hz, quantization, pause/resume, slow motion and cdecl ABI");
}
'''
        with tempfile.TemporaryDirectory() as folder:
            tmp=Path(folder);(tmp/'bridge.c').write_text(source)
            subprocess.run(['cc','-O2','-DGAME_SKYRIM','-Dclock_gettime=fake_clock','-I',str(ROOT/'runtime'),str(tmp/'bridge.c'),str(ROOT/'runtime/physics_timing.c'),str(ROOT/'runtime/physics_timing_bridge.c'),'-lm','-o',str(tmp/'bridge')],check=True)
            subprocess.run([str(tmp/'bridge')],check=True,timeout=30)
if __name__=='__main__':unittest.main()
