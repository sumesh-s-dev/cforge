#include "pool.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { POOL_CAP = 8 };

static DbConn g_pool[POOL_CAP];
static int g_pool_n;
static pthread_mutex_t g_pool_mu = PTHREAD_MUTEX_INITIALIZER;

static int prep_conn(DbConn *c) {
    if (sqlite3_prepare_v2(c->db, "INSERT INTO users(name, age) VALUES (?1, ?2)", -1, &c->insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(c->db, "SELECT id, name, age FROM users WHERE id = ?1", -1, &c->get, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(c->db, "DELETE FROM users WHERE id = ?1", -1, &c->del, NULL) != SQLITE_OK) {
        return -1;
    }
    return 0;
}

static int open_conn(DbConn *c, const char *path) {
    memset(c, 0, sizeof *c);
    if (sqlite3_open(path, &c->db) != SQLITE_OK) {
        return -1;
    }
    sqlite3_busy_timeout(c->db, 1000);
    char *err = NULL;
    const char *boot =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "age INTEGER NOT NULL"
        ");";
    if (sqlite3_exec(c->db, boot, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "ERROR pool schema: %s\n", err ? err : sqlite3_errmsg(c->db));
        sqlite3_free(err);
        return -1;
    }
    return prep_conn(c);
}

int cforge_pool_open(const char *path, int n) {
    if (n < 1) {
        n = 1;
    }
    if (n > POOL_CAP) {
        n = POOL_CAP;
    }
    g_pool_n = n;
    for (int i = 0; i < n; i++) {
        if (open_conn(&g_pool[i], path) != 0) {
            cforge_pool_close();
            return -1;
        }
    }
    return 0;
}

void cforge_pool_close(void) {
    for (int i = 0; i < g_pool_n; i++) {
        DbConn *c = &g_pool[i];
        if (c->insert) {
            sqlite3_finalize(c->insert);
            c->insert = NULL;
        }
        if (c->get) {
            sqlite3_finalize(c->get);
            c->get = NULL;
        }
        if (c->del) {
            sqlite3_finalize(c->del);
            c->del = NULL;
        }
        if (c->db) {
            sqlite3_close(c->db);
            c->db = NULL;
        }
        c->in_use = 0;
    }
    g_pool_n = 0;
}

DbConn *cforge_pool_checkout(void) {
    pthread_mutex_lock(&g_pool_mu);
    for (int i = 0; i < g_pool_n; i++) {
        if (!g_pool[i].in_use) {
            g_pool[i].in_use = 1;
            pthread_mutex_unlock(&g_pool_mu);
            return &g_pool[i];
        }
    }
    pthread_mutex_unlock(&g_pool_mu);
    return NULL;
}

void cforge_pool_release(DbConn *c) {
    if (!c) {
        return;
    }
    if (c->insert) {
        sqlite3_reset(c->insert);
        sqlite3_clear_bindings(c->insert);
    }
    if (c->get) {
        sqlite3_reset(c->get);
        sqlite3_clear_bindings(c->get);
    }
    if (c->del) {
        sqlite3_reset(c->del);
        sqlite3_clear_bindings(c->del);
    }
    pthread_mutex_lock(&g_pool_mu);
    c->in_use = 0;
    pthread_mutex_unlock(&g_pool_mu);
}
