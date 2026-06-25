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
#include <pgexporter.h>
#include <history.h>
#include <history_sqlite.h>
#include <http.h>
#include <logging.h>
#include <prometheus.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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
   if (ops == NULL)
   {
      return 1;
   }
   return ops->write_batch(records, count);
}

int
pgexporter_history_query_range(const char* metric, time_t start, time_t end,
                               struct history_record** records_out, int* count_out)
{
   if (ops == NULL)
   {
      return 1;
   }
   return ops->query_range(metric, start, end, records_out, count_out);
}

int
pgexporter_history_prune(void)
{
   if (ops == NULL)
   {
      return 1;
   }
   return ops->prune();
}

int
pgexporter_history_shutdown(void)
{
   int ret;

   if (ops == NULL)
   {
      return 1;
   }
   ret = ops->shutdown();
   ops = NULL;
   return ret;
}

/**
 * Parse a raw Prometheus container directly into an array
 * of history_record structs and store them in the database.
 *
 * @param container The metrics container
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_store_metrics(struct prometheus_metrics_container* container)
{
   int capacity = 100;
   int count = 0;
   struct history_record* records = NULL;
   time_t now = time(NULL);
   int status = 1;

   if (container == NULL)
   {
      return 1;
   }

   records = malloc(sizeof(struct history_record) * capacity);
   if (records == NULL)
   {
      return 1;
   }

   struct art* trees[] = {
      container->general_metrics,
      container->server_metrics,
      container->version_metrics,
      container->uptime_metrics,
      container->primary_metrics,
      container->fips_metrics,
      container->core_metrics,
      container->extension_metrics,
      container->extension_list_metrics,
      container->settings_metrics,
      container->custom_metrics,
      container->alert_metrics};

   for (size_t i = 0; i < sizeof(trees) / sizeof(trees[0]); i++)
   {
      struct art_iterator* iter = NULL;
      if (trees[i] == NULL)
      {
         continue;
      }

      if (pgexporter_art_iterator_create(trees[i], &iter) == 0)
      {
         while (pgexporter_art_iterator_next(iter))
         {
            char* val = NULL;
            if (pgexporter_prometheus_iterator_value(iter, &val) && val != NULL)
            {
               const char* line = val;
               /* Iterate over each line in the multiline Prometheus exposition block */
               while (*line != '\0')
               {
                  const char* next_line = strchr(line, '\n');
                  size_t line_len;
                  if (next_line)
                  {
                     line_len = next_line - line;
                  }
                  else
                  {
                     line_len = strlen(line);
                  }

                  if (line_len > 0 && line[line_len - 1] == '\r')
                  {
                     line_len--;
                  }

                  if (line_len == 0 || line[0] == '#')
                  {
                     if (!next_line)
                        break;
                     line = next_line + 1;
                     continue;
                  }

                  char metric_name[PROMETHEUS_LENGTH] = {0};
                  char* labels = NULL;
                  char server[MISC_LENGTH] = {0};
                  double value = 0.0;

                  const char* p = line;
                  const char* end_of_line = line + line_len;
                  const char* name_start = p;

                  /* Extract the metric name, stopping at the first brace or whitespace */
                  while (p < end_of_line && *p != '{' && !isspace((unsigned char)*p))
                  {
                     p++;
                  }

                  size_t name_len = p - name_start;
                  pgexporter_snprintf(metric_name, sizeof(metric_name), "%.*s", (int)name_len, name_start);

                  /* If labels are present, extract the full label string and look for the 'server' label */
                  if (p < end_of_line && *p == '{')
                  {
                     p++;
                     const char* labels_start = p;
                     bool escaped = false;
                     bool in_quotes = false;
                     while (p < end_of_line)
                     {
                        if (!escaped && *p == '"')
                           in_quotes = !in_quotes;
                        else if (!in_quotes && *p == '}')
                           break;
                        escaped = (*p == '\\' && !escaped);
                        p++;
                     }

                     if (p < end_of_line && *p == '}')
                     {
                        size_t labels_len = p - labels_start;
                        labels = malloc(labels_len + 1);
                        if (labels == NULL)
                        {
                           pgexporter_log_error("history: labels malloc failed");
                           goto cleanup;
                        }
                        memcpy(labels, labels_start, labels_len);
                        labels[labels_len] = '\0';

                        char* server_pos = strstr(labels, "server=\"");
                        if (server_pos)
                        {
                           server_pos += 8;
                           char* end_quote = strchr(server_pos, '"');
                           if (end_quote)
                           {
                              size_t server_len = end_quote - server_pos;
                              pgexporter_snprintf(server, sizeof(server), "%.*s", (int)server_len, server_pos);
                           }
                        }
                        p++;
                     }
                  }

                  /* Skip any trailing whitespace before the value */
                  while (p < end_of_line && isspace((unsigned char)*p))
                  {
                     p++;
                  }

                  if (p < end_of_line)
                  {
                     /* we must strtod from a null-terminated string, so let's copy the rest of the line */
                     char val_buf[64] = {0};
                     size_t val_len = end_of_line - p;
                     pgexporter_snprintf(val_buf, sizeof(val_buf), "%.*s", (int)val_len, p);
                     value = strtod(val_buf, NULL);
                  }

                  if (count >= capacity)
                  {
                     struct history_record* new_records = NULL;
                     capacity *= 2;
                     new_records = realloc(records, sizeof(struct history_record) * capacity);
                     if (new_records == NULL)
                     {
                        pgexporter_log_error("history: realloc failed");
                        free(labels);
                        goto cleanup;
                     }
                     records = new_records;
                  }

                  memset(&records[count], 0, sizeof(struct history_record));
                  records[count].ts = now;
                  records[count].value = value;
                  pgexporter_snprintf(records[count].metric, PROMETHEUS_LENGTH, "%s", metric_name);
                  pgexporter_snprintf(records[count].server, MISC_LENGTH, "%s", server);
                  records[count].labels = labels; /* transfer ownership; may be NULL */
                  labels = NULL;

                  count++;

                  if (!next_line)
                     break;
                  line = next_line + 1;
               }
            }
         }
         pgexporter_art_iterator_destroy(iter);
      }
   }

   if (count > 0 && records != NULL)
   {
      struct configuration* config = (struct configuration*)shmem;
      status = pgexporter_history_write_batch(records, count);
      if (status == 0)
      {
         atomic_store(&config->history_last_store_time, (int64_t)time(NULL));
      }
   }
   else
   {
      status = 0;
   }

