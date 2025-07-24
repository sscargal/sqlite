/*
** 2025-07-24
** Unit test for PRAGMA pmem_status (persistent memory VFS integration)
**
** This test opens a database, runs PRAGMA pmem_status, and checks the result.
** It prints the result to stdout and returns 0 for success, 1 for failure.
*/
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  sqlite3 *db = 0;
  sqlite3_stmt *stmt = 0;
  int rc, pmem = -1;

  rc = sqlite3_open("pmemtest.db", &db);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Cannot open db: %s\n", sqlite3_errmsg(db));
    return 1;
  }

  rc = sqlite3_prepare_v2(db, "PRAGMA pmem_status;", -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare PRAGMA: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    pmem = sqlite3_column_int(stmt, 0);
    printf("pmem_status=%d\n", pmem);
    if (pmem != 0 && pmem != 1) {
      fprintf(stderr, "pmem_status returned unexpected value: %d\n", pmem);
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return 1;
    }
  } else {
    fprintf(stderr, "No row returned by PRAGMA pmem_status\n");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  printf("pmem_status test PASSED\n");
  return 0;
}
