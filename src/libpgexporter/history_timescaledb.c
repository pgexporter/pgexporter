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
#include <history_timescaledb.h>
#include <logging.h>
#include <queries.h>

/* system */
#include <stdbool.h>

/**
 * TimescaleDB backend for pgexporter history.
 *
 * sample is a hypertable partitioned on ts, in chunks of one day. series stays
 * a plain table. Writes and range reads are the PostgreSQL statements.
 */

static const char* extension_sql =
   "SELECT 1 FROM pg_extension WHERE extname = 'timescaledb'";

/* 86400 seconds is one day, in the same units as the bigint ts column */
static const char* hypertable_sql =
   "SELECT create_hypertable('pgexporter.sample', 'ts', "
   "chunk_time_interval => 86400::bigint, if_not_exists => TRUE)";

static const char* prune_sql =
   "SELECT drop_chunks('pgexporter.sample', older_than => $1::bigint)";

static int
extension_installed(int* installed)
{
   struct query* query = NULL;

   *installed = 0;

   if (pgexporter_history_server_query(extension_sql, 0, NULL, &query))
   {
      return 1;
   }

   *installed = query != NULL && query->tuples != NULL;
   pgexporter_free_query(query);

   return 0;
}

int
pgexporter_history_timescaledb_create(void)
{
   bool connected = pgexporter_history_server_connected();
   int installed = 0;
   int ret = 1;

   if (pgexporter_history_timescaledb_init())
   {
      goto done;
   }

   if (extension_installed(&installed))
   {
      goto done;
   }

   if (!installed)
   {
      pgexporter_log_error("history_timescaledb: the timescaledb extension is not installed");
      goto done;
   }

   if (pgexporter_history_server_create_schema())
   {
      goto done;
   }

   if (pgexporter_history_server_execute(hypertable_sql, 0, NULL))
   {
      pgexporter_log_error("history_timescaledb: failed to create the hypertable");
      goto done;
   }

   ret = 0;

done:

   if (!connected)
   {
      pgexporter_history_server_shutdown();
   }

   return ret;
}

int
pgexporter_history_timescaledb_init(void)
{
   return pgexporter_history_server_init();
}

int
pgexporter_history_timescaledb_write_batch(struct history_record* records, int count)
{
   return pgexporter_history_server_write_batch(records, count);
}

int
pgexporter_history_timescaledb_query_range(const char* metric, time_t start, time_t end,
                                           struct history_record** records_out, int* count_out)
{
   return pgexporter_history_server_query_range(metric, start, end, records_out, count_out);
}

int
pgexporter_history_timescaledb_prune(void)
{
   return pgexporter_history_server_prune(prune_sql, true);
}

int
pgexporter_history_timescaledb_shutdown(void)
{
   pgexporter_history_server_shutdown();

   return 0;
}

const struct history_backend_ops pgexporter_history_timescaledb_ops = {
   .create = pgexporter_history_timescaledb_create,
   .init = pgexporter_history_timescaledb_init,
   .write_batch = pgexporter_history_timescaledb_write_batch,
   .query_range = pgexporter_history_timescaledb_query_range,
   .prune = pgexporter_history_timescaledb_prune,
   .shutdown = pgexporter_history_timescaledb_shutdown,
};
