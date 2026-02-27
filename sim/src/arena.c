/*
 * arena.c — Bump allocator implementation
 */
#include "arena.h"
#include <stdlib.h>
#include <string.h>

int arena_init(arena_t *a, size_t capacity) {
    a->buf = malloc(capacity);
    if (!a->buf) return -1;
    a->capacity = capacity;
    a->used = 0;
    return 0;
}

void *arena_alloc(arena_t *a, size_t n) {
    /* Align to 8 bytes */
    size_t aligned = (n + 7) & ~(size_t)7;
    if (a->used + aligned > a->capacity) return NULL;
    void *ptr = a->buf + a->used;
    a->used += aligned;
    memset(ptr, 0, aligned);
    return ptr;
}

void arena_reset(arena_t *a) {
    a->used = 0;
}

void arena_destroy(arena_t *a) {
    free(a->buf);
    a->buf = NULL;
    a->capacity = 0;
    a->used = 0;
}
