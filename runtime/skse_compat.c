/* SPDX-License-Identifier: GPL-3.0-only
 * Independent classic-Papyrus compatibility bridge. Objects in guest memory
 * always use 32-bit fields, including on a 64-bit Linux host. */
#include "rt.h"
#include "skse_runtime.h"
#include <string.h>
#include <strings.h>

#ifdef GAME_SKYRIM
#ifdef SKYDREAM_SKSE
int skse_skyui_enabled(void) {
#ifdef SKYDREAM_SKYUI
    return 1;
#else
    const char *probe=getenv("LB_SKSE_SKYUI_PROBE");
    return probe && !strcmp(probe,"1");
#endif
}
/* Verified against TESV 1.9.32.0, not Special Edition. */
enum { VM_NATIVE_CTOR=0xC46C00, VM_DESTROY=0xC328E0,
       VM_ARG_OFFSET=0xC3A620, VM_ARG_GET=0xC3AC40,
       STRING_SET=0xA51210, HEAP_ALLOC=0xA48D60, FORM_HEAP=0x1B418B0,
       REGISTER_SLOT=0x17, FLAGS_SLOT=0x19, HAS_CALLBACK_SLOT=21, RUN_SLOT=22 };

uint32_t skse_member(CPU *c, uint32_t address, uint32_t self, int n, const uint32_t *args) {
    CPU saved=*c;
    c->r[1]=self;
    uint32_t result=rt_invoke(c,address,n,args);
    *c=saved;
    return result;
}
uint32_t skse_virtual(CPU *c, uint32_t self, unsigned slot, int n, const uint32_t *args) {
    return skse_member(c,M32(M32(self)+slot*4),self,n,args);
}
static void has_callback(CPU *c) { RET(1,0); }

static void run_native(CPU *c) {
    uint32_t self=c->r[1], result=ARG(3), state=ARG(4), index=M32(self+0x2c);
    unsigned count=0;
    const SkseNative *table=skse_native_table(&count);
    if(index>=count || !result || !state) RET(0,5);
    const SkseNative *fn=&table[index];
    static uint8_t seen[1024];
    if(index<1024 && !__atomic_exchange_n(&seen[index],1,__ATOMIC_RELAXED))
        LOG("SKSE native call: %s.%s",fn->class_name,fn->name);
    uint32_t args[4]={0}, offset=0, list=M32(state);
    const char *strings[4]={0};
    if(fn->argc) {
        if(!list) RET(0,5);
        offset=skse_member(c,VM_ARG_OFFSET,list,1,&state);
        for(unsigned i=0;i<fn->argc;i++) {
            uint32_t getargs[]={state,i,offset};
            uint32_t value=skse_member(c,VM_ARG_GET,list,3,getargs);
            if(!value) RET(0,5);
            if(M32(value)==0 && (fn->args[i]>=12 || fn->args[i]==2)) continue;
            if(M32(value)!=fn->args[i]) {LOG("SKSE argument mismatch: %s.%s[%u]: got %08x expected %u",fn->class_name,fn->name,i,M32(value),fn->args[i]);RET(0,5);}
            args[i]=M32(value+4);
            if(fn->args[i]==2) strings[i]=GSTR(args[i]);
        }
    }
    if(fn->operation>=SKSE_SYSTEM_BASE) {
        int ok=skse_system_run(c,fn,ARG(0),args,ARG(1),result);
        RET(ok,5);
    }
    uint32_t value=0;
    char *text=NULL;
    if(!skse_native_eval(fn,args,strings,&value,&text)) RET(0,5);
    if(skse_skyui_enabled() && !strcmp(fn->class_name,"SKSE")) {
        if(!strcmp(fn->name,"GetVersion")) value=1;
        if(!strcmp(fn->name,"GetVersionMinor")) value=7;
        if(!strcmp(fn->name,"GetVersionBeta")) value=3;
        if(!strcmp(fn->name,"GetVersionRelease")) value=48;
    }
    skse_member(c,VM_DESTROY,result,0,NULL);
    M32(result)=fn->result; M32(result+4)=0;
    if(fn->result==2) {
        uint32_t temporary=gstrdup(text);
        skse_member(c,STRING_SET,result+4,1,&temporary);
        gfree(temporary);free(text);
    } else M32(result+4)=value;
    RET(1,5);
}

/* Optional integration probe: exercise the actual game argument accessors,
 * constructor, vtable dispatch and string cache, not a mocked VM. No files or
 * savegames are created. Run with LB_SKSE_SELFTEST=1 in an enabled build. */
