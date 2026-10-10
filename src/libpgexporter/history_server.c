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

/* pgexporter */
#include <history_server.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <pgexporter.h>
#include <queries.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * Connection and series/sample SQL shared by the PostgreSQL and TimescaleDB
 * history backends. Both talk to a PostgreSQL server; only the hypertable
 * setup and retention statement differ, and those stay in the backend.
 */

static SSL* ssl = NULL;
static int fd = -1;

static const char* schema_sql[] = {
   "CREATE SCHEMA IF NOT EXISTS pgexporter",
   "CREATE TABLE IF NOT EXISTS pgexporter.series ("
   "series_id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY, "
   "metric text NOT NULL, "
   "server text NOT NULL DEFAULT '', "
   "label_hash text NOT NULL, "
   "labels text NOT NULL DEFAULT '', "
   "UNIQUE (metric, server, label_hash))",
   "CREATE TABLE IF NOT EXISTS pgexporter.sample ("
   "series_id bigint NOT NULL REFERENCES pgexporter.series (series_id), "
   "ts bigint NOT NULL, "
   "value double precision NOT NULL, "
   "PRIMARY KEY (series_id, ts))",
   "CREATE INDEX IF NOT EXISTS idx_sample_ts ON pgexporter.sample (ts)",
};

/* One statement resolves the series and writes the sample, so the batch can be pipelined */
static const char* write_sql =
   "WITH ins AS ("
   "INSERT INTO pgexporter.series (metric, server, label_hash, labels) "
   "VALUES ($1, $2, $3, $4) "
   "ON CONFLICT (metric, server, label_hash) DO NOTHING "
   "RETURNING series_id"
   "), "
   "resolved AS ("
   "SELECT series_id FROM ins "
   "UNION ALL "
   "SELECT s.series_id FROM pgexporter.series s "
   "WHERE s.metric = $1 AND s.server = $2 AND s.label_hash = $3 "
   "AND NOT EXISTS (SELECT 1 FROM ins)"
   ") "
   "INSERT INTO pgexporter.sample (series_id, ts, value) "
   "SELECT series_id, $5::bigint, $6::double precision FROM resolved "
   "ON CONFLICT (series_id, ts) DO UPDATE SET value = EXCLUDED.value";

static const char* query_sql =
   "SELECT sa.ts, se.server, se.metric, se.labels, sa.value "
   "FROM pgexporter.sample sa JOIN pgexporter.series se ON se.series_id = sa.series_id "
   "WHERE se.metric = $1 AND sa.ts >= $2::bigint AND sa.ts <= $3::bigint "
   "ORDER BY sa.ts ASC";

static const char* sweep_sql =
   "DELETE FROM pgexporter.series WHERE NOT EXISTS ("
   "SELECT 1 FROM pgexporter.sample WHERE sample.series_id = series.series_id)";

static const char*
backend_tag(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config != NULL && config->history_backend == HISTORY_BACKEND_TIMESCALEDB)
   {
      return "history_timescaledb";
   }

   return "history_postgresql";
}

static int
validate_configuration(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!config)
   {
      pgexporter_log_error("%s: no configuration available", backend_tag());
      return 1;
   }

   if (strlen(config->history_postgresql_host) == 0)
   {
      pgexporter_log_error("%s: history_postgresql_host is not set", backend_tag());
      return 1;
   }

   if (strlen(config->history_postgresql_database) == 0)
   {
      pgexporter_log_error("%s: history_postgresql_database is not set", backend_tag());
      return 1;
   }

   return 0;
}

static int
connect_store(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (pgexporter_authenticate_host("history", config->history_postgresql_host, config->history_postgresql_port,
                                    config->history_postgresql_tls, config->history_postgresql_tls_cert_file,
                                    config->history_postgresql_tls_key_file, config->history_postgresql_tls_ca_file,
                                    config->history_postgresql_database, config->history_user.username,
                                    config->history_user.password, &ssl, &fd) != AUTH_SUCCESS)
   {
      pgexporter_log_error("%s: failed to connect to %s:%d/%s as %s",
                           backend_tag(),
                           config->history_postgresql_host, config->history_postgresql_port,
                           config->history_postgresql_database, config->history_user.username);
      return 1;
   }

   pgexporter_log_debug("%s: connected to %s:%d/%s",
                        backend_tag(),
                        config->history_postgresql_host, config->history_postgresql_port,
                        config->history_postgresql_database);

   return 0;
}

