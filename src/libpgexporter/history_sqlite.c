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

int
pgexporter_history_sqlite_init(void)
{
   struct configuration* config;
   char* err_msg = NULL;
   const char* sql = "CREATE TABLE IF NOT EXISTS history ("
                     "ts INTEGER, "
                     "server TEXT, "
                     "metric TEXT, "
                     "labels TEXT, "
                     "value REAL"
                     ");"
                     /* Index covers both query_range (metric + ts range) and
                      * prune (ts range), avoiding a full table scan. */
                     "CREATE INDEX IF NOT EXISTS idx_history_metric_ts ON history(metric, ts);"
                     "CREATE INDEX IF NOT EXISTS idx_history_ts ON history(ts);";

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
      if (db)
      {
         sqlite3_close(db);
         db = NULL;
      }
      goto error;
   }

   if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: failed to create table: %s", err_msg);
      if (err_msg)
      {
         sqlite3_free(err_msg);
      }
      goto error;
   }

   pgexporter_log_debug("history_sqlite: initialized at %s", config->history_path);
   return 0;

error:

   return 1;
}

int
pgexporter_history_sqlite_write_batch(struct history_record* records, int count)
{
   sqlite3_stmt* stmt = NULL;
   const char* sql = "INSERT INTO history(ts, server, metric, labels, value) VALUES(?, ?, ?, ?, ?);";
   bool in_txn = false;
   int i;

   if (!db)
   {
      goto error;
   }

   if (sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
   {
      goto error;
   }
   in_txn = true;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
   {
      pgexporter_log_error("history_sqlite: prepare failed: %s", sqlite3_errmsg(db));
      goto error;
   }

   for (i = 0; i < count; i++)
   {
      sqlite3_bind_int64(stmt, 1, (sqlite3_int64)records[i].ts);
      sqlite3_bind_text(stmt, 2, records[i].server, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 3, records[i].metric, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 4, records[i].labels ? records[i].labels : "", -1, SQLITE_TRANSIENT);
      sqlite3_bind_double(stmt, 5, records[i].value);

      if (sqlite3_step(stmt) != SQLITE_DONE)
      {
         pgexporter_log_error("history_sqlite: insert failed: %s", sqlite3_errmsg(db));
         goto error;
      }
      sqlite3_reset(stmt);
   }

   sqlite3_finalize(stmt);
   stmt = NULL;

   if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
   {
      goto error;
   }

   return 0;

error:

   if (stmt)
   {
      sqlite3_finalize(stmt);
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
   const char* sql = "SELECT ts, server, metric, labels, value FROM history WHERE metric = ? AND ts >= ? AND ts <= ? ORDER BY ts ASC;";
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
   const char* sql = "DELETE FROM history WHERE ts < ?;";
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
   .init = pgexporter_history_sqlite_init,
   .write_batch = pgexporter_history_sqlite_write_batch,
   .query_range = pgexporter_history_sqlite_query_range,
   .prune = pgexporter_history_sqlite_prune,
   .shutdown = pgexporter_history_sqlite_shutdown,
};
