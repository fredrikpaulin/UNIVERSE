/*
 * persist.h — SQLite persistence layer
 */
#ifndef PERSIST_H
#define PERSIST_H

#include "universe.h"
#include "../vendor/sqlite3.h"

typedef struct {
    sqlite3 *db;
} persist_t;

/* Open (or create) the database file. Creates schema if needed. */
int  persist_open(persist_t *p, const char *path);

/* Close the database */
void persist_close(persist_t *p);

/* Save universe metadata (seed, tick, generation_version) */
int  persist_save_meta(persist_t *p, const universe_t *u);

/* Load universe metadata. Returns 0 on success, -1 if no data. */
int  persist_load_meta(persist_t *p, universe_t *u);

/* Save current tick number (fast, for frequent saves) */
int  persist_save_tick(persist_t *p, uint64_t tick);

/* Save a generated sector (all its systems) */
int  persist_save_sector(persist_t *p, sector_coord_t coord, uint64_t tick,
                         const system_t *systems, int count);

/* Check if a sector has been generated. Returns system count, or -1 if not found. */
int  persist_sector_exists(persist_t *p, sector_coord_t coord);

/* Load all systems in a sector. Returns count loaded, or -1 on error.
 * Caller provides output array and max capacity. */
int  persist_load_sector(persist_t *p, sector_coord_t coord,
                         system_t *out, int max_systems);

#endif