static void probe_native(CPU *c, uint32_t object, const SkseNative *fn) {
    uint32_t memory=gmalloc(0x300);
    memset(GPTR(memory),0,0x300);
    uint32_t list=memory, entries=memory+0x40, chunk=memory+0x80;
    uint32_t state=chunk+4, result=memory+0x200;
    M32(list+0x10)=entries; M32(list+0x28)=1;
    M32(entries)=chunk; M32(chunk)=0x100; M32(state)=list;
    M32(state+0x1c)=fn->argc;
    uint32_t args[4]={0}, owned[4]={0};
    const char *strings[4]={0};
    for(unsigned i=0;i<fn->argc;i++) {
        args[i]=fn->args[i]==4 ? 0x3f800000u : i ? 1 : 0x80123456u;
        if(fn->args[i]==2) {
            strings[i]=i ? "y" : "Skyrim";
            args[i]=owned[i]=gstrdup(strings[i]);
        }
        M32(state+0x24+8*i)=fn->args[i];
        M32(state+0x28+8*i)=args[i];
    }
    uint32_t expected=0; char *text=NULL;
    if(!skse_native_eval(fn,args,strings,&expected,&text)) rt_fatal("SKSE probe: evaluator failed");
    uint32_t runargs[]={0,0,0,result,state};
    if(!skse_virtual(c,object,RUN_SLOT,5,runargs) || M32(result)!=fn->result)
        rt_fatal("SKSE probe: %s.%s failed guest dispatch",fn->class_name,fn->name);
    if(fn->result==2) {
        const char *actual=GSTR(M32(result+4));
        /* Classic Papyrus interns strings case-insensitively. */
        if(strcasecmp(actual ? actual : "",text))
            rt_fatal("SKSE probe: %s.%s string result mismatch: '%s' != '%s'",fn->class_name,fn->name,actual ? actual : "",text);
    } else if(M32(result+4)!=expected) rt_fatal("SKSE probe: numeric result mismatch");
    skse_member(c,VM_DESTROY,result,0,NULL);
    free(text);
    for(unsigned i=0;i<4;i++) if(owned[i]) gfree(owned[i]);
    gfree(memory);
}

void skse_register_natives(CPU *c, uint32_t registry) {
    unsigned count=0;
    const SkseNative *table=skse_native_table(&count);
    if(skse_skyui_enabled()) LOG("SKSE: experimental SkyUI interface enabled; full SKSE compatibility NOT certified");
    uint32_t run=rt_thunk(run_native,"SkydreamSKSE.Papyrus.Run");
    uint32_t has=rt_thunk(has_callback,"SkydreamSKSE.Papyrus.HasCallback");
    uint32_t vtable=0;
    for(unsigned i=0;i<count;i++) {
        const SkseNative *fn=&table[i];
        uint32_t allocargs[]={0x30,0,0};
        uint32_t object=skse_member(c,HEAP_ALLOC,FORM_HEAP,3,allocargs);
        if(!object) rt_fatal("SKSE compatibility: cannot allocate native function");
        memset(GPTR(object),0,0x30);
        uint32_t name=gstrdup(fn->name), cls=gstrdup(fn->class_name);
        uint32_t args[]={name,cls,!(fn->operation&S_METHOD),fn->argc};
        skse_member(c,VM_NATIVE_CTOR,object,4,args);
        if(!vtable) {
            /* Preserve MSVC's RTTI locator immediately before the vtable. */
            vtable=gmalloc(24*4)+4;
            memcpy(GPTR(vtable-4),GPTR(M32(object)-4),24*4);
            M32(vtable+HAS_CALLBACK_SLOT*4)=has;
            M32(vtable+RUN_SLOT*4)=run;
        }
        M32(object)=vtable; M32(object+0x14)=fn->result;
        M32(object+0x2c)=i;
        uint32_t params=M32(object+0x18);
        for(unsigned j=0;j<fn->argc;j++) M32(params+8*j+4)=fn->args[j];
        skse_virtual(c,registry,REGISTER_SLOT,1,&object);
        uint32_t flagsargs[]={cls,name,1};
        skse_virtual(c,registry,FLAGS_SLOT,3,flagsargs);
        if(fn->operation<SKSE_SYSTEM_BASE && getenv("LB_SKSE_SELFTEST") && !strcmp(getenv("LB_SKSE_SELFTEST"),"1"))
            probe_native(c,object,fn);
        gfree(name);gfree(cls);
    }
    LOG("SKSE compatibility: registered %u native Papyrus functions; experimental API 0.1.0, native UI and menu event bridge; no DLL plugins/co-saves",count);
    if(getenv("LB_SKSE_SELFTEST") && !strcmp(getenv("LB_SKSE_SELFTEST"),"1"))
        LOG("SKSE compatibility: primitive guest ABI probes passed (system calls need gameplay tests)");
}

#endif
#endif
