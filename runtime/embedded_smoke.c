/* SPDX-License-Identifier: GPL-3.0-only */
#include "rt.h"
#include "embedded_image.h"
#include "rt_table.h"
#include <string.h>
#include <SDL2/SDL.h>
#if defined(SKYDREAM_EMBEDDED_IMAGE) && defined(GAME_SKYRIM)
/* Execute the retained recompiled implementation, not its native replacement. */
extern void f_F520F0_guest(CPU *c); /* strchr */
int skydream_embedded_smoke(void) {
    MEM = rt_host_reserve_guest(1ull << 32);
    if (!MEM) return 10;
    rt_init_shims();
    CPU *c = rt_new_cpu();
    rt_thread_init(c);
    rt_load_embedded_game();
    extern unsigned rt_module_count(unsigned);
    if (!g_nmodules || !rt_module_count(0) || !g_skyrim_image.import_count) return 11;
    for (unsigned n = 0; n < g_skyrim_image.segment_count; ++n) {
        const SkydreamImageSegment *s = &g_skyrim_image.segments[n];
        if (!s->size || (uint64_t)s->address + s->size > (1ull << 32)) return 12;
    }
    const uint32_t text = 0x03000000u;
    memcpy(MEM + text, "Skydream CI", 12);
    const unsigned chars[] = {'S', 'C', 'Z', 0};
    const uint32_t expected[] = {text, text + 9, 0, text + 11};
    for (unsigned n = 0; n < 4; ++n) {
        const uint32_t esp = c->r[4];
        c->r[4] -= 12;
        M32(c->r[4]) = 0xFEEDF00Du;
        M32(c->r[4] + 4) = text;
        M32(c->r[4] + 8) = chars[n];
        f_F520F0_guest(c);
        if (c->r[0] != expected[n] || c->r[4] != esp - 8) return 13;
        c->r[4] = esp;
    }
    puts("embedded-smoke: image, imports, tables and recompiled strchr passed");
    return 0;
}
int skydream_bundle_smoke(void) {
    fprintf(stderr, "bundle-smoke: starting HLSL test\n");
    extern int d3dx_shader_selftest(CPU *);
    if (!getenv("SKYDREAM_VKD3D_SHADER") || d3dx_shader_selftest(rt_current_cpu())) return 20;
    fprintf(stderr, "bundle-smoke: HLSL passed; loading Vulkan loader\n");
    const char *loader = getenv("SDL_VULKAN_LIBRARY");
    void *vk = loader ? SDL_LoadObject(loader) : NULL;
    if (!vk || !SDL_LoadFunction(vk, "vkGetInstanceProcAddr")) return 21;
#ifdef __APPLE__
    char base[4096], path[8192];
    if (rt_host_base_path(base, sizeof base)) return 22;
    snprintf(path, sizeof path, "%s/lib/skyrim-native/libMoltenVK.dylib", base);
    fprintf(stderr, "bundle-smoke: loader passed; loading MoltenVK\n");
    void *metal = SDL_LoadObject(path);
    if (!metal || !SDL_LoadFunction(metal, "vkGetInstanceProcAddr")) return 23;
#endif
    puts("bundle-smoke: native HLSL compiler and packaged Vulkan libraries passed");
    return 0;
}
#endif
