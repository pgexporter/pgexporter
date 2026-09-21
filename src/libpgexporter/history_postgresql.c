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

#include <history_postgresql.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <pgexporter.h>
#include <queries.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

#include <stdbool.h>
#include <string.h>

/**
 * PostgreSQL Backend for pgexporter History
 *
 * This module implements the `HISTORY_BACKEND_POSTGRESQL` backend, storing metric
 * history in a PostgreSQL database using the same series/sample schema as the
 * SQLite backend.
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

static int validate_configuration(void);
static int connect_store(void);
static int create_schema(void);
static void disconnect_store(void);

static int
validate_configuration(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!config)
   {
      pgexporter_log_error("history_postgresql: no configuration available");
      return 1;
   }

   if (strlen(config->history_postgresql_host) == 0)
   {
      pgexporter_log_error("history_postgresql: history_postgresql_host is not set");
      return 1;
   }

   if (strlen(config->history_postgresql_database) == 0)
   {
      pgexporter_log_error("history_postgresql: history_postgresql_database is not set");
      return 1;
   }

   return 0;
}

int
pgexporter_history_postgresql_create(void)
{
   bool connected = fd != -1;
   int ret = 1;

   if (pgexporter_history_postgresql_init())
   {
      goto done;
   }

   ret = create_schema();

done:

   if (!connected)
   {
      disconnect_store();
   }

   return ret;
}

int
pgexporter_history_postgresql_init(void)
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
      disconnect_store();
      return 1;
   }

   return 0;
}

int
pgexporter_history_postgresql_write_batch(struct history_record* records, int count)
{
   (void)records;
   (void)count;

   pgexporter_log_error("history_postgresql: not yet implemented (write_batch)");

   return 1;
}

int
pgexporter_history_postgresql_query_range(const char* metric, time_t start, time_t end,
                                          struct history_record** records_out, int* count_out)
{
   (void)metric;
   (void)start;
   (void)end;

   if (records_out)
   {
      *records_out = NULL;
   }

   if (count_out)
   {
      *count_out = 0;
   }

   pgexporter_log_error("history_postgresql: not yet implemented (query_range)");

   return 1;
}

int
pgexporter_history_postgresql_prune(void)
{
   pgexporter_log_error("history_postgresql: not yet implemented (prune)");

   return 1;
}

int
pgexporter_history_postgresql_shutdown(void)
{
   disconnect_store();

   return 0;
}

const struct history_backend_ops pgexporter_history_postgresql_ops = {
   .create = pgexporter_history_postgresql_create,
   .init = pgexporter_history_postgresql_init,
   .write_batch = pgexporter_history_postgresql_write_batch,
   .query_range = pgexporter_history_postgresql_query_range,
   .prune = pgexporter_history_postgresql_prune,
   .shutdown = pgexporter_history_postgresql_shutdown,
};

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
      pgexporter_log_error("history_postgresql: failed to connect to %s:%d/%s as %s",
                           config->history_postgresql_host, config->history_postgresql_port,
                           config->history_postgresql_database, config->history_user.username);
      return 1;
   }

   pgexporter_log_debug("history_postgresql: connected to %s:%d/%s",
                        config->history_postgresql_host, config->history_postgresql_port,
                        config->history_postgresql_database);

   return 0;
}

static int
create_schema(void)
{
   struct query_pipeline* pipeline = NULL;

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
      pgexporter_log_error("history_postgresql: failed to create schema");
      goto error;
   }

   pgexporter_pipeline_destroy(pipeline);

   return 0;

error:

   pgexporter_pipeline_destroy(pipeline);

   return 1;
}

static void
disconnect_store(void)
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
