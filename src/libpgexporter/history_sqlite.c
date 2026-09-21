/*
 * Copyright (C) 2026 The pgexporter community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <history_sqlite.h>
#include <logging.h>
#include <pgexporter.h>
#include <shmem.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/**
 * SQLite Backend for pgexporter History
 *
 * This module implements the `HISTORY_BACKEND_SQLITE` backend, storing metrics
 * history in a local SQLite database file. It provides:
 *
 * - Database initialization (creating tables and indexes).
 * - Batch insertion of history records using a single transaction.
 * - Range queries based on metric name and time window.
 * - Pruning of old records according to the configured retention policy.
 *
 * The implementation relies on standard SQLite C API functions and handles
 * resource cleanup/rollback on error.
 */

static sqlite3* db = NULL;

/* Maximum free pages reclaimed per prune via PRAGMA incremental_vacuum */
#define HISTORY_SQLITE_VACUUM_PAGES 1000

/* sample is WITHOUT ROWID: nearly all key, halving space and write amplification */
/* idx_sample_ts: the PK leads with series_id, but graphs and pruning read by ts */
static const char* schema_sql =
   "CREATE TABLE IF NOT EXISTS series ("
   "series_id INTEGER PRIMARY KEY, "
   "metric TEXT NOT NULL, "
   "server TEXT NOT NULL DEFAULT '', "
   "label_hash TEXT NOT NULL, "
   "labels TEXT NOT NULL DEFAULT '', "
   "UNIQUE (metric, server, label_hash)"
   ");"
   "CREATE TABLE IF NOT EXISTS sample ("
   "series_id INTEGER NOT NULL REFERENCES series(series_id), "
   "ts INTEGER NOT NULL, "
   "value REAL NOT NULL, "
   "PRIMARY KEY (series_id, ts)"
   ") WITHOUT ROWID;"
   "CREATE INDEX IF NOT EXISTS idx_sample_ts ON sample(ts);";

/**
 * Run a statement that returns no rows, logging on failure.
 *
 * @param sql   The SQL to execute
 * @param what  Short description used in the log message
 * @return 0 on success, 1 on failure
 */
static int
exec_sql(const char* sql, const char* what)
{
   char* err_msg = NULL;

   if (sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: %s failed: %s", what, err_msg ? err_msg : sqlite3_errmsg(db));
      if (err_msg)
      {
         sqlite3_free(err_msg);
      }
      return 1;
   }

   if (err_msg)
   {
      sqlite3_free(err_msg);
   }

   return 0;
}

