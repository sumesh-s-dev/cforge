#ifndef CFORGE_RT_H
#define CFORGE_RT_H

#include <stddef.h>
#include <stdint.h>

typedef struct Ctx Ctx;

typedef struct Slice {
    uint8_t *ptr;
    size_t len;
} Slice;

typedef int32_t (*CForgeFn)(Ctx *ctx);

Slice slice_empty(void);

int32_t ctx_param_u64(Ctx *ctx, int32_t index, uint64_t *out);
int32_t ctx_status(Ctx *ctx, int32_t status);
int32_t ctx_text(Ctx *ctx, int32_t status, Slice body);
int32_t ctx_json_user(Ctx *ctx, int32_t status, uint64_t id, Slice name, uint32_t age);
int32_t ctx_json_id(Ctx *ctx, int32_t status, uint64_t id);
int32_t ctx_problem(Ctx *ctx, int32_t status, Slice title, Slice detail);
int32_t ctx_metrics(Ctx *ctx);

int32_t json_parse_create(Ctx *ctx, Slice *name, uint32_t *age);

int32_t db_get_user(Ctx *ctx, uint64_t id, uint64_t *out_id, Slice *name, uint32_t *age);
int32_t db_insert_user(Ctx *ctx, Slice name, uint32_t age, uint64_t *out_id);
int32_t db_delete_user(Ctx *ctx, uint64_t id);

void app_get(Slice path, CForgeFn fn);
void app_post(Slice path, CForgeFn fn);
void app_delete(Slice path, CForgeFn fn);
int32_t app_listen(uint16_t port);

#endif
