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
#include <art.h>
#include <history.h>
#include <local_history.h>
#include <logging.h>
#include <prometheus.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void local_history_collect(const struct history_store* store);

const struct history_store*
pgexporter_local_history_store(void)
{
   static struct history_store store;
   struct configuration* config = (struct configuration*)shmem;

   if (config == NULL)
   {
      return NULL;
   }

   store.name = "history";
   store.port = config->history;
   store.backend = config->history_backend;
   store.path = config->history_path;
   store.pg_host = config->history_postgresql_host;
   store.pg_port = config->history_postgresql_port;
   store.pg_database = config->history_postgresql_database;
   store.pg_user = config->history_user.username;
   store.pg_password = config->history_user.password;
   store.pg_tls = config->history_postgresql_tls;
   store.pg_tls_cert_file = config->history_postgresql_tls_cert_file;
   store.pg_tls_key_file = config->history_postgresql_tls_key_file;
   store.pg_tls_ca_file = config->history_postgresql_tls_ca_file;
   store.interval = config->history_interval;
   store.retention = config->history_retention;
   store.running = &config->history_worker_running;
   store.last_store = &config->history_last_store_time;
   store.worker_pid = &config->history_worker_pid;
   store.retention_running = &config->history_retention_worker_running;
   store.retention_pid = &config->history_retention_worker_pid;
   store.collect = local_history_collect;

   return &store;
}

int
pgexporter_local_history_store_metrics(struct prometheus_metrics_container* container)
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

                        /* Same parser as the label hash: no false "server" match, no early quote end */
                        pgexporter_history_label_find(labels, "server", server, sizeof(server));
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
      status = pgexporter_history_write_batch(records, count);
   }
   else
   {
      status = 0;
   }

cleanup:
   pgexporter_history_records_free(records, count);

   return status;
}

static void
local_history_collect(const struct history_store* store)
{
   prometheus_metrics_container_t* container = NULL;

   /* Scrape first so no history connection sits idle during the scrape */
   if (pgexporter_prometheus_scrape(&container) != 0)
   {
      pgexporter_log_error("history: failed to scrape metrics");
      goto done;
   }

   if (pgexporter_history_init(store) != 0)
   {
      pgexporter_log_error("history: failed to init history db");
      goto done;
   }

   pgexporter_local_history_store_metrics(container);

done:
   if (container != NULL)
   {
      pgexporter_prometheus_destroy_container(container);
   }

   pgexporter_history_shutdown();
}

void
pgexporter_local_history_tick_cb(void)
{
   pgexporter_history_tick(pgexporter_local_history_store());
}

void
pgexporter_local_history_retention_tick_cb(void)
{
   pgexporter_history_retention_tick(pgexporter_local_history_store());
}

void
pgexporter_local_history_http(SSL* ssl, int fd)
{
   pgexporter_history_serve(pgexporter_local_history_store(), ssl, fd);
}
