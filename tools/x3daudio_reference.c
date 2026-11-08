/* SPDX-License-Identifier: GPL-3.0-only
 * Optional test oracle, built with i686-w64-mingw32-gcc and run under Wine.
 * Calls a user-supplied ORIGINAL DLL; it is never part of the game runtime.
 * Input: 8192-byte synthetic guest fixture used by test_x3daudio.py.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../third_party/f3daudio/F3DAudio.h"
_Static_assert(sizeof(F3DAUDIO_EMITTER)==100,"32-bit reference compiler required");
static void *resolve(unsigned char *memory,void *offset) {
    return offset ? memory+(uintptr_t)offset : NULL;
}
int main(int argc,char **argv) {
    if(argc<6 || (argc-2)%4) return 2;
    HMODULE dll=LoadLibraryA(argv[1]);
    if(!dll) {fprintf(stderr,"LoadLibrary failed: %lu\n",GetLastError());return 3;}
    void (__cdecl *initialize)(uint32_t,float,uint8_t *)=(void *)GetProcAddress(dll,"X3DAudioInitialize");
    void (__cdecl *calculate)(const uint8_t *,const F3DAUDIO_LISTENER *,const F3DAUDIO_EMITTER *,uint32_t,F3DAUDIO_DSP_SETTINGS *)=(void *)GetProcAddress(dll,"X3DAudioCalculate");
    if(!initialize || !calculate) return 4;
    for(int a=2;a<argc;a+=4) {
    unsigned char memory[8192],original[8192];
    FILE *f=fopen(argv[a],"rb");if(!f || fread(memory,1,sizeof memory,f)!=sizeof memory) return 5;fclose(f);
    memcpy(original,memory,sizeof memory);
    F3DAUDIO_LISTENER *l=(void *)(memory+512);
    F3DAUDIO_EMITTER *e=(void *)(memory+768);
    F3DAUDIO_DSP_SETTINGS *d=(void *)(memory+1024);
    l->pCone=resolve(memory,l->pCone);e->pCone=resolve(memory,e->pCone);
    e->pChannelAzimuths=resolve(memory,e->pChannelAzimuths);
    F3DAUDIO_DISTANCE_CURVE **curves[]={&e->pVolumeCurve,&e->pLFECurve,&e->pLPFDirectCurve,&e->pLPFReverbCurve,&e->pReverbCurve};
    for(unsigned i=0;i<5;i++) if(*curves[i]) {
        *curves[i]=resolve(memory,*curves[i]);
        (*curves[i])->pPoints=resolve(memory,(*curves[i])->pPoints);
    }
    d->pMatrixCoefficients=resolve(memory,d->pMatrixCoefficients);d->pDelayTimes=resolve(memory,d->pDelayTimes);
    initialize(strtoul(argv[a+2],NULL,0),343.5f,memory+256);
    calculate(memory+256,l,e,strtoul(argv[a+3],NULL,0),d);
    memcpy(original+1040,memory+1040,32);
    memcpy(original+1280,memory+1280,sizeof(float)*d->SrcChannelCount*d->DstChannelCount);
    f=fopen(argv[a+1],"wb");if(!f) return 6;
    int ok=fwrite(original,1,sizeof original,f)==sizeof original;fclose(f);if(!ok) return 7;
    }
    FreeLibrary(dll);return 0;
}
