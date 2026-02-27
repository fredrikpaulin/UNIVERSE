/*
 * util.h — Common macros and helpers
 */
#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>

#define ARRAY_LEN(a)  (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) MIN(MAX(x, lo), hi)

/* Use GNU extension ##__VA_ARGS__ — works on GCC/Clang, swallows trailing comma */
#define LOG_INFO(...)  do { fprintf(stdout, "[INFO ] "); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#define LOG_WARN(...)  do { fprintf(stderr, "[WARN ] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define LOG_ERROR(...) do { fprintf(stderr, "[ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

#endif
