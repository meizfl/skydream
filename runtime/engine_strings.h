/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
/* Host pointers and full-width sizes. Reentrant: readers may share immutable
 * input; each writer must own its output. Copy requires nonoverlapping ranges.
 * As with libc, strings must be terminated within accessible storage. */
size_t skydream_string_length(const char *text);
const char *skydream_string_find(const char *text, int byte);
const void *skydream_bytes_find(const void *data, int byte, size_t bytes);
char *skydream_string_copy_padded(char *destination, const char *source,
                                  size_t bytes);
