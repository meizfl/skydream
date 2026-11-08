/* SPDX-License-Identifier: GPL-3.0-only
 * Independently implemented Papyrus UI commands and event subscriptions. */
#include "skse_runtime.h"
#include <string.h>
#include <strings.h>
#include <SDL2/SDL.h>
#ifdef SKYDREAM_SKSE

typedef struct UiCommand {
    struct UiCommand *next;
    char *menu,*target;
    unsigned operation,count;
    uint32_t values; /* owned, reference-counted guest VMValue array */
} UiCommand;
static pthread_mutex_t queue_lock=PTHREAD_MUTEX_INITIALIZER;
static UiCommand *queue_head,*queue_tail;

typedef struct Registration {
    struct Registration *next;
    uint64_t handle;
    uint32_t registry;
    unsigned kind,key,active;
    char *name,*callback;
    uint32_t down_tick;
} Registration;
static Registration *registrations;
static pthread_mutex_t event_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t event_vtable;

static uint32_t float_bits(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
static float as_float(uint32_t u) {float f;memcpy(&f,&u,4);return f;}
static const char *str(uint32_t p) {const char *s=GSTR(p);return s?s:"";}
static void vm_clear(CPU *c,uint32_t value) {skse_member(c,0xC328E0,value,0,NULL);}
static void vm_string(CPU *c,uint32_t value,const char *text) {
    vm_clear(c,value);M32(value)=2;M32(value+4)=0;
    uint32_t temp=gstrdup(text);skse_member(c,0xA51210,value+4,1,&temp);gfree(temp);
}
static void vm_copy(CPU *c,uint32_t dst,uint32_t src) {skse_member(c,0xC329E0,dst,1,&src);}
static uint64_t base_handle(uint32_t base) {
    if(!base || M32(base)<16 || !M32(base+4)) return 0;
    return M64(M32(base+4)+0x10);
}
static void handle_ref(CPU *c,uint64_t handle,int add) {
    uint32_t policy=M32(0x1BA7008),args[]={(uint32_t)handle,(uint32_t)(handle>>32)};
    if(policy) skse_virtual(c,policy,add?9:10,2,args);
}
static uint32_t resolve_form(CPU *c,uint32_t base) {
    uint64_t handle=base_handle(base);uint32_t policy=M32(0x1BA7008);
    uint32_t args[]={0,(uint32_t)handle,(uint32_t)(handle>>32)};
    return handle&&policy ? skse_virtual(c,policy,8,3,args) : 0;
}
static void gfx_from_vm(uint32_t gfx,uint32_t vm) {
    memset(GPTR(gfx),0,16);
    unsigned type=M32(vm);uint32_t value=M32(vm+4);
    if(type==2) {M32(gfx+4)=4;M32(gfx+8)=value;}
    else if(type==3) {M32(gfx+4)=3;MF64(gfx+8)=(int32_t)value;}
    else if(type==4) {M32(gfx+4)=3;MF64(gfx+8)=as_float(value);}
    else if(type==5) {M32(gfx+4)=2;M8(gfx+8)=!!value;}
    else M32(gfx+4)=1;
}
static void enqueue(CPU *c,const SkseNative *fn,const uint32_t *args) {
    UiCommand *cmd=calloc(1,sizeof *cmd);
    if(!cmd) rt_fatal("SKSE UI queue allocation failed");
    cmd->menu=strdup(str(args[0]));cmd->target=strdup(str(args[1]));cmd->operation=fn->operation;
    unsigned type=fn->args[2];
    uint32_t array=type>=12 ? args[2] : 0;
    cmd->count=type>=12 ? (array ? M32(array+8) : 0) : 1;
    if(cmd->count>4096) rt_fatal("SKSE UI argument array too large");
    cmd->values=gmalloc(8*(cmd->count?cmd->count:1));memset(GPTR(cmd->values),0,8*(cmd->count?cmd->count:1));
    if(array) for(unsigned i=0;i<cmd->count;i++) vm_copy(c,cmd->values+i*8,array+0x14+i*8);
    else if(type<12) {
        if(type==2) vm_string(c,cmd->values,str(args[2]));
        else {M32(cmd->values)=type;M32(cmd->values+4)=args[2];}
    }
    pthread_mutex_lock(&queue_lock);
    if(queue_tail) queue_tail->next=cmd;else queue_head=cmd;
    queue_tail=cmd;pthread_mutex_unlock(&queue_lock);
}
static void flush_ui(CPU *c) {
    pthread_mutex_lock(&queue_lock);UiCommand *cmd=queue_head;queue_head=queue_tail=NULL;pthread_mutex_unlock(&queue_lock);
    while(cmd) {
        UiCommand *next=cmd->next;uint32_t view=skse_menu_view(cmd->menu);
        if(view) {
            uint32_t target=gstrdup(cmd->target),gfx=gmalloc(16*(cmd->count?cmd->count:1));
            memset(GPTR(gfx),0,16*(cmd->count?cmd->count:1));
            for(unsigned i=0;i<cmd->count;i++) gfx_from_vm(gfx+16*i,cmd->values+8*i);
            if(cmd->operation>=S_UI_SBOOL && cmd->operation<=S_UI_SSTRING) {
                uint32_t args[]={target,gfx,1};skse_virtual(c,view,16,3,args);
            } else {
                uint32_t args[]={target,0,gfx,cmd->count};skse_virtual(c,view,23,4,args);
            }
            gfree(gfx);gfree(target);
        }
        for(unsigned i=0;i<cmd->count;i++) vm_clear(c,cmd->values+8*i);
        gfree(cmd->values);free(cmd->menu);free(cmd->target);free(cmd);cmd=next;
    }
}
static void event_destroy(CPU *c) {RET(c->r[1],1);}
static void event_copy(CPU *c) {
    uint32_t self=c->r[1],out=ARG(0),count=M32(self+4);
    if(!skse_member(c,0x8C2A70,out,1,&count)) RET(0,1);
    for(unsigned i=0;i<count;i++) vm_copy(c,M32(out+4)+8*i,self+8+8*i);
    RET(1,1);
}
static void send_event(CPU *c,const Registration *r,const char *event,unsigned count,uint32_t values) {
    if(!event_vtable) {
        event_vtable=gmalloc(8);M32(event_vtable)=rt_thunk(event_destroy,"SKSE.Event.Destroy");
        M32(event_vtable+4)=rt_thunk(event_copy,"SKSE.Event.Copy");
    }
    uint32_t object=gmalloc(8+count*8),name=gmalloc(4),temp=gstrdup(event);
    memset(GPTR(object),0,8+count*8);M32(object)=event_vtable;M32(object+4)=count;M32(name)=0;
    skse_member(c,0xA51210,name,1,&temp);gfree(temp);
    for(unsigned i=0;i<count;i++) vm_copy(c,object+8+8*i,values+8*i);
    uint32_t args[]={(uint32_t)r->handle,(uint32_t)(r->handle>>32),name,object};
    skse_virtual(c,r->registry,35,4,args);
    for(unsigned i=0;i<count;i++) vm_clear(c,object+8+8*i);
    skse_member(c,0xA511B0,name,0,NULL);gfree(name);gfree(object);
    LOG("SKSE event: %s -> %016llx",event,(unsigned long long)r->handle);
}
void skse_send_mod_event(CPU *c,const char *name,const char *text,float number,uint32_t sender) {
    typedef struct Target {Registration r;struct Target *next;} Target;
    Target *targets=NULL;
    pthread_mutex_lock(&event_lock);
    for(Registration *r=registrations;r;r=r->next) if(r->kind==2 && !strcmp(r->name,name)) {
        Target *t=calloc(1,sizeof *t);if(!t) rt_fatal("SKSE mod event allocation failed");
        t->r=*r;t->r.callback=strdup(r->callback);handle_ref(c,r->handle,1);t->next=targets;targets=t;
    }
    pthread_mutex_unlock(&event_lock);
    uint32_t values=gmalloc(32);memset(GPTR(values),0,32);
    vm_string(c,values,name);vm_string(c,values+8,text);M32(values+16)=4;M32(values+20)=float_bits(number);
    if(sender) vm_copy(c,values+24,sender);
    while(targets) {
        Target *t=targets;targets=t->next;send_event(c,&t->r,t->r.callback,4,values);
        handle_ref(c,t->r.handle,0);free(t->r.callback);free(t);
    }
    for(unsigned i=0;i<4;i++) vm_clear(c,values+8*i);
    gfree(values);
}
static void change_registration(CPU *c,unsigned op,uint32_t registry,uint32_t base,const uint32_t *args) {
    uint64_t handle=base_handle(base);if(!handle) return;
    unsigned kind=op>=S_REG_MOD ? 2 : op>=S_REG_KEY ? 1 : 0;
    int add=op==S_REG_MENU || op==S_REG_KEY || op==S_REG_MOD;
    int all=op==S_UNREG_MENUS || op==S_UNREG_KEYS || op==S_UNREG_MODS;
    const char *name=kind==1 ? "" : str(args[0]);
    pthread_mutex_lock(&event_lock);
    Registration **link=&registrations;
    while(*link) {
        Registration *r=*link;
        int match=r->handle==handle && r->kind==kind && (all || (kind==1 ? r->key==args[0] : !strcmp(r->name,name)));
        if(match) {
            *link=r->next;handle_ref(c,r->handle,0);free(r->name);free(r->callback);free(r);
        } else link=&r->next;
    }
    if(add) {
        Registration *r=calloc(1,sizeof *r);
        if(!r) rt_fatal("SKSE event allocation failed");
        r->handle=handle;r->registry=registry;r->kind=kind;r->key=args[0];
        r->name=strdup(name);r->callback=strdup(kind==2 ? str(args[1]) : "");
        r->active=kind==0 ? !!skse_menu_view(name) : 0;
        handle_ref(c,handle,1);r->next=registrations;registrations=r;
        LOG("SKSE subscribe: kind %u %s key %u handle %016llx",kind,name,r->key,(unsigned long long)handle);
    }
    pthread_mutex_unlock(&event_lock);
}
static void pump_events(CPU *c) {
    extern const Uint8 *platform_keys(int *);
    extern uint32_t dinput_dik_of(int);
    const Uint8 *keys=platform_keys(NULL);
    pthread_mutex_lock(&event_lock);
    /* Copy deliveries before calling the VM: callbacks may register again. */
    typedef struct Delivery {Registration r;char *event;uint32_t key;int opened;struct Delivery *next;} Delivery;
    Delivery *deliveries=NULL;
    for(Registration *r=registrations;r;r=r->next) {
        if(r->kind>1) continue;
        unsigned active=0;
        if(r->kind==0) active=!!skse_menu_view(r->name);
        else for(unsigned i=0;i<SDL_NUM_SCANCODES;i++) if(keys[i] && dinput_dik_of(i)==r->key) {active=1;break;}
        if(active==r->active) continue;
        Delivery *d=calloc(1,sizeof *d);if(!d) rt_fatal("SKSE delivery allocation failed");
        d->r=*r;handle_ref(c,r->handle,1);d->r.name=strdup(r->name);d->key=r->key;d->opened=active;
        d->event=strdup(r->kind==0 ? active?"OnMenuOpen":"OnMenuClose" : active?"OnKeyDown":"OnKeyUp");
        d->next=deliveries;deliveries=d;r->active=active;
        if(r->kind==1) {d->r.down_tick=r->down_tick;if(active) r->down_tick=SDL_GetTicks();}
    }
    pthread_mutex_unlock(&event_lock);
    while(deliveries) {
        Delivery *d=deliveries;deliveries=d->next;
        uint32_t values=gmalloc(16);memset(GPTR(values),0,16);
        unsigned count=1;
        if(d->r.kind==0) vm_string(c,values,d->r.name);
        else {
            M32(values)=3;M32(values+4)=d->key;
            if(!d->opened) {count=2;M32(values+8)=4;M32(values+12)=float_bits((SDL_GetTicks()-d->r.down_tick)/1000.f);}
        }
        send_event(c,&d->r,d->event,count,values);
        vm_clear(c,values);vm_clear(c,values+8);gfree(values);handle_ref(c,d->r.handle,0);free(d->r.name);free(d->event);free(d);
    }
}
void skse_ui_pump(void) {
    CPU *c=rt_current_cpu();if(!c) return;
    skse_input_install(c);flush_ui(c);pump_events(c);
}
uint32_t skse_ini_setting(CPU *c,const char *name) {
    uint32_t memory=gmalloc(4),key=gstrdup(name),args[]={key,memory},result=0;
    for(unsigned i=0;i<2;i++) {
        uint32_t list=M32(i?0x1B8FAF8:0x1B97F14);M32(memory)=0;
        /* The guest bool return occupies AL; upper EAX bits are unspecified.
         * A failed lookup in Skyrim.ini must continue into SkyrimPrefs.ini. */
        if(list && (skse_member(c,0x5263E0,list,2,args)&0xff) && M32(memory)) {result=M32(memory);break;}
    }
    gfree(key);gfree(memory);return result;
}
int skse_system_run(CPU *c,const SkseNative *fn,uint32_t base,const uint32_t *args,uint32_t registry,uint32_t result) {
    unsigned op=fn->operation&0x7fff;
    vm_clear(c,result);M32(result)=fn->result;M32(result+4)=0;
    if(op==S_UI_OPEN) {M32(result+4)=!!skse_menu_view(str(args[0]));return 1;}
    if(op==S_UI_TEXT) {uint32_t manager=M32(0x12E7458);M32(result+4)=manager&&M8(manager+0x98);return 1;}
    if(op>=S_UI_GBOOL && op<=S_UI_GSTRING) {
        uint32_t view=skse_menu_view(str(args[0]));
        if(!view) return 1;
        uint32_t value=gmalloc(16);memset(GPTR(value),0,16);
        skse_gfx_read(c,view,str(args[1]),value);
        unsigned type=M32(value+4)&0x8f;
        if(op==S_UI_GSTRING && type==4) {
            uint32_t text=M32(value+8);if((M32(value+4)&64)&&text) text=M32(text);
            vm_string(c,result,str(text));
        } else if(type==3 || type==2) {
            double number=type==3 ? MF64(value+8) : !!M8(value+8);
            M32(result+4)=op==S_UI_GFLOAT ? float_bits((float)number) : op==S_UI_GBOOL ? number!=0 : (int32_t)number;
        }
        skse_gfx_clean(c,value);gfree(value);return 1;
    }
    if(op>=S_UI_SBOOL && op<=S_UI_ASTRING) {enqueue(c,fn,args);return 1;}
    if(op>=S_REG_MENU && op<=S_UNREG_MODS) {change_registration(c,op,registry,base,args);return 1;}
    if(op==S_SEND_MOD) {
        skse_send_mod_event(c,str(args[0]),str(args[1]),as_float(args[2]),base);return 1;
    }
    if(op==S_GAME_PAD) {extern int input_desktop_pad_active(void);M32(result+4)=input_desktop_pad_active();return 1;}
    if(op==S_INPUT_CONTROL) {
        uint32_t manager=M32(0x12E7458),device=args[1],key=args[0];
        if(manager && device<3) {
            uint32_t ctx=M32(manager+0x34);
            if(ctx) {
                uint32_t map=ctx+device*12,count=M32(map+8),data=M32(map);
                if(data && count<4096) for(unsigned i=0;i<count;i++) if(M32(data+i*16+4)==key) {vm_string(c,result,str(M32(data+i*16)));break;}
            }
        }
        return 1;
    }
    if(op>=S_INI_FLOAT && op<=S_SET_INI_FLOAT) {
        uint32_t setting=skse_ini_setting(c,str(args[0]));if(!setting) return 1;
        const char *name=str(M32(setting+8));
        if(op==S_SET_INI_FLOAT) {if(name[0]=='f') M32(setting+4)=args[1];return 1;}
        double number=name[0]=='f' ? MF32(setting+4) : name[0]=='b' ? !!M8(setting+4) : (int32_t)M32(setting+4);
        M32(result+4)=op==S_INI_FLOAT ? float_bits((float)number) : (uint32_t)(int32_t)number;return 1;
    }
    if(op==1300) {uint32_t form=resolve_form(c,base);M32(result+4)=form?M8(form+0x12):0;return 1;}
    LOG("SKSE system operation not implemented: %s.%s",fn->class_name,fn->name);return 0;
}
#endif
