#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sqlite3.h>

/*
 * Example Usage:
 * ./pmemtest -f /mnt/pmem/pmemtest.db      # uses mmap on a pmem device
 * ./pmemtest -f /mnt/pmem/pmemtest.db -m   # disables mmap on a pmem device
 * ./pmemtest -f /db/pmemtest.db -m         # disables mmap on a block device (ie: NVMe)
 */

void log_callback(void *pArg, int iErrCode, const char *zMsg) {
    fprintf(stderr, "SQLite LOG [%d]: %s\n", iErrCode, zMsg);
}


int main(int argc, char *argv[]) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int saw_row = 0;
    char *db_path = NULL;
    int use_mmap = 1; // default: use mmap
    size_t max_db_size = 1073741824; // default 1GB
    int row_count = 100000; // default 100k rows
    int debug_mode = 0;
    int opt;

    while ((opt = getopt(argc, argv, "f:ms:r:d")) != -1) {
        switch (opt) {
            case 'f':
                db_path = optarg;
                break;
            case 'm':
                use_mmap = 0;
                break;
            case 's':
                max_db_size = strtoull(optarg, NULL, 10);
                break;
            case 'r':
                row_count = atoi(optarg);
                break;
            case 'd':
                debug_mode = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s -f <db_path> [-m] [-s <max_db_size>] [-r <row_count>] [--debug]\n", argv[0]);
                return 1;
        }
    }

    if (!db_path) {
        fprintf(stderr, "Usage: %s -f <db_path> [-m] [-s <max_db_size>] [-r <row_count>] [--debug]\n", argv[0]);
        return 1;
    }

    if (debug_mode) {
        printf("Registering log callback...\n");
        rc = sqlite3_config(SQLITE_CONFIG_LOG, log_callback, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "sqlite3_config failed: %d\n", rc);
            return 1;
        }
    }

    if (!use_mmap) {
        printf("Disabling memory-mapped I/O via PRAGMA mmap_size=0...\n");
    }

    printf("Opening database: %s\n", db_path);
    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    if (!use_mmap) {
        rc = sqlite3_exec(db, "PRAGMA mmap_size=0;", NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to disable mmap: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return 1;
        }
        // Verify mmap_size
        sqlite3_stmt *mmap_stmt = NULL;
        rc = sqlite3_prepare_v2(db, "PRAGMA mmap_size;", -1, &mmap_stmt, NULL);
        if (rc == SQLITE_OK && sqlite3_step(mmap_stmt) == SQLITE_ROW) {
            printf("Verified mmap_size: %s\n", sqlite3_column_text(mmap_stmt, 0));
        } else {
            fprintf(stderr, "Failed to verify mmap_size: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(mmap_stmt);
    } else {
        char pragma_sql[128];
        snprintf(pragma_sql, sizeof(pragma_sql), "PRAGMA mmap_size=%zu;", max_db_size);
        rc = sqlite3_exec(db, pragma_sql, NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to set mmap_size: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return 1;
        }
        // Verify mmap_size
        sqlite3_stmt *mmap_stmt = NULL;
        rc = sqlite3_prepare_v2(db, "PRAGMA mmap_size;", -1, &mmap_stmt, NULL);
        if (rc == SQLITE_OK && sqlite3_step(mmap_stmt) == SQLITE_ROW) {
            printf("Verified mmap_size: %s\n", sqlite3_column_text(mmap_stmt, 0));
        } else {
            fprintf(stderr, "Failed to verify mmap_size: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(mmap_stmt);
    }

    // Drop and recreate 'users' table to ensure correct schema
    printf("Dropping and recreating 'users' table to ensure correct schema...\n");
    rc = sqlite3_exec(db, "DROP TABLE IF EXISTS users;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to drop users table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    rc = sqlite3_exec(db,
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "first_name TEXT,"
        "last_name TEXT,"
        "email TEXT,"
        "phone TEXT,"
        "address TEXT);",
        NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create users table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // Insert random users and time the operation
    printf("Inserting %d random users...\n", row_count);
    sqlite3_stmt *insert_stmt = NULL;
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO users (first_name, last_name, email, phone, address) VALUES (?, ?, ?, ?, ?);",
        -1, &insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    struct timespec insert_start, insert_end;
    clock_gettime(CLOCK_MONOTONIC, &insert_start);
    for (int i = 0; i < row_count; ++i) {
        char first_name[32], last_name[32], email[64], phone[32], address[128];
        snprintf(first_name, sizeof(first_name), "First%d", rand() % 10000);
        snprintf(last_name, sizeof(last_name), "Last%d", rand() % 10000);
        snprintf(email, sizeof(email), "user%d@example.com", rand() % 100000);
        snprintf(phone, sizeof(phone), "555-%04d", rand() % 10000);
        snprintf(address, sizeof(address), "%d Main St", rand() % 10000);
        sqlite3_bind_text(insert_stmt, 1, first_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 2, last_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 3, email, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 4, phone, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 5, address, -1, SQLITE_TRANSIENT);
        sqlite3_step(insert_stmt);
        sqlite3_reset(insert_stmt);
    }
    clock_gettime(CLOCK_MONOTONIC, &insert_end);
    sqlite3_finalize(insert_stmt);
    double insert_time = (insert_end.tv_sec - insert_start.tv_sec) + (insert_end.tv_nsec - insert_start.tv_nsec) / 1e9;
    double insert_iops = row_count / (insert_time > 0 ? insert_time : 1);
    printf("Insert phase completed in %.4f sec, IOPS: %.2f\n", insert_time, insert_iops);

    // SELECT all users and time the operation
    printf("Selecting all users to measure read performance...\n");
    sqlite3_stmt *select_stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT * FROM users;", -1, &select_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare select statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    struct timespec select_start, select_end;
    clock_gettime(CLOCK_MONOTONIC, &select_start);
    int select_rows = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        select_rows++;
    }
    clock_gettime(CLOCK_MONOTONIC, &select_end);
    sqlite3_finalize(select_stmt);
    double select_time = (select_end.tv_sec - select_start.tv_sec) + (select_end.tv_nsec - select_start.tv_nsec) / 1e9;
    double select_iops = select_rows / (select_time > 0 ? select_time : 1);
    printf("Select phase completed in %.4f sec, IOPS: %.2f\n", select_time, select_iops);

    printf("Preparing PRAGMA pmem_status...\n");
    rc = sqlite3_prepare_v2(db, "PRAGMA pmem_status;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    printf("Stepping through results...\n");
    saw_row = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        saw_row = 1;
        int cols = sqlite3_column_count(stmt);
        for (int i = 0; i < cols; ++i) {
            const char *colname = sqlite3_column_name(stmt, i);
            const char *colval = (const char *)sqlite3_column_text(stmt, i);
            printf("%s = %s\n", colname, colval ? colval : "NULL");
        }
    }
    if (!saw_row) {
        printf("No rows returned by PRAGMA pmem_status.\n");
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite3_step() error: %d (%s)\n", rc, sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    printf("Benchmark complete.\n");
    printf("Summary: DB Path: %s, mmap: %s, DB Size: %zu, Rows: %d\n", db_path, use_mmap ? "enabled" : "disabled", max_db_size, row_count);
    printf("Insert: %.2f sec, %.2f IOPS\n", insert_time, insert_iops);
    printf("Select: %.2f sec, %.2f IOPS\n", select_time, select_iops);
    return 0;
}