/* SPDX-License-Identifier: GPL-3.0-only
 * Observe actual engine ButtonEvents, including keyboard, mouse and gamepad. */
#include "skse_runtime.h"
#include <string.h>
#ifdef SKYDREAM_SKSE
static uint32_t sink,attached_dispatcher;
static void sink_destroy(CPU *c) {RET(c->r[1],1);}
static unsigned macro_key(unsigned device,unsigned key) {
    if(device==0) return key;
    if(device==1) return key+256;
    static const unsigned masks[]={1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x1000,0x2000,0x4000,0x8000,9,10};
    for(unsigned i=0;i<16;i++) if(masks[i]==key) return 266+i;
    return 282;
}
static void sink_event(CPU *c) {
    uint32_t pointer=ARG(0),event=pointer?M32(pointer):0;
    for(unsigned steps=0;event && steps<4096;steps++,event=M32(event+0xc)) {
        if(M32(event+8)!=0) continue;
        unsigned device=M32(event+4),key=macro_key(device,M32(event+0x14));
        if(device>2 || key>=282) continue;
        int down=M32(event+0x18)!=0 && MF32(event+0x1c)==0;
        int up=M32(event+0x18)==0 && MF32(event+0x1c)!=0;
        if(!down && !up) continue;
        const char *control=GSTR(M32(event+0x10));
        skse_gfx_input_event(c,key,control?control:"",down);
    }
    RET(0,2);
}
void skse_input_install(CPU *c) {
    uint32_t dispatcher=M32(0x1B2E724);
    if(!dispatcher || dispatcher==attached_dispatcher) return;
    if(!sink) {
        uint32_t vt=gmalloc(8);M32(vt)=rt_thunk(sink_destroy,"SKSE.Input.Destroy");
        M32(vt+4)=rt_thunk(sink_event,"SKSE.Input.Receive");sink=gmalloc(4);M32(sink)=vt;
    }
    skse_member(c,0x6E3E30,dispatcher,1,&sink);
    /* Present and engine input delivery run on the UI thread. Put the observer
     * first so CLIK's immediate Key callbacks see this event, not the last one.
     * Never reorder a dispatcher while it is delivering an event. */
    if(!M8(dispatcher+0x2c)) {
        uint32_t data=M32(dispatcher+8),count=M32(dispatcher+0x10);
        if(data && count<4096) for(unsigned i=0;i<count;i++) if(M32(data+4*i)==sink) {
            memmove(GPTR(data+4),GPTR(data),4*i);M32(data)=sink;break;
        }
    }
    attached_dispatcher=dispatcher;LOG("SKSE input: attached native engine observer");
}
#endif
