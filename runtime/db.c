#include "internal.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static sqlite3 *g_db;
static sqlite3_stmt *g_insert;
static sqlite3_stmt *g_get;
static sqlite3_stmt *g_del;
static int g_checked_out;
static uint64_t g_db_errors;

static void db_fail(const char *what) {
    g_db_errors++;
    fprintf(stderr, "ERROR db %s: %s\n", what, g_db ? sqlite3_errmsg(g_db) : "no db");
}

uint64_t cforge_db_errors(void) {
    return g_db_errors;
}

void cforge_db_release(void) {
    g_checked_out = 0;
    if (g_insert) {
        sqlite3_reset(g_insert);
        sqlite3_clear_bindings(g_insert);
    }
    if (g_get) {
        sqlite3_reset(g_get);
        sqlite3_clear_bindings(g_get);
    }
    if (g_del) {
        sqlite3_reset(g_del);
        sqlite3_clear_bindings(g_del);
    }
}

int cforge_db_open(const char *path) {
    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "ERROR db open %s: %s\n", path, g_db ? sqlite3_errmsg(g_db) : "sqlite");
        return -1;
    }
    sqlite3_busy_timeout(g_db, 1000);
    char *err = NULL;
    const char *boot =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "age INTEGER NOT NULL"
        ");";
    if (sqlite3_exec(g_db, boot, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "ERROR db schema: %s\n", err ? err : sqlite3_errmsg(g_db));
        sqlite3_free(err);
        return -1;
    }
    if (sqlite3_prepare_v2(g_db, "INSERT INTO users(name, age) VALUES (?1, ?2)", -1, &g_insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(g_db, "SELECT id, name, age FROM users WHERE id = ?1", -1, &g_get, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(g_db, "DELETE FROM users WHERE id = ?1", -1, &g_del, NULL) != SQLITE_OK) {
        db_fail("prepare");
        return -1;
    }
    return 0;
}

void cforge_db_close(void) {
    if (g_insert) {
        sqlite3_finalize(g_insert);
        g_insert = NULL;
    }
    if (g_get) {
        sqlite3_finalize(g_get);
        g_get = NULL;
    }
    if (g_del) {
        sqlite3_finalize(g_del);
        g_del = NULL;
    }
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

static int checkout(void) {
    if (!g_db) {
        return -1;
    }
    g_checked_out = 1;
    return 0;
}

int32_t db_insert_user(Ctx *ctx, Slice name, uint32_t age, uint64_t *out_id) {
    (void)ctx;
    if (checkout() != 0 || name.len == 0 || name.len > CFORGE_MAX_NAME || !name.ptr) {
        return -1;
    }
    sqlite3_reset(g_insert);
    sqlite3_clear_bindings(g_insert);
    if (sqlite3_bind_text(g_insert, 1, (const char *)name.ptr, (int)name.len, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(g_insert, 2, (sqlite3_int64)age) != SQLITE_OK) {
        db_fail("bind insert");
        return -1;
    }
    int step = sqlite3_step(g_insert);
    if (step != SQLITE_DONE) {
        db_fail("insert");
        sqlite3_reset(g_insert);
        return -1;
    }
    *out_id = (uint64_t)sqlite3_last_insert_rowid(g_db);
    sqlite3_reset(g_insert);
    return 0;
}

int32_t db_get_user(Ctx *ctx, uint64_t id, uint64_t *out_id, Slice *name, uint32_t *age) {
    if (checkout() != 0) {
        return -1;
    }
    sqlite3_reset(g_get);
    sqlite3_clear_bindings(g_get);
    if (sqlite3_bind_int64(g_get, 1, (sqlite3_int64)id) != SQLITE_OK) {
        db_fail("bind get");
        return -1;
    }
    int step = sqlite3_step(g_get);
    if (step == SQLITE_DONE) {
        sqlite3_reset(g_get);
        return 1;
    }
    if (step != SQLITE_ROW) {
        db_fail("get");
        sqlite3_reset(g_get);
        return -1;
    }
    const void *text = sqlite3_column_text(g_get, 1);
    int nbytes = sqlite3_column_bytes(g_get, 1);
    if (!text || nbytes < 0) {
        sqlite3_reset(g_get);
        return -1;
    }
    uint8_t *dst = cforge_arena_alloc(ctx->arena, (size_t)nbytes == 0 ? 1 : (size_t)nbytes);
    if (!dst) {
        sqlite3_reset(g_get);
        return -1;
    }
    if (nbytes > 0) {
        memcpy(dst, text, (size_t)nbytes);
    }
    *out_id = (uint64_t)sqlite3_column_int64(g_get, 0);
    name->ptr = dst;
    name->len = (size_t)nbytes;
    sqlite3_int64 a = sqlite3_column_int64(g_get, 2);
    if (a < 0 || a > UINT32_MAX) {
        sqlite3_reset(g_get);
        return -1;
    }
    *age = (uint32_t)a;
    sqlite3_reset(g_get);
    return 0;
}

int32_t db_delete_user(Ctx *ctx, uint64_t id) {
    (void)ctx;
    if (checkout() != 0) {
        return -1;
    }
    sqlite3_reset(g_del);
    sqlite3_clear_bindings(g_del);
    if (sqlite3_bind_int64(g_del, 1, (sqlite3_int64)id) != SQLITE_OK) {
        db_fail("bind delete");
        return -1;
    }
    int step = sqlite3_step(g_del);
    if (step != SQLITE_DONE) {
        db_fail("delete");
        sqlite3_reset(g_del);
        return -1;
    }
    int changed = sqlite3_changes(g_db);
    sqlite3_reset(g_del);
    return changed == 0 ? 1 : 0;
}
