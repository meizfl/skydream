/* SPDX-License-Identifier: GPL-3.0-only
 * Native interoperability with TESV's 32-bit Scaleform API. */
#include "skse_runtime.h"
#include <string.h>
#include <strings.h>

#ifdef GAME_SKYRIM
#ifdef SKYDREAM_SKSE
static int extend_inventory;
static pthread_mutex_t gfx_init_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t handler_vtable;

void skse_gfx_clean(CPU *c,uint32_t value) {
    if(M32(value+4)&64) {
        uint32_t args[]={value,M32(value+8)};
        skse_member(c,0x922660,M32(value),2,args);
    }
    memset(GPTR(value),0,16);
}
static const char *gfx_string(uint32_t value) {
    if((M32(value+4)&0x8f)!=4) return "";
    uint32_t p=M32(value+8);
    if((M32(value+4)&64) && p) p=M32(p);
    const char *s=GSTR(p);return s ? s : "";
}
static double gfx_number(uint32_t value) {
    unsigned type=M32(value+4)&0x8f;
    return type==3 ? MF64(value+8) : type==2 ? !!M8(value+8) : 0;
}
static void gfx_set_number(CPU *c,uint32_t value,double n) {
    skse_gfx_clean(c,value); M32(value+4)=3; MF64(value+8)=n;
}
static int gfx_set(CPU *c,uint32_t object,const char *name,uint32_t value) {
    uint32_t key=gstrdup(name);
    uint32_t args[]={M32(object+8),key,value,(M32(object+4)&0x8f)==8};
    int ok=skse_member(c,0x920D20,M32(object),4,args)!=0;
    gfree(key);return ok;
}
void skse_gfx_number(CPU *c,uint32_t object,const char *name,double n) {
    uint32_t value=gmalloc(16);memset(GPTR(value),0,16);
    M32(value+4)=3;MF64(value+8)=n;gfx_set(c,object,name,value);gfree(value);
}
void skse_gfx_read(CPU *c,uint32_t view,const char *path,uint32_t out) {
    uint32_t key=gstrdup(path),args[]={out,key};
    skse_virtual(c,view,17,2,args);gfree(key);
}

static uint32_t keycode(const char *name,unsigned device,unsigned context) {
    uint32_t manager=M32(0x12E7458);
    if(!manager || device>2 || context>=17) return UINT32_MAX;
    uint32_t ctx=M32(manager+0x34+context*4);
    if(!ctx) return UINT32_MAX;
    uint32_t map=ctx+12*device, data=M32(map),count=M32(map+8);
    if(count>4096 || !data) return UINT32_MAX;
    for(unsigned i=0;i<count;i++) {
        const char *event=GSTR(M32(data+i*16));
        if(event && !strcasecmp(event,name)) {
            unsigned key=M32(data+i*16+4);
            if(key==255) return UINT32_MAX;
            if(device==1) return key+256;
            if(device==2) {
                static const unsigned masks[]={1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x1000,0x2000,0x4000,0x8000,9,10};
                for(unsigned j=0;j<16;j++) if(key==masks[j]) return 266+j;
                return UINT32_MAX;
            }
            return key;
        }
    }
    return UINT32_MAX;
}
/* Menu table entries are {name, menu instance, creator, next}; scan active
 * entries rather than reproducing the engine's interned-string hash. */
