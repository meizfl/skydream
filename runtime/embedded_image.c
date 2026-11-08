/* SPDX-License-Identifier: GPL-3.0-only
 * Initialize compatibility memory from link-time data, without opening a PE. */
#include "rt.h"
#include "embedded_image.h"
#include <string.h>
void rt_load_embedded_game(void) {
#ifdef SKYDREAM_EMBEDDED_IMAGE
    const SkydreamImage *i=&g_skyrim_image;
    extern uint32_t g_entry,g_image_size;
    for(uint32_t n=0;n<i->segment_count;n++) {
        const SkydreamImageSegment *s=&i->segments[n];
        memcpy(MEM+s->address,g_skyrim_image_blob+s->offset,s->size);
    }
    for(uint32_t n=0;n<i->import_count;n++) {
        const SkydreamImageImport *p=&i->imports[n];
        M32(p->address)=rt_import(p->dll,p->name);
    }
    extern void rt_module_add(const char *,uint32_t);
    rt_module_add(GAME_EXE,i->base);
    g_entry=i->entry;g_image_size=i->size;
    if(i->tls_index) {
        extern void rt_set_static_tls(uint32_t,uint32_t,uint32_t,uint32_t);
        rt_set_static_tls(i->tls_start,i->tls_end,i->tls_index,i->tls_zero);
        if(i->tls_callbacks&&M32(i->tls_callbacks))LOG("warning: TLS callbacks not run");
    }
    LOG("embedded Skyrim: base %08x entry %08x image %08x, %u imports; no external PE images",i->base,i->entry,i->size,i->import_count);
#else
    rt_fatal("embedded game data was not linked into this build");
#endif
}
