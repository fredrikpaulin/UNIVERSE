/*
 * persist.c — SQLite persistence implementation
 */
#include "persist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS sectors ("
    "  x INT, y INT, z INT,"
    "  generated_tick INT,"
    "  data TEXT,"
    "  PRIMARY KEY (x, y, z)"
    ");"
    "CREATE TABLE IF NOT EXISTS systems ("
    "  id TEXT PRIMARY KEY,"
    "  sector_x INT, sector_y INT, sector_z INT,"
    "  data TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS probes ("
    "  id TEXT PRIMARY KEY,"
    "  parent_id TEXT,"
    "  generation INT,"
    "  data TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS events ("
    "  tick INT,"
    "  probe_id TEXT,"
    "  type TEXT,"
    "  data TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS messages ("
    "  id TEXT PRIMARY KEY,"
    "  sender_id TEXT,"
    "  receiver_id TEXT,"
    "  sent_tick INT,"
    "  arrival_tick INT,"
    "  content TEXT,"
    "  delivered INT DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS structures ("
    "  id TEXT PRIMARY KEY,"
    "  type TEXT,"
    "  system_id TEXT,"
    "  body_id TEXT,"
    "  builder_id TEXT,"
    "  data TEXT"
    ");";

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
    }
    return rc == SQLITE_OK ? 0 : -1;
}

int persist_open(persist_t *p, const char *path) {
    int rc = sqlite3_open_v2(path, &p->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(p->db));
        return -1;
    }
    /* Enable WAL mode for better concurrent read performance */
    exec_sql(p->db, "PRAGMA journal_mode=WAL;");
    return exec_sql(p->db, SCHEMA_SQL);
}

void persist_close(persist_t *p) {
    if (p->db) {
        sqlite3_close(p->db);
        p->db = NULL;
    }
}

static int upsert_meta(sqlite3 *db, const char *key, const char *value) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?);";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int read_meta(sqlite3 *db, const char *key, char *buf, size_t buflen) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT value FROM meta WHERE key = ?;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        if (val) {
            size_t len = strlen(val);
            if (len >= buflen) len = buflen - 1;
            memcpy(buf, val, len);
            buf[len] = '\0';
        }
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return -1;
}

int persist_save_meta(persist_t *p, const universe_t *u) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)u->seed);
    if (upsert_meta(p->db, "seed", buf) != 0) return -1;

    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)u->tick);
    if (upsert_meta(p->db, "tick", buf) != 0) return -1;

    snprintf(buf, sizeof(buf), "%u", u->generation_version);
    if (upsert_meta(p->db, "generation_version", buf) != 0) return -1;

    return 0;
}

int persist_load_meta(persist_t *p, universe_t *u) {
    char buf[64];

    if (read_meta(p->db, "seed", buf, sizeof(buf)) != 0) return -1;
    u->seed = strtoull(buf, NULL, 10);

    if (read_meta(p->db, "tick", buf, sizeof(buf)) != 0) return -1;
    u->tick = strtoull(buf, NULL, 10);

    if (read_meta(p->db, "generation_version", buf, sizeof(buf)) == 0)
        u->generation_version = (uint32_t)strtoul(buf, NULL, 10);

    return 0;
}

int persist_save_tick(persist_t *p, uint64_t tick) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)tick);
    return upsert_meta(p->db, "tick", buf);
}

/* ---- UID formatting ---- */

static void uid_to_str(probe_uid_t id, char *buf, size_t len) {
    snprintf(buf, len, "%016llx%016llx",
        (unsigned long long)id.hi, (unsigned long long)id.lo);
}

static probe_uid_t uid_from_str(const char *s) {
    probe_uid_t id = {0, 0};
    if (!s || strlen(s) < 32) return id;
    char hi_buf[17] = {0}, lo_buf[17] = {0};
    memcpy(hi_buf, s, 16);
    memcpy(lo_buf, s + 16, 16);
    id.hi = strtoull(hi_buf, NULL, 16);
    id.lo = strtoull(lo_buf, NULL, 16);
    return id;
}

/* ---- Sector / System persistence ---- */

/* Simple binary blob approach: we serialize system_t structs directly.
 * This is fast and avoids JSON overhead. The schema is versioned via
 * generation_version in the meta table, so we can migrate if the
 * struct layout changes. */

int persist_save_sector(persist_t *p, sector_coord_t coord, uint64_t tick,
                        const system_t *systems, int count) {
    /* Save sector record */
    {
        sqlite3_stmt *stmt;
        const char *sql = "INSERT OR REPLACE INTO sectors (x, y, z, generated_tick, data) "
                          "VALUES (?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(p->db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_int64(stmt, 1, coord.x);
        sqlite3_bind_int64(stmt, 2, coord.y);
        sqlite3_bind_int64(stmt, 3, coord.z);
        sqlite3_bind_int64(stmt, 4, (long long)tick);
        /* Store system count as sector data */
        char count_str[16];
        snprintf(count_str, sizeof(count_str), "%d", count);
        sqlite3_bind_text(stmt, 5, count_str, -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) return -1;
    }

    /* Save each system as a blob in the systems table */
    exec_sql(p->db, "BEGIN;");
    for (int i = 0; i < count; i++) {
        sqlite3_stmt *stmt;
        const char *sql = "INSERT OR REPLACE INTO systems (id, sector_x, sector_y, sector_z, data) "
                          "VALUES (?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(p->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            exec_sql(p->db, "ROLLBACK;");
            return -1;
        }
        char id_str[33];
        uid_to_str(systems[i].id, id_str, sizeof(id_str));
        sqlite3_bind_text(stmt, 1, id_str, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, coord.x);
        sqlite3_bind_int64(stmt, 3, coord.y);
        sqlite3_bind_int64(stmt, 4, coord.z);
        /* Store as binary blob — fast, compact */
        sqlite3_bind_text(stmt, 5, (const char *)&systems[i], (int)sizeof(system_t), SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            exec_sql(p->db, "ROLLBACK;");
            return -1;
        }
    }
    exec_sql(p->db, "COMMIT;");
    return 0;
}

int persist_sector_exists(persist_t *p, sector_coord_t coord) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT data FROM sectors WHERE x = ? AND y = ? AND z = ?;";
    if (sqlite3_prepare_v2(p->db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, coord.x);
    sqlite3_bind_int64(stmt, 2, coord.y);
    sqlite3_bind_int64(stmt, 3, coord.z);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char *data = (const char *)sqlite3_column_text(stmt, 0);
        int count = data ? atoi(data) : 0;
        sqlite3_finalize(stmt);
        return count;
    }
    sqlite3_finalize(stmt);
    return -1;
}

int persist_load_sector(persist_t *p, sector_coord_t coord,
                        system_t *out, int max_systems) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT data FROM systems WHERE sector_x = ? AND sector_y = ? AND sector_z = ?;";
    if (sqlite3_prepare_v2(p->db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, coord.x);
    sqlite3_bind_int64(stmt, 2, coord.y);
    sqlite3_bind_int64(stmt, 3, coord.z);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_systems) {
        const void *blob = sqlite3_column_text(stmt, 0);
        if (blob) {
            memcpy(&out[count], blob, sizeof(system_t));
            count++;
        }
    }
    sqlite3_finalize(stmt);
    return count;
}