uint32_t skse_menu_view(const char *name) {
    uint32_t manager=M32(0x12E3548);
    if(!manager || !name) return 0;
    uint32_t table=manager+0xa4, count=M32(table+4), entries=M32(table+0x18);
    if(!entries || count>4096) return 0;
    for(unsigned i=0;i<count;i++) {
        uint32_t entry=entries+i*16;
        if(!M32(entry+12)) continue;
        const char *key=GSTR(M32(entry));
        if(key && !strcmp(key,name)) {
            uint32_t menu=M32(entry+4);
            return menu ? M32(menu+8) : 0;
        }
    }
    return 0;
}
static char last_control[2][128];
static unsigned last_key[2];
static uint32_t remap_scope,remap_view;
static void end_remap(CPU *c,int key) {
    if(!remap_scope) return;
    uint32_t scope=remap_scope;remap_scope=0;
    uint32_t value=gmalloc(16),name=gstrdup("EndRemapMode");memset(GPTR(value),0,16);
    M32(value+4)=3;MF64(value+8)=key;
    uint32_t args[]={M32(scope+8),0,name,value,1,(M32(scope+4)&0x8f)==8};
    skse_member(c,0x922ED0,M32(scope),6,args);gfree(name);gfree(value);
    skse_gfx_clean(c,scope);gfree(scope);
    uint32_t player=M32(0x12E7454),menu=M32(0x1B10D5C);
    if(player) M8(player+0x40)=0;if(menu) M8(menu+0x42)=0;
    if(remap_view && !__atomic_sub_fetch((uint32_t *)GPTR(remap_view+4),1,__ATOMIC_ACQ_REL)) {
        uint32_t flags=1;skse_virtual(c,remap_view,0,1,&flags);
    }
    remap_view=0;LOG("SKSE input: remapped key %d",key);
}
void skse_gfx_input_event(CPU *c,unsigned key,const char *control,int down) {
    snprintf(last_control[!!down],sizeof last_control[0],"%s",control);
    last_key[!!down]=key;
    if(down && remap_scope) end_remap(c,key<282?(int)key:-1);
}
static void menu_message(CPU *c,const char *name,unsigned message) {
    uint32_t manager=M32(0x12E35E4);if(!manager) return;
    uint32_t ref=gmalloc(4),text=gstrdup(name);M32(ref)=0;
    skse_member(c,0xA51210,ref,1,&text);gfree(text);
    uint32_t args[]={ref,message,0};skse_member(c,0x431B00,manager,3,args);
    skse_member(c,0xA511B0,ref,0,NULL);gfree(ref);
}
enum { CB_LOG,CB_KEY,CB_TEXT,CB_EXTEND,CB_MOD_EVENT,CB_LAST_CONTROL,CB_LAST_KEY,CB_REMAP,
       CB_OPEN,CB_CLOSE,CB_GET_INI,CB_SET_INI,CB_CLIP_GET,CB_CLIP_SET };
