#ifndef CFORGE_POOL_H
#define CFORGE_POOL_H

#include <sqlite3.h>

typedef struct DbConn {
    sqlite3 *db;
    sqlite3_stmt *insert;
    sqlite3_stmt *get;
    sqlite3_stmt *del;
    int in_use;
} DbConn;

int cforge_pool_open(const char *path, int n);
void cforge_pool_close(void);
DbConn *cforge_pool_checkout(void);
void cforge_pool_release(DbConn *c);

#endif
