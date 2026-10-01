#ifndef CFORGE_INTERNAL_H
#define CFORGE_INTERNAL_H

#include "cforge_rt.h"

#include <stddef.h>
#include <stdint.h>

enum {
    CFORGE_MAX_CONNS_DEFAULT = 128,
    CFORGE_MAX_HEADER = 8192,
    CFORGE_MAX_BODY = 65536,
    CFORGE_MAX_NAME = 256,
    CFORGE_ARENA = 4096,
    CFORGE_OUT_CAP = 16384,
    CFORGE_MAX_ROUTES = 64,
    CFORGE_IDLE_MS_DEFAULT = 30000
};

typedef struct Arena {
    uint8_t *base;
    size_t cap;
    size_t off;
} Arena;

struct Ctx {
    Arena *arena;
    Slice path;
    Slice body;
    Slice params[4];
    int nparams;
    int close_after;
    int responded;
    int method;
    uint8_t *out;
    size_t *out_len;
    size_t out_cap;
};

void *cforge_arena_alloc(Arena *arena, size_t n);
void cforge_arena_reset(Arena *arena);

int cforge_db_open(const char *path);
void cforge_db_close(void);
void cforge_db_release(void);
uint64_t cforge_db_errors(void);

int cforge_queue(Ctx *ctx, int status, const char *ctype, const void *body, size_t len, const char *extra);

void cforge_log_set_level(int level);
void cforge_logf(int level, const char *fmt, ...);

#endif
