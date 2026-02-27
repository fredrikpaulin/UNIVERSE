/*
 * arena.h — Simple bump allocator for per-tick scratch memory
 */
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t   capacity;
    size_t   used;
} arena_t;

/* Create arena with given capacity */
int    arena_init(arena_t *a, size_t capacity);

/* Allocate n bytes (8-byte aligned). Returns NULL if full. */
void  *arena_alloc(arena_t *a, size_t n);

/* Reset arena (free all allocations, keep buffer) */
void   arena_reset(arena_t *a);

/* Free the underlying buffer */
void   arena_destroy(arena_t *a);

#endif
