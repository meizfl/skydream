/* SPDX-License-Identifier: GPL-3.0-only
 * Standalone package diagnostic: no TESV code or private inputs. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef main
#undef main
#endif
#include "host.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include "vkd3d_shader.h"
#include <stdio.h>
#include <string.h>
static void *load(const char *base, const char *name) {
    char path[8192];
    snprintf(path,sizeof path,"%s/%s",base,name);
    fprintf(stderr,"dependency-smoke: loading %s\n",name); fflush(stderr);
    void *lib=SDL_LoadObject(path);
    if(!lib) fprintf(stderr,"dependency-smoke: %s\n",SDL_GetError());
    return lib;
}
int main(void) {
    fprintf(stderr,"dependency-smoke: entered main, FFmpeg %u/%u/%u/%u/%u\n",avcodec_version(),avformat_version(),avutil_version(),swresample_version(),swscale_version());
    char *base=SDL_GetBasePath();
#ifdef __APPLE__
    const char *shader="lib/skyrim-native/libvkd3d-shader.1.dylib", *vulkan="lib/skyrim-native/libvulkan.1.dylib";
#else
    const char *shader="libvkd3d-shader-1.dll", *vulkan="vulkan-1.dll";
#endif
    void *lib=load(base,shader);
    if(!lib) return 1;
    PFN_vkd3d_shader_compile compile=(PFN_vkd3d_shader_compile)SDL_LoadFunction(lib,"vkd3d_shader_compile");
    struct vkd3d_shader_hlsl_source_info hlsl={0};
    hlsl.type=VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO;hlsl.entry_point="main";hlsl.profile="vs_3_0";
    const char *source="float4 main(float4 p:POSITION):POSITION{return p;}";
    struct vkd3d_shader_compile_info info={0};
    info.type=VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;info.next=&hlsl;
    info.source.code=source;info.source.size=strlen(source);
    info.source_type=VKD3D_SHADER_SOURCE_HLSL;info.target_type=VKD3D_SHADER_TARGET_D3D_BYTECODE;
    struct vkd3d_shader_code code={0};char *messages=NULL;
    fprintf(stderr,"dependency-smoke: compiling HLSL\n");fflush(stderr);
    if(!compile || compile(&info,&code,&messages)<0 || !code.size) return 2;
    fprintf(stderr,"dependency-smoke: HLSL compiled\n");fflush(stderr);
    if(!load(base,vulkan)) return 3;
#ifdef __APPLE__
    if(!load(base,"lib/skyrim-native/libMoltenVK.dylib")) return 4;
#endif
    if(rt_host_bundle_audit()) return 5;
    fprintf(stderr,"dependency-smoke: PASS\n");return 0;
}
