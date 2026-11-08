/* SPDX-License-Identifier: GPL-3.0-only
 * Import active plugins' UTF-16 translations through the game's resource
 * streams, so loose files and BSA archives have identical behaviour. */
#include "skse_runtime.h"
#include <string.h>
#include <strings.h>
#ifdef SKYDREAM_SKSE
static pthread_mutex_t translation_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t translated_loader;

static uint32_t key_hash(CPU *c,uint32_t key,uint32_t scratch) {
    uint32_t args[]={scratch,key};skse_member(c,0xA4A570,0,2,args);return M32(scratch);
}
static void put_translation(CPU *,uint32_t,uint32_t,uint32_t,uint32_t);
static void grow_table(CPU *c,uint32_t table,uint32_t scratch) {
    uint32_t old_count=M32(table+4),old_entries=M32(table+0x18);
    if(old_count>1u<<19) rt_fatal("SKSE translator: unreasonable table size");
    uint32_t count=old_count*2,args[]={count*12,0,0};
    uint32_t data=skse_member(c,0xA48D60,0x1B418B0,3,args);
    if(!data) rt_fatal("SKSE translator: cannot grow table");
    memset(GPTR(data),0,count*12);M32(table+4)=count;M32(table+8)=count;
    M32(table+0xc)=count;M32(table+0x18)=data;
    for(unsigned i=0;i<old_count;i++) {
        uint32_t e=old_entries+12*i;
        if(M32(e+8)) put_translation(c,table,M32(e),M32(e+4),scratch);
    }
    uint32_t free_args[]={old_entries,0};skse_member(c,0xA487B0,0x1B418B0,2,free_args);
}
static void put_translation(CPU *c,uint32_t table,uint32_t key,uint32_t text,uint32_t scratch) {
    uint32_t count=M32(table+4),entries=M32(table+0x18),end=M32(table+0x10);
    if(!count || !entries || (count&(count-1))) rt_fatal("SKSE translator: invalid hash table");
    uint32_t slot=entries+12*(key_hash(c,key,scratch)&(count-1));
    if(!M32(slot+8)) {
        M32(slot)=key;M32(slot+4)=text;M32(slot+8)=end;M32(table+8)--;return;
    }
    uint32_t current=slot;
    for(unsigned steps=0;steps<count;steps++) {
        if(M32(current)==key) {M32(current+4)=text;return;}
        uint32_t next=M32(current+8);if(next==end) break;current=next;
    }
    if(!M32(table+8)) {grow_table(c,table,scratch);put_translation(c,table,key,text,scratch);return;}
    uint32_t free_slot=0;
    for(unsigned i=0;i<count;i++) {
        uint32_t index=(M32(table+0xc)-1)&(count-1);M32(table+0xc)=index;
        uint32_t candidate=entries+index*12;
        if(!M32(candidate+8)) {free_slot=candidate;break;}
    }
    if(!free_slot) rt_fatal("SKSE translator: no free translation slot");
    uint32_t root=entries+12*(key_hash(c,M32(slot),scratch)&(count-1));
    if(root==slot) {
        M32(free_slot)=key;M32(free_slot+4)=text;M32(free_slot+8)=M32(slot+8);M32(slot+8)=free_slot;
    } else {
        unsigned steps=0;
        while(M32(root+8)!=slot && ++steps<count) root=M32(root+8);
        if(steps>=count) rt_fatal("SKSE translator: broken collision chain");
        memcpy(GPTR(free_slot),GPTR(slot),12);M32(root+8)=free_slot;
        M32(slot)=key;M32(slot+4)=text;M32(slot+8)=end;
    }
    M32(table+8)--;
}
static unsigned import_file(CPU *c,uint32_t translator,const char *path) {
    uint32_t stream=gmalloc(32),buffer=gmalloc(1024*1024+2),scratch=gmalloc(12);
    memset(GPTR(stream),0,32);memset(GPTR(scratch),0,12);
    uint32_t filename=gstrdup(path),ctor[]={filename,0,0};
    skse_member(c,0xAC6790,stream,3,ctor);gfree(filename);
    unsigned imported=0;
    if(skse_member(c,0xAC6400,stream,0,NULL)) {
        uint32_t read[]={buffer,1024*1024};
        unsigned bytes=skse_member(c,0xAC6410,stream,2,read);
        if(bytes>=2 && bytes<1024*1024 && M16(buffer)==0xfeff) {
            M16(buffer+bytes)=0;
            unsigned start=1,units=bytes/2;
            for(unsigned i=1;i<=units;i++) {
                if(i!=units && M16(buffer+i*2)!=10) continue;
                unsigned end=i;while(end>start && (M16(buffer+2*(end-1))==13 || M16(buffer+2*(end-1))==0)) end--;
                M16(buffer+2*end)=0;
                unsigned split=0;
                for(unsigned j=start;j<end;j++) if(M16(buffer+2*j)==9) split=j;
                if(M16(buffer+2*start)=='$' && split>start+1) {
                    M16(buffer+2*split)=0;
                    /* Each dictionary entry owns its interned references.
                     * Reusing a non-null output would release the previous entry. */
                    M32(scratch+4)=0;M32(scratch+8)=0;
                    uint32_t keyargs[]={scratch+4,buffer+2*start};
                    uint32_t textargs[]={scratch+8,buffer+2*(split+1)};
                    skse_member(c,0xA51800,0,2,keyargs);skse_member(c,0xA51800,0,2,textargs);
                    put_translation(c,translator+0x14,M32(scratch+4),M32(scratch+8),scratch);imported++;
                }
                start=i+1;
            }
        } else LOG("SKSE translations: invalid UTF-16 file %s (%u bytes)",path,bytes);
    }
    skse_member(c,0xAC69B0,stream,0,NULL);gfree(stream);gfree(buffer);gfree(scratch);
    if(imported) LOG("SKSE translations: imported %u entries from %s",imported,path);
    return imported;
}
void skse_import_translations(CPU *c) {
    uint32_t loader=M32(0x1B2E9B0);if(!loader) return;
    pthread_mutex_lock(&translation_lock);
    if(loader==translated_loader) {pthread_mutex_unlock(&translation_lock);return;}
    uint32_t bag=M32(loader+4),kind=3,translator=bag?skse_virtual(c,bag,3,1,&kind):0;
    if(!translator) {pthread_mutex_unlock(&translation_lock);return;}
    uint32_t setting=skse_ini_setting(c,"sLanguage:General");
    const char *language=setting?GSTR(M32(setting+4)):NULL;
    if(!language || !*language) language="ENGLISH";
    char listpath[1400];snprintf(listpath,sizeof listpath,"%s/Documents/Skyrim/Plugins.txt",g_save_dir);
    FILE *f=fopen(listpath,"rb");
    if(f) {
        char line[512];
        while(fgets(line,sizeof line,f)) {
            line[strcspn(line,"\r\n")]=0;
            char *name=line;while(*name==' '||*name=='\t'||*name=='*') name++;
            if(!*name || *name=='#') continue;
            char *ext=strrchr(name,'.');
            if(!ext || (strcasecmp(ext,".esp") && strcasecmp(ext,".esm"))) continue;
            *ext=0;
            if(strchr(name,'/') || strchr(name,'\\')) continue;
            char path[1024];snprintf(path,sizeof path,"Interface\\Translations\\%s_%s.txt",name,language);
            import_file(c,translator,path);
        }
        fclose(f);
    }
    translated_loader=loader;
    if(!__atomic_sub_fetch((uint32_t *)GPTR(translator+4),1,__ATOMIC_ACQ_REL)) {
        uint32_t flags=1;skse_virtual(c,translator,0,1,&flags);
    }
    pthread_mutex_unlock(&translation_lock);
}
#endif
