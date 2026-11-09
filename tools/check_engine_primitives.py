#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Private differential oracle against lifted TESV functions; no Wine needed."""
from pathlib import Path
import argparse,re,subprocess,tempfile
from embed_game_image import parse_image
ROOT=Path(__file__).resolve().parents[1]
TARGETS=['F537F0','F52E30','A4A4E0','A4A530','A4A570']
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('image',type=Path);args=ap.parse_args()
    parsed=parse_image(args.image.read_bytes());memory=bytearray(0x2000000)
    for address,data in parsed['segments']:memory[address:address+len(data)]=data
    # Initialize the scalar path; the harness toggles the original SSE path.
    memory[0x1bc0468:0x1bc046c]=bytes(4)
    bodies=[]
    files=list((ROOT/'gen').glob('exe_*.c'))
    for address in TARGETS:
        for file in files:
            text=file.read_text();match=re.search(r'void f_'+address+r'(?:_guest)?\(CPU \*c\) \{',text)
            if not match:continue
            start=match.start();at=match.end();depth=1
            while depth:
                if text[at]=='{':depth+=1
                elif text[at]=='}':depth-=1
                at+=1
            body=text[start:at];bodies.append(re.sub(r'void f_'+address+r'(?:_guest)?', 'void oracle_'+address,body,count=1));break
        else:raise RuntimeError('missing lifted '+address)
    extra=[];extra_addresses=[]
    # Lifted jump-table landing pads are separate callable roots. Include their
    # real bodies rather than substituting host code in the reference path.
    for file in files:
        text=file.read_text()
        for match in re.finditer(r'void f_([0-9A-F]+)\(CPU \*c\) \{',text):
            address=int(match.group(1),16)
            if not (0xF52E30<address<0xF53190 or 0xF537F0<address<0xF53B55 or address in (0xF6029C,0xF60323)):continue
            at=match.end();depth=1
            while depth:
                if text[at]=='{':depth+=1
                elif text[at]=='}':depth-=1
                at+=1
            extra.append(text[match.start():at]);extra_addresses.append(address)
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp);(tmp/'memory.bin').write_bytes(memory)
        c='''#include "rt.h"
#include "lift_prelude.h"
#include <assert.h>
uint8_t *MEM;int g_log_level=0;
void rt_call(CPU *c,uint32_t t){fprintf(stderr,"unexpected oracle call %x\\n",t);abort();}

'''+ '\n'.join(bodies)+'\n'+r'''
static unsigned seed=123456;static unsigned random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static CPU init(unsigned *args,unsigned n){CPU c={0};for(int i=0;i<8;i++)c.r[i]=random32();c.r[4]=0x20000;M32(c.r[4])=0xabcdef;for(unsigned i=0;i<n;i++)M32(c.r[4]+4+i*4)=args[i];return c;}
static void check(CPU *a,CPU *b,int returns_value){assert(a->r[4]==b->r[4]);if(returns_value)assert(a->r[0]==b->r[0]);for(int i=3;i<8;i++)assert(a->r[i]==b->r[i]);assert(a->df==b->df);}
int main(int argc,char **argv){MEM=calloc(1,0x2000000);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(MEM,1,0x2000000,f)==0x2000000);fclose(f);
void (*original[])(CPU *)={oracle_F537F0,oracle_F52E30,oracle_A4A4E0,oracle_A4A530,oracle_A4A570};
void (*native[])(CPU *)={f_F537F0,f_F52E30,f_A4A4E0,f_A4A530,f_A4A570};
unsigned sizes[]={0,1,2,3,4,7,8,15,16,31,32,63,64,127,128,255,256,257,4095,4096};
unsigned char before[20480],expected[20480];unsigned total=0;
for(unsigned iteration=0;iteration<4000;iteration++)for(unsigned target=0;target<5;target++){
 M32(0x1bc0468)=(iteration>>1)&1;
 for(unsigned k=0;k<sizeof before;k++)before[k]=random32();memcpy(MEM+0x30000,before,sizeof before);
 for(unsigned k=0;k<256;k++)M8(0x40000+k)=(uint8_t)(k+128);if(iteration&1)M8(0x40080+'Q')=0;
 unsigned size=iteration<20?sizes[iteration]:random32()%4097,src=0x31000+(iteration%32),dst;
 switch(iteration%4){case 0:dst=src;break;case 1:dst=src+1+iteration%32;break;case 2:dst=src-1-iteration%32;break;default:dst=0x33000+iteration%32;}
 unsigned args[4];if(target<2){args[0]=dst;args[1]=src;args[2]=size;}
 else {args[0]=(iteration&2)?src+4:0x33f00;args[1]=target==4?random32():src;args[2]=size;args[3]=0x40080;}
 CPU a=init(args,4),b=a;original[target](&a);memcpy(expected,MEM+0x30000,sizeof expected);
 memcpy(MEM+0x30000,before,sizeof before);native[target](&b);
 if(memcmp(expected,MEM+0x30000,sizeof expected)){fprintf(stderr,"mismatch target %u iteration %u size %u\\n",target,iteration,size);return 1;}
 check(&a,&b,target<3||target==4);++total;
}
printf("PASS: %u differential cases, five TESV entry points; memory, returns and preserved ABI\n",total);free(MEM);}
'''
        # Original prototypes keep direct generated calls and the bridge declarations type-checked.
        c=c.replace('#include "lift_prelude.h"','#include "lift_prelude.h"\n#include "funcs_exe.h"')
        declaration='\n'.join('void f_%X(CPU *);'%a for a in extra_addresses)
        c=c.replace('void rt_call(CPU *c,uint32_t t){fprintf(stderr,"unexpected oracle call %x\\n",t);abort();}',
            'void rt_call(CPU *c,uint32_t t){switch(t){'+''.join('case 0x%Xu:f_%X(c);return;'%(a,a) for a in extra_addresses)+'default:fprintf(stderr,"unexpected oracle call %x\\n",t);abort();}}')
        c=c.replace('uint8_t *MEM;int g_log_level=0;','uint8_t *MEM;int g_log_level=0;\n'+declaration)
        c+='\n'+'\n'.join(extra)
        (tmp/'oracle.c').write_text(c)
        subprocess.run(['cc','-O2',*['-Df_'+address+'_guest=oracle_'+address for address in TARGETS],'-DGAME_SKYRIM','-I',str(ROOT/'runtime'),'-I',str(ROOT/'gen'),str(tmp/'oracle.c'),str(ROOT/'runtime/engine_primitives.c'),str(ROOT/'runtime/engine_primitives_bridge.c'),'-lm','-o',str(tmp/'oracle')],check=True)
        subprocess.run([str(tmp/'oracle'),str(tmp/'memory.bin')],check=True)
if __name__=='__main__':main()
