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

#include <stdlib.h>

/*
 * SQLite backend for the history module.
 *
 * TODO: link libsqlite3 in CMakeLists.txt and include <sqlite3.h>.
 */

int
pgexporter_history_sqlite_init(void)
{
   /* TODO: open config->history_path with sqlite3_open(), run CREATE TABLE */
   pgexporter_log_debug("history_sqlite: init (not yet implemented)");
   return 1;
}

int
pgexporter_history_sqlite_write_batch(struct history_record* records, int count)
{
   (void)records;
   (void)count;
   /* TODO: BEGIN TRANSACTION; prepared INSERT loop; COMMIT */
   pgexporter_log_debug("history_sqlite: write_batch (not yet implemented)");
   return 1;
}

int
pgexporter_history_sqlite_query_range(const char* metric, time_t start, time_t end,
                                      struct history_record** records_out, int* count_out)
{
   (void)metric;
   (void)start;
   (void)end;
   (void)records_out;
   if (count_out)
   {
      *count_out = 0;
   }
   /* TODO: SELECT ... */
   pgexporter_log_debug("history_sqlite: query_range (not yet implemented)");
   return 1;
}

int
pgexporter_history_sqlite_prune(void)
{
   /* TODO: DELETE ... */
   pgexporter_log_debug("history_sqlite: prune (not yet implemented)");
   return 1;
}

int
pgexporter_history_sqlite_shutdown(void)
{
   /* TODO: sqlite3_close() */
   pgexporter_log_debug("history_sqlite: shutdown (not yet implemented)");
   return 1;
}

const struct history_backend_ops pgexporter_history_sqlite_ops = {
   .init = pgexporter_history_sqlite_init,
   .write_batch = pgexporter_history_sqlite_write_batch,
   .query_range = pgexporter_history_sqlite_query_range,
   .prune = pgexporter_history_sqlite_prune,
   .shutdown = pgexporter_history_sqlite_shutdown,
};