int
pgexporter_history_server_init(void)
{
   if (validate_configuration())
   {
      return 1;
   }

   if (fd != -1)
   {
      return 0;
   }

   if (connect_store())
   {
      pgexporter_history_server_shutdown();
      return 1;
   }

   return 0;
}

void
pgexporter_history_server_shutdown(void)
{
   if (fd != -1)
   {
      pgexporter_write_terminate(ssl, fd);
   }

   pgexporter_close_ssl(ssl);

   if (fd != -1)
   {
      pgexporter_disconnect(fd);
   }

   ssl = NULL;
   fd = -1;
}

bool
pgexporter_history_server_connected(void)
{
   return fd != -1;
}

int
pgexporter_history_server_create_schema(void)
{
   struct query_pipeline* pipeline = NULL;

   if (fd == -1)
   {
      return 1;
   }

   if (pgexporter_pipeline_create(&pipeline))
   {
      goto error;
   }

   for (size_t i = 0; i < sizeof(schema_sql) / sizeof(schema_sql[0]); i++)
   {
      if (pgexporter_pipeline_prepare(pipeline, "schema", (char*)schema_sql[i], 0) ||
          pgexporter_pipeline_execute(pipeline, "schema", 0, NULL))
      {
         goto error;
      }
   }

   if (pgexporter_pipeline_sync(ssl, fd, pipeline))
   {
      pgexporter_log_error("%s: failed to create schema", backend_tag());
      goto error;
   }

   pgexporter_pipeline_destroy(pipeline);

   return 0;

error:

   pgexporter_pipeline_destroy(pipeline);

   return 1;
}

int
pgexporter_history_server_write_batch(struct history_record* records, int count)
{
   struct query_pipeline* pipeline = NULL;
   bool in_txn = false;

   if (fd == -1)
   {
      return 1;
   }

   if (count == 0)
   {
      return 0;
   }

   if (pgexporter_history_server_command("BEGIN", 0, NULL))
   {
      pgexporter_log_error("%s: begin failed", backend_tag());
      goto error;
   }
   in_txn = true;

   if (pgexporter_pipeline_create(&pipeline))
   {
      goto error;
   }

   if (pgexporter_pipeline_prepare(pipeline, "write", (char*)write_sql, 6))
   {
      pgexporter_log_error("%s: prepare failed", backend_tag());
      goto error;
   }

   for (int i = 0; i < count; i++)
   {
      char label_hash[HISTORY_LABEL_HASH_LENGTH];
      char ts_buf[32];
      char value_buf[64];
      char* values[6];
      const char* labels = records[i].labels ? records[i].labels : "";

      if (pgexporter_history_label_hash(labels, label_hash))
      {
         pgexporter_log_error("%s: failed to hash labels for metric %s", backend_tag(), records[i].metric);
         goto error;
      }

      pgexporter_snprintf(ts_buf, sizeof(ts_buf), "%lld", (long long)records[i].ts);
      pgexporter_snprintf(value_buf, sizeof(value_buf), "%.17g", records[i].value);

      values[0] = records[i].metric;
      values[1] = records[i].server;
      values[2] = label_hash;
      values[3] = (char*)labels;
      values[4] = ts_buf;
      values[5] = value_buf;

      if (pgexporter_pipeline_execute(pipeline, "write", 6, values))
      {
         goto error;
      }
   }

   if (pgexporter_pipeline_sync(ssl, fd, pipeline))
   {
      pgexporter_log_error("%s: insert failed", backend_tag());
      goto error;
   }

   pgexporter_pipeline_destroy(pipeline);
   pipeline = NULL;

   if (pgexporter_history_server_command("COMMIT", 0, NULL))
   {
      pgexporter_log_error("%s: commit failed", backend_tag());
      goto error;
   }

   return 0;

error:

   pgexporter_pipeline_destroy(pipeline);

   if (in_txn && pgexporter_history_server_command("ROLLBACK", 0, NULL))
   {
      pgexporter_log_error("%s: rollback failed", backend_tag());
   }

   return 1;
}

