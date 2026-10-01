#include "internal.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pool.h"

static DbConn *g_active;
static uint64_t g_db_errors;

static void db_fail(DbConn *c, const char *what) {
    g_db_errors++;
    fprintf(stderr, "ERROR db %s: %s\n", what, c && c->db ? sqlite3_errmsg(c->db) : "no db");
}

uint64_t cforge_db_errors(void) {
    return g_db_errors;
}

void cforge_db_release(void) {
    if (g_active) {
        cforge_pool_release(g_active);
        g_active = NULL;
    }
}

int cforge_db_open(const char *path) {
    const char *nenv = getenv("CFORGE_DB_POOL");
    int n = 4;
    if (nenv && nenv[0]) {
        n = atoi(nenv);
        if (n < 1) {
            n = 1;
        }
        if (n > 8) {
            n = 8;
        }
    }
    return cforge_pool_open(path, n);
}

void cforge_db_close(void) {
    cforge_db_release();
    cforge_pool_close();
}

static int checkout(void) {
    if (g_active) {
        return 0;
    }
    g_active = cforge_pool_checkout();
    return g_active ? 0 : -1;
}

int32_t db_insert_user(Ctx *ctx, Slice name, uint32_t age, uint64_t *out_id) {
    (void)ctx;
    if (checkout() != 0 || name.len == 0 || name.len > CFORGE_MAX_NAME || !name.ptr) {
        return -1;
    }
    sqlite3_stmt *ins = g_active->insert;
    sqlite3_reset(ins);
    sqlite3_clear_bindings(ins);
    if (sqlite3_bind_text(ins, 1, (const char *)name.ptr, (int)name.len, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(ins, 2, (sqlite3_int64)age) != SQLITE_OK) {
        db_fail(g_active, "bind insert");
        return -1;
    }
    int step = sqlite3_step(ins);
    if (step != SQLITE_DONE) {
        db_fail(g_active, "insert");
        sqlite3_reset(ins);
        return -1;
    }
    *out_id = (uint64_t)sqlite3_last_insert_rowid(g_active->db);
    sqlite3_reset(ins);
    return 0;
}

int32_t db_get_user(Ctx *ctx, uint64_t id, uint64_t *out_id, Slice *name, uint32_t *age) {
    if (checkout() != 0) {
        return -1;
    }
    sqlite3_stmt *q = g_active->get;
    sqlite3_reset(q);
    sqlite3_clear_bindings(q);
    if (sqlite3_bind_int64(q, 1, (sqlite3_int64)id) != SQLITE_OK) {
        db_fail(g_active, "bind get");
        return -1;
    }
    int step = sqlite3_step(q);
    if (step == SQLITE_DONE) {
        sqlite3_reset(q);
        return 1;
    }
    if (step != SQLITE_ROW) {
        db_fail(g_active, "get");
        sqlite3_reset(q);
        return -1;
    }
    const void *text = sqlite3_column_text(q, 1);
    int nbytes = sqlite3_column_bytes(q, 1);
    if (!text || nbytes < 0) {
        sqlite3_reset(q);
        return -1;
    }
    uint8_t *dst = cforge_arena_alloc(ctx->arena, (size_t)nbytes == 0 ? 1 : (size_t)nbytes);
    if (!dst) {
        sqlite3_reset(q);
        return -1;
    }
    if (nbytes > 0) {
        memcpy(dst, text, (size_t)nbytes);
    }
    *out_id = (uint64_t)sqlite3_column_int64(q, 0);
    name->ptr = dst;
    name->len = (size_t)nbytes;
    sqlite3_int64 a = sqlite3_column_int64(q, 2);
    if (a < 0 || a > UINT32_MAX) {
        sqlite3_reset(q);
        return -1;
    }
    *age = (uint32_t)a;
    sqlite3_reset(q);
    return 0;
}

int32_t db_delete_user(Ctx *ctx, uint64_t id) {
    (void)ctx;
    if (checkout() != 0) {
        return -1;
    }
    sqlite3_stmt *d = g_active->del;
    sqlite3_reset(d);
    sqlite3_clear_bindings(d);
    if (sqlite3_bind_int64(d, 1, (sqlite3_int64)id) != SQLITE_OK) {
        db_fail(g_active, "bind delete");
        return -1;
    }
    int step = sqlite3_step(d);
    if (step != SQLITE_DONE) {
        db_fail(g_active, "delete");
        sqlite3_reset(d);
        return -1;
    }
    int changed = sqlite3_changes(g_active->db);
    sqlite3_reset(d);
    return changed == 0 ? 1 : 0;
}