static const struct {const char *name;unsigned op;} callbacks[]={
    {"Log",CB_LOG},{"GetMappedKey",CB_KEY},{"AllowTextInput",CB_TEXT},
    {"ExtendData",CB_EXTEND},{"SendModEvent",CB_MOD_EVENT},{"GetLastControl",CB_LAST_CONTROL},{"GetLastKeycode",CB_LAST_KEY},
    {"StartRemapMode",CB_REMAP},{"OpenMenu",CB_OPEN},{"CloseMenu",CB_CLOSE},
    {"GetINISetting",CB_GET_INI},{"SetINISetting",CB_SET_INI},{"GetClipboardData",CB_CLIP_GET},{"SetClipboardData",CB_CLIP_SET}
};
static void handler_destroy(CPU *c) {
    uint32_t self=c->r[1];
    if(ARG(0)&1) gfree(self);
    RET(self,1);
}
static void handler_invoke(CPU *c) {
    uint32_t self=c->r[1],p=ARG(0),out=M32(p),args=M32(p+16),n=M32(p+20);
    switch(M32(self+8)) {
    case CB_LOG:if(n) LOG("SkyUI: %s",gfx_string(args));break;
    case CB_KEY:
        if(n>=3 && out) {
            uint32_t key=keycode(gfx_string(args),(unsigned)gfx_number(args+16),(unsigned)gfx_number(args+32));
            gfx_set_number(c,out,key==UINT32_MAX ? -1 : (double)key);
        }
        break;
    case CB_TEXT:
        if(n) {
            uint32_t manager=M32(0x12E7458);
            if(manager) {
                unsigned value=M8(manager+0x98);
                if(gfx_number(args)) { if(value<255) value++; }
                else if(value) value--;
                M8(manager+0x98)=value;
            }
        }
        break;
    case CB_EXTEND:if(n) __atomic_store_n(&extend_inventory,!!gfx_number(args),__ATOMIC_RELAXED);break;
    case CB_MOD_EVENT:
        if(n) skse_send_mod_event(c,gfx_string(args),n>1?gfx_string(args+16):"",n>2?(float)gfx_number(args+32):0,0);
        break;
    case CB_LAST_CONTROL:
        if(out && n) {
            uint32_t text=gstrdup(last_control[!!gfx_number(args)]),params[]={out,text};
            skse_virtual(c,M32(p+4),11,2,params);gfree(text);
        }
        break;
    case CB_LAST_KEY:if(out && n) gfx_set_number(c,out,last_key[!!gfx_number(args)]);break;
    case CB_REMAP:
        if(n && (M32(args+4)&0x8f)>=6) {
            if(remap_scope) end_remap(c,-1);
            remap_scope=gmalloc(16);memcpy(GPTR(remap_scope),GPTR(args),16);
            if(M32(remap_scope+4)&64) {
                uint32_t retain[]={remap_scope,M32(remap_scope+8)};
                skse_member(c,0x921DA0,M32(remap_scope),2,retain);
            }
            remap_view=M32(p+4);__atomic_add_fetch((uint32_t *)GPTR(remap_view+4),1,__ATOMIC_RELAXED);
            uint32_t player=M32(0x12E7454),menu=M32(0x1B10D5C);
            if(player) M8(player+0x40)=1;if(menu) M8(menu+0x42)=1;
        }
        break;
    case CB_OPEN:case CB_CLOSE:if(n) menu_message(c,gfx_string(args),M32(self+8)==CB_OPEN?1:3);break;
    case CB_GET_INI:case CB_SET_INI:
        if(n) {
            uint32_t setting=skse_ini_setting(c,gfx_string(args));
            if(setting) {
                const char *name=GSTR(M32(setting+8));
                if(name && M32(self+8)==CB_SET_INI && n>=2) {
                    double number=gfx_number(args+16);
                    if(*name=='f') MF32(setting+4)=(float)number;
                    else if(*name=='i') M32(setting+4)=(int32_t)number;
                    else if(*name=='b') M8(setting+4)=number!=0;
                } else if(name && out) gfx_set_number(c,out,*name=='f'?MF32(setting+4):*name=='b'?!!M8(setting+4):(int32_t)M32(setting+4));
            }
        }
        break;
    /* Clipboard callbacks are intentionally not exposed until implemented. */
    default:LOG("SKSE Scaleform: unimplemented callback %u",M32(self+8));break;
    }
    RET(0,1);
}
void skse_gfx_install(CPU *c,uint32_t view) {
    skse_import_translations(c);
    uint32_t memory=gmalloc(16*4);memset(GPTR(memory),0,16*4);
    uint32_t global=memory, root=memory+16, value=memory+32, version=memory+48;
    uint32_t key=gstrdup("_global"), get[]={global,key};
    int ok=skse_virtual(c,view,17,2,get)!=0;gfree(key);
    if(!ok) {gfree(memory);return;}
    uint32_t create[]={root,0,0,0};skse_virtual(c,view,13,4,create);
    pthread_mutex_lock(&gfx_init_lock);
    if(!handler_vtable) {
        handler_vtable=gmalloc(8);
        M32(handler_vtable)=rt_thunk(handler_destroy,"SKSE.Scaleform.Destroy");
        M32(handler_vtable+4)=rt_thunk(handler_invoke,"SKSE.Scaleform.Invoke");
    }
    pthread_mutex_unlock(&gfx_init_lock);
    for(unsigned i=0;i<sizeof callbacks/sizeof callbacks[0];i++) {
        if(callbacks[i].op>=CB_CLIP_GET) continue;
        uint32_t handler=gmalloc(12);
        M32(handler)=handler_vtable;M32(handler+4)=1;M32(handler+8)=callbacks[i].op;
        uint32_t make[]={value,handler,0};skse_virtual(c,view,15,3,make);
        gfx_set(c,root,callbacks[i].name,value);
        skse_gfx_clean(c,value);
        /* Drop our reference; the created ActionScript function owns one. */
        if(!__atomic_sub_fetch((uint32_t *)GPTR(handler+4),1,__ATOMIC_ACQ_REL)) gfree(handler);
    }
    uint32_t versionargs[]={version,0,0,0};skse_virtual(c,view,13,4,versionargs);
    int probe=skse_skyui_enabled();
    skse_gfx_number(c,version,"major",probe?1:0);skse_gfx_number(c,version,"minor",probe?7:1);
    skse_gfx_number(c,version,"beta",probe?3:0);skse_gfx_number(c,version,"releaseIdx",probe?48:0);
    gfx_set(c,root,"version",version);
    gfx_set(c,global,"skse",root);
    skse_gfx_clean(c,version);skse_gfx_clean(c,root);skse_gfx_clean(c,global);
    gfree(memory);LOG("SKSE Scaleform: installed native root on view %08x",view);
}
static void extend_form(CPU *c,uint32_t value,uint32_t form,uint32_t view) {
    if(!form || !__atomic_load_n(&extend_inventory,__ATOMIC_RELAXED)) return;
    unsigned type=M8(form+0x12);
    skse_gfx_number(c,value,"formType",type);
    skse_gfx_number(c,value,"formId",M32(form+0xc));
    if(type==26) {
        skse_gfx_number(c,value,"partMask",M32(form+0xe4));
        skse_gfx_number(c,value,"weightClass",M32(form+0xe8));
    } else if(type==41) {
        skse_gfx_number(c,value,"weaponType",M8(form+0xf1));
        skse_gfx_number(c,value,"subType",M8(form+0xf1));
        skse_gfx_number(c,value,"baseDamage",M16(form+0x6c));
        skse_gfx_number(c,value,"speed",MF32(form+0xc4));
        skse_gfx_number(c,value,"reach",MF32(form+0xc8));
        skse_gfx_number(c,value,"stagger",MF32(form+0xdc));
        skse_gfx_number(c,value,"critDamage",M16(form+0xfc));
        skse_gfx_number(c,value,"minRange",MF32(form+0xcc));
        skse_gfx_number(c,value,"maxRange",MF32(form+0xd0));
        uint32_t slot=M32(form+0x7c);
        if(slot) skse_gfx_number(c,value,"equipSlot",M32(slot+0xc));
    } else if(type==42) skse_gfx_number(c,value,"flags",M32(form+0x90));
    else if(type==52) {
        skse_gfx_number(c,value,"soulSize",M8(form+0x8c));skse_gfx_number(c,value,"gemSize",M8(form+0x8d));
    } else if(type==46) skse_gfx_number(c,value,"flags",M32(form+0xa8));
    else if(type==27) {
        skse_gfx_number(c,value,"flags",M8(form+0x94));skse_gfx_number(c,value,"bookType",M8(form+0x95));
        if(M8(form+0x94)&4) {
            uint32_t spell=M32(form+0x98);skse_gfx_number(c,value,"teachesSpell",spell?M32(spell+0xc):-1);
        } else if(M8(form+0x94)&1) skse_gfx_number(c,value,"teachesSkill",M32(form+0x98));
    }
    /* Query the game's RTTI for a keyword component rather than assuming
     * every form has the same inheritance layout. */
    uint32_t castargs[]={form,0,0x123B000,0x123B0F4,0};
    uint32_t component=skse_member(c,0xF51DAE,0,5,castargs);
    if(component) {
        uint32_t data=M32(component+4),count=M32(component+8);
        if(data && count<4096) {
            uint32_t temp=gmalloc(32);memset(GPTR(temp),0,32);
            uint32_t create[]={temp,0,0,0};skse_virtual(c,view,13,4,create);
            for(unsigned i=0;i<count;i++) {
                uint32_t keyword=M32(data+4*i);
                if(keyword) {
                    const char *name=GSTR(M32(keyword+0x14));
                    if(name) skse_gfx_number(c,temp,name,1);
                }
            }
            gfx_set(c,value,"keywords",temp);skse_gfx_clean(c,temp);gfree(temp);
        }
    }
}
#endif

/* Host overrides keep the original guest bodies for disabled builds. */
extern void f_A60200_guest(CPU *);
void f_A60200(CPU *c) {
    f_A60200_guest(c);
#ifdef SKYDREAM_SKSE
    uint32_t view=c->r[0];
    if(view) skse_gfx_install(c,view);
#endif
}
extern void f_842140_guest(CPU *);
void f_842140(CPU *c) {
#ifdef SKYDREAM_SKSE
    uint32_t object=c->r[1],entry=ARG(1),viewptr=ARG(0);
#endif
    f_842140_guest(c);
#ifdef SKYDREAM_SKSE
    if(entry && viewptr) extend_form(c,object+0x10,M32(entry),M32(viewptr));
#endif
}
extern void f_873D70_guest(CPU *);
void f_873D70(CPU *c) {
#ifdef SKYDREAM_SKSE
    uint32_t object=c->r[1],form=ARG(1),viewptr=ARG(0);
#endif
    f_873D70_guest(c);
#ifdef SKYDREAM_SKSE
    if(viewptr) extend_form(c,object+0x10,form,M32(viewptr));
#endif
}
#endif
