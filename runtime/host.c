/* SPDX-License-Identifier: GPL-3.0-only
 * OS APIs are isolated from DXVK's synthetic Windows ABI headers. */
#include "host.h"
#include <SDL.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <limits.h>
static unsigned char *guest_base;
static size_t guest_size;
/* Reserve address space only; commit data pages on first access. A full 4-GiB
 * MEM_COMMIT allocation would consume 4 GiB of Windows commit charge at launch. */
static LONG CALLBACK guest_fault(EXCEPTION_POINTERS *exception) {
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        exception->ExceptionRecord->NumberParameters < 2 ||
        exception->ExceptionRecord->ExceptionInformation[0] == 8)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t p = exception->ExceptionRecord->ExceptionInformation[1];
    uintptr_t base = (uintptr_t)guest_base;
    if (!base || p < base + 0x10000 || p - base >= guest_size)
        return EXCEPTION_CONTINUE_SEARCH;
    SYSTEM_INFO info; GetSystemInfo(&info);
    void *page = (void *)(p & ~((uintptr_t)info.dwPageSize - 1));
    return VirtualAlloc(page, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
void *rt_host_reserve_guest(size_t bytes) {
    guest_size = bytes;
    guest_base = VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (!guest_base) return NULL;
    if (!AddVectoredExceptionHandler(1, guest_fault)) {
        VirtualFree(guest_base, 0, MEM_RELEASE); guest_base = NULL;
    }
    return guest_base;
}
void rt_host_discard(void *address, size_t bytes) {
    VirtualFree(address, bytes, MEM_DECOMMIT);
}
unsigned long rt_host_thread_id(void) { return GetCurrentThreadId(); }
int rt_host_disk_space(const char *path, uint64_t *available, uint64_t *total) {
    ULARGE_INTEGER a,t;
    if (!GetDiskFreeSpaceExA(path,&a,&t,NULL)) return -1;
    *available=a.QuadPart;*total=t.QuadPart;return 0;
}
static wchar_t *wide_path(const char *text) {
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
    if(!count){errno=EINVAL;return NULL;}
    wchar_t *wide=malloc((size_t)count*sizeof *wide);
    if(!wide){errno=ENOMEM;return NULL;}
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,wide,count);return wide;
}
int rt_host_mkdir(const char *path,int mode) {(void)mode;wchar_t *p=wide_path(path);if(!p)return -1;int result=_wmkdir(p);free(p);return result;}
int rt_host_setenv(const char *name,const char *value,int overwrite) {
    return !overwrite && getenv(name) ? 0 : _putenv_s(name,value);
}
char *rt_host_realpath(const char *path,char *result) {
    wchar_t *input=wide_path(path);if(!input)return NULL;
    wchar_t *full=_wfullpath(NULL,input,0);free(input);if(!full)return NULL;
    int bytes=WideCharToMultiByte(CP_UTF8,0,full,-1,NULL,0,NULL,NULL);
    if(!result)result=malloc((size_t)bytes);
    if(result)WideCharToMultiByte(CP_UTF8,0,full,-1,result,bytes,NULL,NULL);
    free(full);if(result)for(char *p=result;*p;p++)if(*p=='\\')*p='/';
    return result;
}

/* CRT descriptors are synchronous. Coordinate offset I/O with ordinary I/O
 * on the same descriptor, so guest OVERLAPPED reads never corrupt its position.
 * Hash collisions serialize only I/O, not gameplay or unrelated host workers. */