static int
open_db(void)
{
   struct configuration* config;

   if (db != NULL)
   {
      return 0;
   }

   config = (struct configuration*)shmem;

   if (!config || !config->history_path[0])
   {
      pgexporter_log_error("history_sqlite: no history path configured");
      goto error;
   }

   if (sqlite3_open_v2(config->history_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: failed to open db %s: %s", config->history_path, db ? sqlite3_errmsg(db) : "out of memory");
      goto error;
   }

   /* auto_vacuum must precede the first table to stick on a fresh file */
   if (exec_sql("PRAGMA auto_vacuum=INCREMENTAL;"
                "PRAGMA journal_mode=WAL;"
                "PRAGMA busy_timeout=5000;"
                "PRAGMA foreign_keys=ON;",
                "pragma setup"))
   {
      goto error;
   }

   return 0;

error:

   if (db)
   {
      sqlite3_close_v2(db);
      db = NULL;
   }

   return 1;
}

int
pgexporter_history_sqlite_create(void)
{
   struct configuration* config = (struct configuration*)shmem;
   bool opened = db != NULL;
   int ret = 1;

   if (open_db())
   {
      goto done;
   }

   ret = exec_sql(schema_sql, "schema create");

   if (ret == 0)
   {
      pgexporter_log_debug("history_sqlite: created at %s", config->history_path);
   }

done:

   if (!opened && db)
   {
      sqlite3_close_v2(db);
      db = NULL;
   }

   return ret;
}

int
pgexporter_history_sqlite_init(void)
{
   return open_db();
}

/**
 * Resolve a record's series, inserting it the first time it is seen.
 *
 * @param record  The record whose metric/server/labels identify the series
 * @param select  Prepared SELECT over the (metric, server, label_hash) unique index
 * @param insert  Prepared INSERT into series
 * @param out     Receives the series_id
 * @return 0 on success, 1 on failure
 */
static int
series_resolve(struct history_record* record, sqlite3_stmt* select, sqlite3_stmt* insert, sqlite3_int64* out)
{
   char label_hash[HISTORY_LABEL_HASH_LENGTH];
   const char* labels = record->labels ? record->labels : "";
   int rc;

   if (pgexporter_history_label_hash(labels, label_hash))
   {
      pgexporter_log_error("history_sqlite: failed to hash labels for metric %s", record->metric);
      goto error;
   }

   sqlite3_reset(select);
   sqlite3_bind_text(select, 1, record->metric, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(select, 2, record->server, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(select, 3, label_hash, -1, SQLITE_TRANSIENT);

   rc = sqlite3_step(select);

   if (rc == SQLITE_ROW)
   {
      *out = sqlite3_column_int64(select, 0);
      sqlite3_reset(select);
      return 0;
   }

   if (rc != SQLITE_DONE)
   {
      pgexporter_log_error("history_sqlite: series lookup failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   sqlite3_reset(select);

   sqlite3_reset(insert);
   sqlite3_bind_text(insert, 1, record->metric, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(insert, 2, record->server, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(insert, 3, label_hash, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(insert, 4, labels, -1, SQLITE_TRANSIENT);

   if (sqlite3_step(insert) != SQLITE_DONE)
   {
      pgexporter_log_error("history_sqlite: series insert failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   sqlite3_reset(insert);
   *out = sqlite3_last_insert_rowid(db);

   return 0;

error:

   return 1;
}

int
pgexporter_history_sqlite_write_batch(struct history_record* records, int count)
{
   sqlite3_stmt* select_series = NULL;
   sqlite3_stmt* insert_series = NULL;
   sqlite3_stmt* insert_sample = NULL;
   const char* select_series_sql = "SELECT series_id FROM series WHERE metric = ? AND server = ? AND label_hash = ?;";
   const char* insert_series_sql = "INSERT INTO series(metric, server, label_hash, labels) VALUES(?, ?, ?, ?);";

   const char* insert_sample_sql = "INSERT OR REPLACE INTO sample(series_id, ts, value) VALUES(?, ?, ?);";
   bool in_txn = false;
   int i;

   if (!db)
   {
      goto error;
   }

   /* IMMEDIATE: resolving a series reads before it writes, else SQLITE_BUSY_SNAPSHOT */
   if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: begin failed: %s", sqlite3_errmsg(db));
      goto error;
   }
   in_txn = true;

   if (sqlite3_prepare_v2(db, select_series_sql, -1, &select_series, NULL) != SQLITE_OK ||
       sqlite3_prepare_v2(db, insert_series_sql, -1, &insert_series, NULL) != SQLITE_OK ||
       sqlite3_prepare_v2(db, insert_sample_sql, -1, &insert_sample, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: prepare failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   for (i = 0; i < count; i++)
   {
      sqlite3_int64 series_id = 0;

      if (series_resolve(&records[i], select_series, insert_series, &series_id))
      {
         goto error;
      }

      sqlite3_reset(insert_sample);
      sqlite3_bind_int64(insert_sample, 1, series_id);
      sqlite3_bind_int64(insert_sample, 2, (sqlite3_int64)records[i].ts);
      sqlite3_bind_double(insert_sample, 3, records[i].value);

      if (sqlite3_step(insert_sample) != SQLITE_DONE)
      {
         pgexporter_log_error("history_sqlite: insert failed: %s", sqlite3_errmsg(db));
         goto error;
      }
   }

   sqlite3_finalize(select_series);
   select_series = NULL;
   sqlite3_finalize(insert_series);
   insert_series = NULL;
   sqlite3_finalize(insert_sample);
   insert_sample = NULL;

   if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: commit failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   return 0;

error:

   if (select_series)
   {
      sqlite3_finalize(select_series);
   }
   if (insert_series)
   {
      sqlite3_finalize(insert_series);
   }
   if (insert_sample)
   {
      sqlite3_finalize(insert_sample);
   }

   /* Only roll back if a transaction was actually started */
   if (db && in_txn)
   {
      if (sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL) != SQLITE_OK)
      {
         pgexporter_log_error("history_sqlite: rollback failed: %s", sqlite3_errmsg(db));
      }
   }

   return 1;
}

int
pgexporter_history_sqlite_query_range(const char* metric, time_t start, time_t end,
                                      struct history_record** records_out, int* count_out)
{
   sqlite3_stmt* stmt = NULL;
   const char* sql = "SELECT sa.ts, se.server, se.metric, se.labels, sa.value "
                     "FROM sample sa JOIN series se ON se.series_id = sa.series_id "
                     "WHERE se.metric = ? AND sa.ts >= ? AND sa.ts <= ? "
                     "ORDER BY sa.ts ASC;";
   int count = 0;
   int capacity = 100;
   struct history_record* results = NULL;

   if (count_out)
   {
      *count_out = 0;
   }

   if (!db)
   {
      goto error;
   }

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: query prepare failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   sqlite3_bind_text(stmt, 1, metric, -1, SQLITE_TRANSIENT);
   sqlite3_bind_int64(stmt, 2, (sqlite3_int64)start);
   sqlite3_bind_int64(stmt, 3, (sqlite3_int64)end);

   if (records_out)
   {
      results = malloc(capacity * sizeof(struct history_record));
      if (!results)
      {
         goto error;
      }
      memset(results, 0, capacity * sizeof(struct history_record));
   }

   while (sqlite3_step(stmt) == SQLITE_ROW)
   {
      if (records_out)
      {
         if (count >= capacity)
         {
            struct history_record* new_results;
            capacity *= 2;
            new_results = realloc(results, capacity * sizeof(struct history_record));
            if (!new_results)
            {
               goto error;
            }
            results = new_results;
            memset(results + (capacity / 2), 0, (capacity / 2) * sizeof(struct history_record));
         }

         results[count].ts = (time_t)sqlite3_column_int64(stmt, 0);

         const unsigned char* srv = sqlite3_column_text(stmt, 1);
         if (srv)
            pgexporter_snprintf(results[count].server, MISC_LENGTH, "%s", (const char*)srv);

         const unsigned char* met = sqlite3_column_text(stmt, 2);
         if (met)
            pgexporter_snprintf(results[count].metric, PROMETHEUS_LENGTH, "%s", (const char*)met);

         const unsigned char* lab = sqlite3_column_text(stmt, 3);
         results[count].labels = pgexporter_append(NULL, lab ? (char*)lab : (char*)"");

         results[count].value = sqlite3_column_double(stmt, 4);
      }
      count++;
   }

   sqlite3_finalize(stmt);
   stmt = NULL;

   if (count_out)
   {
      *count_out = count;
   }

   if (records_out)
   {
      *records_out = results;
   }

   return 0;

error:

   if (stmt)
   {
      sqlite3_finalize(stmt);
   }

   if (results)
   {
      for (int i = 0; i < count; i++)
      {
         free(results[i].labels);
      }
      free(results);
   }

   return 1;
}

int
pgexporter_history_sqlite_prune(void)
{
   struct configuration* config;
   sqlite3_stmt* stmt = NULL;
   const char* sql = "DELETE FROM sample WHERE ts < ?;";
   const char* sweep_sql = "DELETE FROM series WHERE NOT EXISTS ("
                           "SELECT 1 FROM sample WHERE sample.series_id = series.series_id);";
   char vacuum_sql[48];
   time_t cutoff;
   int64_t retention_s;

   config = (struct configuration*)shmem;

   if (!db || !config || !pgexporter_time_is_valid(config->history_retention))
   {
      return 0;
   }

   retention_s = pgexporter_time_convert(config->history_retention, FORMAT_TIME_S);
   if (retention_s <= 0)
   {
      return 0;
   }

   cutoff = time(NULL) - (time_t)retention_s;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: prune prepare failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);

   if (sqlite3_step(stmt) != SQLITE_DONE)
   {
      pgexporter_log_error("history_sqlite: prune failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   sqlite3_finalize(stmt);
   stmt = NULL;

   /* Separate lock: the sweep re-checks NOT EXISTS, so an insert in between is safe */
   if (exec_sql(sweep_sql, "orphan series sweep"))
   {
      goto error;
   }

   /* Hand reclaimed pages back to the OS. Bounded so the write lock is held
    * only briefly; a no-op when auto_vacuum freed nothing. */
   pgexporter_snprintf(vacuum_sql, sizeof(vacuum_sql), "PRAGMA incremental_vacuum(%d);", HISTORY_SQLITE_VACUUM_PAGES);
   if (sqlite3_exec(db, vacuum_sql, NULL, NULL, NULL) != SQLITE_OK)
   {
      pgexporter_log_warn("history_sqlite: incremental_vacuum failed: %s", sqlite3_errmsg(db));
   }

   return 0;

error:

   if (stmt)
   {
      sqlite3_finalize(stmt);
   }

   return 1;
}

int
pgexporter_history_sqlite_shutdown(void)
{
   if (db)
   {
      sqlite3_close_v2(db);
      db = NULL;
   }
   return 0;
}

const struct history_backend_ops pgexporter_history_sqlite_ops = {
   .create = pgexporter_history_sqlite_create,
   .init = pgexporter_history_sqlite_init,
   .write_batch = pgexporter_history_sqlite_write_batch,
   .query_range = pgexporter_history_sqlite_query_range,
   .prune = pgexporter_history_sqlite_prune,
   .shutdown = pgexporter_history_sqlite_shutdown,
};