int
pgexporter_history_server_query_range(const char* metric, time_t start, time_t end,
                                      struct history_record** records_out, int* count_out)
{
   struct query* query = NULL;
   struct tuple* tuple;
   char start_buf[32];
   char end_buf[32];
   char* values[3];
   int count = 0;
   int capacity = 100;
   struct history_record* results = NULL;

   if (count_out != NULL)
   {
      *count_out = 0;
   }

   if (fd == -1)
   {
      goto error;
   }

   pgexporter_snprintf(start_buf, sizeof(start_buf), "%lld", (long long)start);
   pgexporter_snprintf(end_buf, sizeof(end_buf), "%lld", (long long)end);
   values[0] = (char*)metric;
   values[1] = start_buf;
   values[2] = end_buf;

   if (pgexporter_history_server_query(query_sql, 3, values, &query))
   {
      goto error;
   }

   if (records_out != NULL)
   {
      results = malloc(capacity * sizeof(struct history_record));
      if (!results)
      {
         goto error;
      }
      memset(results, 0, capacity * sizeof(struct history_record));
   }

   for (tuple = query->tuples; tuple != NULL; tuple = tuple->next)
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

         if (tuple->data[0] != NULL)
         {
            results[count].ts = (time_t)strtoll(tuple->data[0], NULL, 10);
         }

         if (tuple->data[1] != NULL)
         {
            pgexporter_snprintf(results[count].server, MISC_LENGTH, "%s", tuple->data[1]);
         }

         if (tuple->data[2] != NULL)
         {
            pgexporter_snprintf(results[count].metric, PROMETHEUS_LENGTH, "%s", tuple->data[2]);
         }

         results[count].labels = pgexporter_append(NULL, tuple->data[3] != NULL ? tuple->data[3] : (char*)"");

         if (tuple->data[4] != NULL)
         {
            results[count].value = strtod(tuple->data[4], NULL);
         }
      }
      count++;
   }

   pgexporter_free_query(query);

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

   pgexporter_free_query(query);

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
pgexporter_history_server_query(const char* sql, int nparams, char** values, struct query** query)
{
   if (query == NULL || fd == -1)
   {
      return 1;
   }

   *query = NULL;

   if (pgexporter_query_execute_params(ssl, fd, (char*)sql, nparams, values, "history", query))
   {
      pgexporter_log_error("%s: query failed", backend_tag());
      return 1;
   }

   return 0;
}

int
pgexporter_history_server_execute(const char* sql, int nparams, char** values)
{
   struct query* query = NULL;
   int ret;

   ret = pgexporter_history_server_query(sql, nparams, values, &query);
   pgexporter_free_query(query);

   return ret;
}

int
pgexporter_history_server_command(const char* sql, int nparams, char** values)
{
   if (fd == -1)
   {
      return 1;
   }

   if (pgexporter_command_execute_params(ssl, fd, (char*)sql, nparams, values))
   {
      return 1;
   }

   return 0;
}

int
pgexporter_history_server_prune(const char* prune_sql, bool returns_rows)
{
   struct configuration* config;
   char cutoff_buf[32];
   char* values[1];
   time_t cutoff;
   int64_t retention_s;

   config = (struct configuration*)shmem;

   if (fd == -1 || !config || !pgexporter_time_is_valid(config->history_retention))
   {
      return 0;
   }

   retention_s = pgexporter_time_convert(config->history_retention, FORMAT_TIME_S);
   if (retention_s <= 0)
   {
      return 0;
   }

   cutoff = time(NULL) - (time_t)retention_s;
   pgexporter_snprintf(cutoff_buf, sizeof(cutoff_buf), "%lld", (long long)cutoff);
   values[0] = cutoff_buf;

   if (returns_rows)
   {
      if (pgexporter_history_server_execute(prune_sql, 1, values))
      {
         pgexporter_log_error("%s: prune failed", backend_tag());
         return 1;
      }
   }
   else if (pgexporter_history_server_command(prune_sql, 1, values))
   {
      pgexporter_log_error("%s: prune failed", backend_tag());
      return 1;
   }

   /* Separate statement: the sweep re-checks NOT EXISTS, so an insert in between is safe */
   if (pgexporter_history_server_command(sweep_sql, 0, NULL))
   {
      pgexporter_log_error("%s: orphan series sweep failed", backend_tag());
      return 1;
   }

   return 0;
}
