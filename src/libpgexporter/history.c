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
#include <http_server.h>
#include <json.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <security.h>
#include <utils.h>
#include <value.h>

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

static bool
history_query_param(const char* query, const char* name, char* out, size_t out_size)
{
   size_t name_len = strlen(name);
   const char* p = query;

   if (p == NULL)
   {
      return false;
   }

   while (*p != '\0')
   {
      const char* eq = strchr(p, '=');
      const char* amp = strchr(p, '&');
      size_t key_len;

      if (eq == NULL || (amp != NULL && amp < eq))
      {
         if (amp == NULL)
         {
            break;
         }
         p = amp + 1;
         continue;
      }

      key_len = (size_t)(eq - p);

      if (key_len == name_len && strncmp(p, name, name_len) == 0)
      {
         const char* val_start = eq + 1;
         size_t val_len = amp != NULL ? (size_t)(amp - val_start) : strlen(val_start);

         if (val_len >= out_size)
         {
            val_len = out_size - 1;
         }

         memcpy(out, val_start, val_len);
         out[val_len] = '\0';
         return true;
      }

      if (amp == NULL)
      {
         break;
      }

      p = amp + 1;
   }

   return false;
}

void
pgexporter_history_http(SSL* ssl, int fd)
{
   struct http_server_request* req = NULL;
   struct configuration* config;
   char* path = NULL;
   char* query = NULL;
   char metric[PROMETHEUS_LENGTH];
   char value_buf[64];
   time_t ts;
   long long duration;
   time_t start;
   time_t end;
   struct history_record* records = NULL;
   int count = 0;
   struct json* root = NULL;
   char* json_str = NULL;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   if (pgexporter_history_init())
   {
      pgexporter_log_error("History: failed to initialize backend");
      goto error;
   }

   if (ssl)
   {
      int ssl_status = pgexporter_http_server_ssl_accept(ssl, fd);

      if (ssl_status == MESSAGE_STATUS_ERROR)
      {
         pgexporter_log_error("History: SSL accept failed");
         goto error;
      }

      if (ssl_status == MESSAGE_STATUS_ZERO)
      {
         /*
          * Plain HTTP on a TLS port — redirect to HTTPS. Read the raw message
          * here (not via pgexporter_http_server_parse) because we need the path
          * for the redirect URL and the process exits immediately after.
          */
         struct message* redirect_msg = NULL;
         char* redirect_path = "/";
         char* base_url = NULL;

         if (pgexporter_read_timeout_message(NULL, fd, (int)pgexporter_time_convert(config->authentication_timeout, FORMAT_TIME_S), &redirect_msg) != MESSAGE_STATUS_OK)
         {
            pgexporter_log_error("History: failed to read redirect message");
            goto error;
         }

         char* path_start = strstr(redirect_msg->data, " ");
         if (path_start)
         {
            path_start++;
            char* path_end = strstr(path_start, " ");
            if (path_end)
            {
               *path_end = '\0';
               redirect_path = path_start;
            }
         }

         base_url = pgexporter_format_and_append(base_url, "https://localhost:%d%s", config->history, redirect_path);

         if (pgexporter_http_respond_redirect(NULL, fd, base_url) != MESSAGE_STATUS_OK)
         {
            pgexporter_log_error("History: failed to redirect to: %s", base_url);
            free(base_url);
            goto error;
         }

         free(base_url);
         pgexporter_close_ssl(ssl);
         pgexporter_disconnect(fd);
         pgexporter_memory_destroy();
         pgexporter_stop_logging();
         exit(0);
      }
      /* MESSAGE_STATUS_OK: TLS handshake done, proceed to parse */
   }

   if (pgexporter_http_server_parse(ssl, fd, &req) != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   path = req->path;
   query = strchr(path, '?');
   if (query != NULL)
   {
      *query = '\0';
      query++;
   }

   if (strncmp(path, "/history/", strlen("/history/")) != 0 || strlen(path) <= strlen("/history/"))
   {
      pgexporter_http_respond_404(ssl, fd);
      goto done;
   }

   memset(metric, 0, sizeof(metric));
   {
      const char* metric_name = path + strlen("/history/");
      size_t metric_len = strlen(metric_name);

      if (metric_len == 0 || metric_len >= sizeof(metric))
      {
         pgexporter_http_respond_400(ssl, fd);
         goto done;
      }

      memcpy(metric, metric_name, metric_len);
   }

   ts = time(NULL);
   duration = -3600;

   if (history_query_param(query, "timestamp", value_buf, sizeof(value_buf)))
   {
      char* endptr = NULL;
      long long parsed = strtoll(value_buf, &endptr, 10);

      if (endptr == value_buf || *endptr != '\0')
      {
         pgexporter_http_respond_400(ssl, fd);
         goto done;
      }

      ts = (time_t)parsed;
   }

   if (history_query_param(query, "duration", value_buf, sizeof(value_buf)))
   {
      char* endptr = NULL;

      duration = strtoll(value_buf, &endptr, 10);

      if (endptr == value_buf || *endptr != '\0')
      {
         pgexporter_http_respond_400(ssl, fd);
         goto done;
      }
   }

   start = ts + (duration < 0 ? (time_t)duration : 0);
   end = ts + (duration > 0 ? (time_t)duration : 0);

   if (pgexporter_history_query_range(metric, start, end, &records, &count))
   {
      pgexporter_http_respond_500(ssl, fd);
      goto done;
   }

   if (pgexporter_json_create(&root))
   {
      pgexporter_http_respond_500(ssl, fd);
      goto done;
   }

   for (int i = 0; i < count; i++)
   {
      struct json* item = NULL;

      if (pgexporter_json_create(&item))
      {
         continue;
      }

      pgexporter_json_put(item, "timestamp", (uintptr_t)records[i].ts, ValueInt64);
      pgexporter_json_put(item, "server", (uintptr_t)records[i].server, ValueString);
      pgexporter_json_put(item, "metric", (uintptr_t)records[i].metric, ValueString);

      if (records[i].labels != NULL)
      {
         pgexporter_json_put(item, "labels", (uintptr_t)records[i].labels, ValueString);
      }

      pgexporter_json_put(item, "value", pgexporter_value_from_double(records[i].value), ValueDouble);

      pgexporter_json_append(root, (uintptr_t)item, ValueJSON);
   }

   json_str = pgexporter_json_to_string(root, FORMAT_JSON, NULL, 0);

   if (json_str == NULL)
   {
      pgexporter_http_respond_500(ssl, fd);
      goto done;
   }

   pgexporter_http_respond_ok(ssl, fd, "application/json; charset=utf-8", json_str, strlen(json_str));

done:
   free(json_str);
   pgexporter_json_destroy(root);
   pgexporter_history_records_free(records, count);
   pgexporter_http_server_request_destroy(req);
   pgexporter_close_ssl(ssl);
   pgexporter_disconnect(fd);
   pgexporter_memory_destroy();
   pgexporter_stop_logging();
   exit(0);

error:
   pgexporter_http_respond_400(ssl, fd);
   pgexporter_http_server_request_destroy(req);
   pgexporter_close_ssl(ssl);
   pgexporter_disconnect(fd);
   pgexporter_memory_destroy();
   pgexporter_stop_logging();
   exit(1);
}
