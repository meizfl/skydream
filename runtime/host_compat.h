/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "host.h"
#ifdef _WIN32
/* Include CRT declarations before redirects; never include OS windows.h here. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <direct.h>
#include <io.h>
#include <time.h>
#define localtime_r(t, out) (localtime_s((out), (t)) ? NULL : (out))
#define gmtime_r(t, out) (gmtime_s((out), (t)) ? NULL : (out))
#define timegm _mkgmtime64
#define mkdir rt_host_mkdir
#define setenv rt_host_setenv
#define realpath rt_host_realpath
#ifndef __cplusplus
#define pread rt_host_pread
#define pwrite rt_host_pwrite
#define read rt_host_read
#define write rt_host_write
#define lseek rt_host_lseek
#define open(path, flags, ...) _open(path, (flags) | _O_BINARY, ##__VA_ARGS__)
#endif
#ifndef X_OK
#define X_OK 0
#endif
#endif
