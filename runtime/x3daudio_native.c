/* SPDX-License-Identifier: GPL-3.0-only
 * Translate classic 32-bit X3DAudio structures into native F3DAudio values. */
#include "x3daudio_native.h"
#include "../third_party/f3daudio/F3DAudio.h"
#include <string.h>

typedef struct { float front[3],top[3],position[3],velocity[3];uint32_t cone; } Listener32;
typedef struct {
    uint32_t cone;float front[3],top[3],position[3],velocity[3];
    float inner_radius,inner_angle;uint32_t channels;float channel_radius;
    uint32_t azimuths,curves[5];float distance_scaler,doppler_scaler;
} Emitter32;
typedef struct {
    uint32_t matrix,delay,src,dst;
    float lpf_direct,lpf_reverb,reverb,doppler,angle,distance,emitter_velocity,listener_velocity;
} DSP32;
_Static_assert(sizeof(Listener32)==52,"listener guest ABI");
_Static_assert(sizeof(Emitter32)==100,"emitter guest ABI");
_Static_assert(offsetof(Emitter32,curves)==72,"curve guest ABI");
_Static_assert(sizeof(DSP32)==48,"DSP guest ABI");
static void *span(uint8_t *mem,size_t bytes,uint32_t address,size_t count,size_t width) {
    if(!address || address>bytes || (width && count>(bytes-address)/width)) return NULL;
    return mem+address;
}
int x3d_native_initialize(uint8_t *mem,size_t bytes,uint32_t mask,float speed,uint32_t instance) {
    uint8_t *out=span(mem,bytes,instance,20,1);
    if(!out) return X3D_BAD_MEMORY;
    union {uint32_t align[5];F3DAUDIO_HANDLE data;} handle;
    if(F3DAudioInitialize8(mask,speed,handle.data)) return X3D_BAD_INIT;
    memcpy(out,handle.data,20);return X3D_OK;
}
int x3d_native_calculate(uint8_t *mem,size_t bytes,uint32_t instance,uint32_t listener,uint32_t emitter,uint32_t flags,uint32_t dsp) {
    /* FAudio currently has no stereo interaural delay implementation. Do not
     * silently claim support or return zero delays as if they were correct. */
    if(flags&F3DAUDIO_CALCULATE_DELAY) return X3D_UNSUPPORTED_DELAY;
    const void *hp=span(mem,bytes,instance,20,1),*lp=span(mem,bytes,listener,1,sizeof(Listener32));
    const void *ep=span(mem,bytes,emitter,1,sizeof(Emitter32));
    void *dp=span(mem,bytes,dsp,1,sizeof(DSP32));
    if(!hp || !lp || !ep || !dp) return X3D_BAD_MEMORY;
    union {uint32_t align[5];F3DAUDIO_HANDLE data;} handle;
    Listener32 l;Emitter32 e;DSP32 d;
    memcpy(handle.data,hp,20);memcpy(&l,lp,sizeof l);memcpy(&e,ep,sizeof e);memcpy(&d,dp,sizeof d);
    if(!e.channels || e.channels>64 || d.src!=e.channels || !d.dst || d.dst>8) return X3D_BAD_CHANNELS;
    F3DAUDIO_LISTENER nl={0};F3DAUDIO_EMITTER ne={0};F3DAUDIO_DSP_SETTINGS nd={0};
    memcpy(&nl.OrientFront,l.front,48);memcpy(&ne.OrientFront,e.front,48);
    if(l.cone && !(nl.pCone=span(mem,bytes,l.cone,1,sizeof(F3DAUDIO_CONE)))) return X3D_BAD_MEMORY;
    if(e.cone && !(ne.pCone=span(mem,bytes,e.cone,1,sizeof(F3DAUDIO_CONE)))) return X3D_BAD_MEMORY;
    ne.InnerRadius=e.inner_radius;ne.InnerRadiusAngle=e.inner_angle;ne.ChannelCount=e.channels;
    ne.ChannelRadius=e.channel_radius;ne.CurveDistanceScaler=e.distance_scaler;ne.DopplerScaler=e.doppler_scaler;
    if(e.azimuths && !(ne.pChannelAzimuths=span(mem,bytes,e.azimuths,e.channels,sizeof(float)))) return X3D_BAD_MEMORY;
    if(e.channels>1 && !ne.pChannelAzimuths) return X3D_BAD_MEMORY;
    F3DAUDIO_DISTANCE_CURVE curves[5];
    F3DAUDIO_DISTANCE_CURVE *converted[5]={0};
    for(unsigned i=0;i<5;i++) if(e.curves[i]) {
        const uint8_t *raw=span(mem,bytes,e.curves[i],8,1);uint32_t points,count;
        if(!raw) return X3D_BAD_MEMORY;
        memcpy(&points,raw,4);memcpy(&count,raw+4,4);
        if(count<2 || !(curves[i].pPoints=span(mem,bytes,points,count,sizeof(F3DAUDIO_DISTANCE_CURVE_POINT)))) return X3D_BAD_MEMORY;
        curves[i].PointCount=count;converted[i]=&curves[i];
    }
    ne.pVolumeCurve=converted[0];ne.pLFECurve=converted[1];ne.pLPFDirectCurve=converted[2];
    ne.pLPFReverbCurve=converted[3];ne.pReverbCurve=converted[4];
    nd.SrcChannelCount=d.src;nd.DstChannelCount=d.dst;
    if(flags&F3DAUDIO_CALCULATE_MATRIX) {
        nd.pMatrixCoefficients=span(mem,bytes,d.matrix,(size_t)d.src*d.dst,sizeof(float));
        if(!nd.pMatrixCoefficients) return X3D_BAD_MEMORY;
    }
    memcpy(&nd.LPFDirectCoefficient,&d.lpf_direct,8*sizeof(float));
    F3DAudioCalculate(handle.data,&nl,&ne,flags,&nd);
    memcpy(&d.lpf_direct,&nd.LPFDirectCoefficient,8*sizeof(float));
    memcpy(dp,&d,sizeof d);return X3D_OK;
}
