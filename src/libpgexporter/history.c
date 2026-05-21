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

#include <history.h>
#include <history_sqlite.h>
#include <logging.h>
#include <pgexporter.h>

#include <stddef.h>
#include <stdlib.h>

static const struct history_backend_ops* ops = NULL;

static const struct
{
   int id;
   const struct history_backend_ops* ops;
} backend_registry[] = {
   {HISTORY_BACKEND_SQLITE, &pgexporter_history_sqlite_ops},
};

#define BACKEND_REGISTRY_SIZE (sizeof(backend_registry) / sizeof(backend_registry[0]))

int
pgexporter_history_init(void)
{
   struct configuration* config = (struct configuration*)shmem;
   int backend = config->history_backend;

   for (size_t i = 0; i < BACKEND_REGISTRY_SIZE; i++)
   {
      if (backend_registry[i].id == backend)
      {
         ops = backend_registry[i].ops;
         return ops->init();
      }
   }

   pgexporter_log_error("history: unknown backend %d", backend);
   return 1;
}

int
pgexporter_history_write_batch(struct history_record* records, int count)
{
   if (!ops)
   {
      return 1;
   }
   return ops->write_batch(records, count);
}

int
pgexporter_history_query_range(const char* metric, time_t start, time_t end,
                               struct history_record** records_out, int* count_out)
{
   if (!ops)
   {
      return 1;
   }
   return ops->query_range(metric, start, end, records_out, count_out);
}

int
pgexporter_history_prune(void)
{
   if (!ops)
   {
      return 1;
   }
   return ops->prune();
}

int
pgexporter_history_shutdown(void)
{
   int ret;

   if (!ops)
   {
      return 1;
   }
   ret = ops->shutdown();
   ops = NULL;
   return ret;
}

void
pgexporter_history_tick_cb(void)
{
   /* TODO: fork history worker, skip if previous worker still running */
   pgexporter_log_debug("history: tick (not yet implemented)");
}

void
pgexporter_history_retention_tick_cb(void)
{
   /* TODO: fork retention worker */
   pgexporter_log_debug("history: retention tick (not yet implemented)");
}
