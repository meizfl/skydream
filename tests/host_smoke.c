/* SPDX-License-Identifier: GPL-3.0-only */
#include "host.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
static unsigned char *memory;
static void *worker(void *arg) {
    size_t i=(uintptr_t)arg;
    volatile uint32_t *p=(void *)(memory+0x200000+i*0x10000);
    *p=(uint32_t)(0x12340000+i);return NULL;
}
int main(void) {
    char base[4096];assert(!rt_host_base_path(base,sizeof base));assert(base[0]);
    assert(rt_host_cpu_count()>0);assert(rt_host_thread_id()!=0);
    assert(rt_host_match("*.EsS","Save.ess"));assert(rt_host_match("a?c*","ABCdef"));
    assert(!rt_host_match("a?c","abdc"));
    memory=rt_host_reserve_guest((size_t)1<<32);assert(memory);
    volatile uint32_t *low=(void *)(memory+0x10000),*high=(void *)(memory+0xfffff000u);
    assert(*low==0 && *high==0);*low=17;*high=23;assert(*low==17&&*high==23);
    rt_host_discard((void *)low,4096);assert(*low==0 && *high==23);
    pthread_t threads[16];
    for(uintptr_t i=0;i<16;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));
    for(unsigned i=0;i<16;i++){assert(!pthread_join(threads[i],NULL));assert(*(uint32_t *)(memory+0x200000+i*0x10000)==0x12340000+i);}
    uint64_t available,total;assert(!rt_host_disk_space(base,&available,&total));assert(total>=available);
    puts("PASS: native paths, disk space, sparse 4-GiB memory, zeroed discard, 16 threads");
}
