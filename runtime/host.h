/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *rt_host_reserve_guest(size_t bytes);
void rt_host_discard(void *address, size_t bytes);
int rt_host_cpu_count(void);
unsigned long rt_host_thread_id(void);
int rt_host_base_path(char *path, size_t capacity);
int rt_host_match(const char *pattern, const char *name);
int rt_host_disk_space(const char *path, uint64_t *available, uint64_t *total);
#ifdef _WIN32
#include <sys/types.h>
int rt_host_mkdir(const char *path, int mode);
int rt_host_setenv(const char *name, const char *value, int overwrite);
char *rt_host_realpath(const char *path, char *result);
ssize_t rt_host_pread(int fd, void *data, size_t bytes, int64_t offset);
ssize_t rt_host_pwrite(int fd, const void *data, size_t bytes, int64_t offset);
ssize_t rt_host_read(int fd, void *data, size_t bytes);
ssize_t rt_host_write(int fd, const void *data, size_t bytes);
int64_t rt_host_lseek(int fd, int64_t offset, int origin);
#endif
/* Prefer optional runtime libraries shipped beside the executable. */
void rt_host_bundle_environment(void);
#ifdef __cplusplus
}
#endif
