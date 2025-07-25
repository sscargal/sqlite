#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>

/*
 * Example Usage:
 * ./pmemtest -f /mnt/pmem/pmemtest.db      # uses mmap
 * ./pmemtest -f /mnt/pmem/pmemtest.db -m   # disables mmap
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
    int opt;

    while ((opt = getopt(argc, argv, "f:m")) != -1) {
        switch (opt) {
            case 'f':
                db_path = optarg;
                break;
            case 'm':
                use_mmap = 0;
                break;
            default:
                fprintf(stderr, "Usage: %s -f <db_path> [-m] (disable mmap)\n", argv[0]);
                return 1;
        }
    }

    if (!db_path) {
        fprintf(stderr, "Usage: %s -f <db_path> [-m] (disable mmap)\n", argv[0]);
        return 1;
    }

    printf("Registering log callback...\n");
    rc = sqlite3_config(SQLITE_CONFIG_LOG, log_callback, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "sqlite3_config failed: %d\n", rc);
        return 1;
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
        rc = sqlite3_exec(db, "PRAGMA mmap_size=1073741824;", NULL, NULL, NULL); // 1GB
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

    // Generate random data for insertion
    char first_name[32], last_name[32], email[64], phone[32], address[128];
    snprintf(first_name, sizeof(first_name), "First%d", rand() % 10000);
    snprintf(last_name, sizeof(last_name), "Last%d", rand() % 10000);
    snprintf(email, sizeof(email), "user%d@example.com", rand() % 100000);
    snprintf(phone, sizeof(phone), "555-%04d", rand() % 10000);
    snprintf(address, sizeof(address), "%d Main St", rand() % 10000);

    // Insert random user
    sqlite3_stmt *insert_stmt = NULL;
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO users (first_name, last_name, email, phone, address) VALUES (?, ?, ?, ?, ?);",
        -1, &insert_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(insert_stmt, 1, first_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 2, last_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 3, email, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 4, phone, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 5, address, -1, SQLITE_TRANSIENT);
        sqlite3_step(insert_stmt);
        sqlite3_finalize(insert_stmt);
    } else {
        fprintf(stderr, "Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
    }

    // Force IO to trigger mmap mapping by reading from users table
    printf("Forcing IO to trigger mmap mapping (SELECT on users)...\n");
    sqlite3_stmt *io_stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT * FROM users LIMIT 1;", -1, &io_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_step(io_stmt); // ignore result, just force access
        sqlite3_finalize(io_stmt);
    } else {
        fprintf(stderr, "Failed to prepare IO statement: %s\n", sqlite3_errmsg(db));
    }

    printf("Preparing PRAGMA pmem_status...\n");
    rc = sqlite3_prepare_v2(db, "PRAGMA pmem_status;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    printf("Stepping through results...\n");
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
    printf("Done.\n");
    return 0;
}