static SRWLOCK io_locks[64];
static SRWLOCK *io_lock(int fd) {return &io_locks[(unsigned)fd%64];}
static ssize_t position_io(int fd,void *data,size_t bytes,int64_t offset,int writing,int positioned) {
    if(bytes>INT_MAX){errno=EINVAL;return -1;}
    SRWLOCK *lock=io_lock(fd);AcquireSRWLockExclusive(lock);
    int64_t saved=positioned?_lseeki64(fd,0,SEEK_CUR):0;
    int result=-1;
    if(!positioned || (saved>=0 && _lseeki64(fd,offset,SEEK_SET)>=0))
        result=writing?_write(fd,data,(unsigned)bytes):_read(fd,data,(unsigned)bytes);
    int error=errno;
    if(positioned && saved>=0 && _lseeki64(fd,saved,SEEK_SET)<0) {result=-1;error=errno;}
    ReleaseSRWLockExclusive(lock);errno=error;return result;
}
ssize_t rt_host_pread(int fd,void *p,size_t n,int64_t at){return position_io(fd,p,n,at,0,1);}
ssize_t rt_host_pwrite(int fd,const void *p,size_t n,int64_t at){return position_io(fd,(void *)p,n,at,1,1);}
ssize_t rt_host_read(int fd,void *p,size_t n){return position_io(fd,p,n,0,0,0);}
ssize_t rt_host_write(int fd,const void *p,size_t n){return position_io(fd,(void *)p,n,0,1,0);}
int64_t rt_host_lseek(int fd,int64_t at,int origin){SRWLOCK *lock=io_lock(fd);AcquireSRWLockExclusive(lock);int64_t r=_lseeki64(fd,at,origin);ReleaseSRWLockExclusive(lock);return r;}
#else
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <pthread.h>
#if defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#endif
void *rt_host_reserve_guest(size_t bytes) {
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags|=MAP_NORESERVE;
#endif
    void *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,flags,-1,0);
    if(p==MAP_FAILED)return NULL;
    if(mprotect(p,0x10000,PROT_NONE)){munmap(p,bytes);return NULL;}
    return p;
}
void rt_host_discard(void *address,size_t bytes){
#ifdef __APPLE__
    /* Darwin MADV_DONTNEED does not promise zero-filled subsequent reads. */
    if (mmap(address,bytes,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED) abort();
#else
    madvise(address,bytes,MADV_DONTNEED);
#endif
}
unsigned long rt_host_thread_id(void){
#ifdef __APPLE__
    uint64_t id=0;pthread_threadid_np(NULL,&id);return (unsigned long)id;
#elif defined(__linux__)
    return (unsigned long)syscall(SYS_gettid);
#else
    return (unsigned long)(uintptr_t)pthread_self();
#endif
}
int rt_host_disk_space(const char *path,uint64_t *available,uint64_t *total){struct statvfs s;if(statvfs(path,&s))return -1;*available=(uint64_t)s.f_bavail*s.f_frsize;*total=(uint64_t)s.f_blocks*s.f_frsize;return 0;}
#endif
int rt_host_base_path(char *path,size_t capacity) {
    char *base=SDL_GetBasePath();if(!base)return -1;
    size_t length=strlen(base);if(length>=capacity){SDL_free(base);return -1;}
    memcpy(path,base,length+1);SDL_free(base);
    for(char *p=path;*p;p++)if(*p=='\\')*p='/';
    while(length>1 && path[length-1]=='/')path[--length]=0;
    return 0;
}

/* Guest file searches require case-insensitive DOS-style * and ? matching. */
int rt_host_match(const char *pattern, const char *name) {
    const char *star = NULL, *retry = NULL;
    while (*name) {
        if (*pattern == '*') { star = ++pattern; retry = name; }
        else if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*name)) {
            ++pattern; ++name;
        } else if (star) { pattern = star; name = ++retry; }
        else return 0;
    }
    while (*pattern == '*') ++pattern;
    return !*pattern;
}

int rt_host_cpu_count(void) { return SDL_GetCPUCount(); }

static void bundle_env_file(const char *name, const char *base, const char *relative) {
    char path[8192];
    if (SDL_getenv(name)) return;
    if (snprintf(path, sizeof path, "%s/%s", base, relative) >= (int)sizeof path) return;
    SDL_RWops *file = SDL_RWFromFile(path, "rb");
    if (file) {
        SDL_RWclose(file);
#ifdef _WIN32
        rt_host_setenv(name, path, 0);
#else
        setenv(name, path, 0);
#endif
    }
}
void rt_host_bundle_environment(void) {
    char base[4096];
    if (rt_host_base_path(base, sizeof base)) return;
#ifdef _WIN32
    bundle_env_file("SKYDREAM_VKD3D_SHADER", base, "libvkd3d-shader-1.dll");
    bundle_env_file("SDL_VULKAN_LIBRARY", base, "vulkan-1.dll");
#elif defined(__APPLE__)
    bundle_env_file("SKYDREAM_VKD3D_SHADER", base, "lib/skyrim-native/libvkd3d-shader.1.dylib");
    bundle_env_file("SDL_VULKAN_LIBRARY", base, "lib/skyrim-native/libvulkan.1.dylib");
    if (!SDL_getenv("VK_ICD_FILENAMES"))
        bundle_env_file("VK_DRIVER_FILES", base, "share/vulkan/icd.d/MoltenVK_icd.json");
#else
    bundle_env_file("SKYDREAM_VKD3D_SHADER", base, "lib/skyrim-native/libvkd3d-shader.so.1");
#endif
}

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
int rt_host_bundle_audit(void) {
#ifdef __APPLE__
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *name = _dyld_get_image_name(i);
        if (!name) continue;
        if (!strncmp(name, "/opt/homebrew/", 14) || !strncmp(name, "/usr/local/Cellar/", 18) || !strncmp(name, "/usr/local/opt/", 15)) {
            fprintf(stderr, "bundle-audit: external Homebrew image %s\n", name);
            return 1;
        }
        if (strncmp(name, "/System/", 8) && strncmp(name, "/usr/lib/", 9))
            fprintf(stderr, "bundle-image: %s\n", name);
    }
#endif
    return 0;
}
