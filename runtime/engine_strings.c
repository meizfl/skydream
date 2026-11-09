/* SPDX-License-Identifier: GPL-3.0-only
 * Contracts recovered from TESV 1.9.32 byte-string primitives. No guest CPU,
 * address truncation, mutable caches, locale state or per-call worker pool.
 * Host libc selects the architecture-appropriate optimized implementation. */
#include "engine_strings.h"
#include <string.h>

size_t skydream_string_length(const char *text) {
    return strlen(text);
}
const char *skydream_string_find(const char *text, int byte) {
    return strchr(text, (unsigned char)byte);
}
const void *skydream_bytes_find(const void *data, int byte, size_t bytes) {
    return bytes ? memchr(data, (unsigned char)byte, bytes) : NULL;
}
char *skydream_string_copy_padded(char *destination, const char *source,
                                  size_t bytes) {
    return bytes ? strncpy(destination, source, bytes) : destination;
}
