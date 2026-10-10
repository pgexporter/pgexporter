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
#include <history_postgresql.h>
#include <history_server.h>

/* system */
#include <stdbool.h>

/**
 * PostgreSQL Backend for pgexporter History
 *
 * This module implements the `HISTORY_BACKEND_POSTGRESQL` backend, storing metric
 * history in a PostgreSQL database using the same series/sample schema as the
 * SQLite backend.
 */

static const char* prune_sql = "DELETE FROM pgexporter.sample WHERE ts < $1::bigint";

int
pgexporter_history_postgresql_create(void)
{
   bool connected = pgexporter_history_server_connected();
   int ret = 1;

   if (pgexporter_history_postgresql_init())
   {
      goto done;
   }

   ret = pgexporter_history_server_create_schema();

done:

   if (!connected)
   {
      pgexporter_history_server_shutdown();
   }

   return ret;
}

int
pgexporter_history_postgresql_init(void)
{
   return pgexporter_history_server_init();
}

int
pgexporter_history_postgresql_write_batch(struct history_record* records, int count)
{
   return pgexporter_history_server_write_batch(records, count);
}

int
pgexporter_history_postgresql_query_range(const char* metric, time_t start, time_t end,
                                          struct history_record** records_out, int* count_out)
{
   return pgexporter_history_server_query_range(metric, start, end, records_out, count_out);
}

int
pgexporter_history_postgresql_prune(void)
{
   return pgexporter_history_server_prune(prune_sql, false);
}

int
pgexporter_history_postgresql_shutdown(void)
{
   pgexporter_history_server_shutdown();

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