cleanup:
   pgexporter_history_records_free(records, count);

   return status;
}

void
pgexporter_history_records_free(struct history_record* records, int count)
{
   if (records == NULL)
   {
      return;
   }

   for (int i = 0; i < count; i++)
   {
      free(records[i].labels);
   }

   free(records);
}

/**
 * Child-process worker that fetches the current metrics directly
 * and persists them as one history snapshot. Called after fork(); exit(0)s.
 */
static void
history_tick_worker(void)
{
   struct configuration* config = (struct configuration*)shmem;
   prometheus_metrics_container_t* container = NULL;

   if (pgexporter_history_init() != 0)
   {
      pgexporter_log_error("history: failed to init history db");
      goto child_done;
   }

   if (pgexporter_prometheus_scrape(&container) != 0)
   {
      pgexporter_log_error("history: failed to scrape metrics");
      goto child_done;
   }

   pgexporter_history_store_metrics(container);

child_done:
   if (container != NULL)
   {
      pgexporter_prometheus_destroy_container(container);
   }

   pgexporter_history_shutdown();
   atomic_store(&config->history_worker_pid, 0);
   atomic_store(&config->history_worker_running, false);
   exit(0);
}

void
pgexporter_history_tick_cb(void)
{
   struct configuration* config = (struct configuration*)shmem;
   pid_t pid;
   time_t now = time(NULL);
   int tick_time = 0;
   bool expected = false;

   if (config == NULL || config->history == 0)
   {
      return;
   }

   tick_time = pgexporter_time_convert(config->history_interval, FORMAT_TIME_S);
   if (tick_time <= 0)
   {
      return;
   }

   if (now < (time_t)atomic_load(&config->history_last_store_time) + tick_time)
   {
      return;
   }

   if (!atomic_compare_exchange_strong(&config->history_worker_running, &expected, true))
   {
      /* Worker already running */
      return;
   }

   pid = fork();
   if (pid < 0)
   {
      pgexporter_log_error("history: failed to fork ticker worker");
      atomic_store(&config->history_worker_running, false);
      return;
   }
   else if (pid > 0)
   {
      /* Record worker pid so sigchld_cb can clear the running flag
       * if the worker dies before resetting it itself. */
      atomic_store(&config->history_worker_pid, (int)pid);
      return;
   }

   history_tick_worker();
}

/**
 * Child-process worker that opens its own database connection and prunes
 * records older than the configured retention. Called after fork(); exit(0)s.
 */
static void
history_retention_worker(void)
{
   struct configuration* config = (struct configuration*)shmem;

   if (pgexporter_history_init() != 0)
   {
      pgexporter_log_error("history: failed to init history db for retention");
      goto child_done;
   }

   if (pgexporter_history_prune() != 0)
   {
      pgexporter_log_error("history: prune failed");
   }

child_done:
   pgexporter_history_shutdown();
   atomic_store(&config->history_retention_worker_pid, 0);
   atomic_store(&config->history_retention_worker_running, false);
   exit(0);
}

void
pgexporter_history_retention_tick_cb(void)
{
   struct configuration* config = (struct configuration*)shmem;
   pid_t pid;
   bool expected = false;

   if (config == NULL || config->history == 0)
   {
      return;
   }

   if (!atomic_compare_exchange_strong(&config->history_retention_worker_running, &expected, true))
   {
      /* Previous retention worker still running */
      return;
   }

   pid = fork();
   if (pid < 0)
   {
      pgexporter_log_error("history: failed to fork retention worker");
      atomic_store(&config->history_retention_worker_running, false);
      return;
   }
   else if (pid > 0)
   {
      /* Record worker pid so sigchld_cb can clear the running flag
       * if the worker dies before resetting it itself. */
      atomic_store(&config->history_retention_worker_pid, (int)pid);
      return;
   }

   history_retention_worker();
}
