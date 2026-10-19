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
#include <deque.h>
#include <history.h>
#include <history_postgresql.h>
#include <history_sqlite.h>
#include <history_timescaledb.h>
#include <http.h>
#include <http_server.h>
#include <json.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <utils.h>
#include <value.h>

/* system */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <openssl/evp.h>

/** Upper bound on the number of labels parsed from one exposition line. */
#define HISTORY_LABEL_MAX 64

/**
 * @struct history_label
 * @brief One parsed `key="value"` pair, pointing into the caller's string.
 */
struct history_label
{
   const char* key; /**< Start of the label name */
   size_t key_len;  /**< Length of the label name */
   const char* val; /**< Start of the label value, inside the quotes */
   size_t val_len;  /**< Length of the label value, escapes left intact */
};

static const struct history_backend_ops* ops = NULL;
static const struct history_store* opened = NULL;

static int
label_compare(const void* a, const void* b)
{
   const struct history_label* la = (const struct history_label*)a;
   const struct history_label* lb = (const struct history_label*)b;
   size_t n = la->key_len < lb->key_len ? la->key_len : lb->key_len;
   int c = memcmp(la->key, lb->key, n);

   if (c != 0)
   {
      return c;
   }

   if (la->key_len != lb->key_len)
   {
      return la->key_len < lb->key_len ? -1 : 1;
   }

   n = la->val_len < lb->val_len ? la->val_len : lb->val_len;
   c = memcmp(la->val, lb->val, n);

   if (c != 0)
   {
      return c;
   }

   if (la->val_len == lb->val_len)
   {
      return 0;
   }

   return la->val_len < lb->val_len ? -1 : 1;
}

/**
 * Parse a Prometheus label string into key/value pairs.
 * A string carrying more than HISTORY_LABEL_MAX pairs is reported as
 * malformed rather than truncated.
 * @param labels The label string, or NULL
 * @param out    Array of at least HISTORY_LABEL_MAX entries
 * @param count  Set to the number of pairs parsed
 * @return 0 on success, 1 if the string is not a well-formed label set
 */
static int
label_parse(const char* labels, struct history_label* out, int* count)
{
   const char* p = labels;
   int n = 0;

   *count = 0;

   if (labels == NULL)
   {
      return 0;
   }

   while (*p != '\0')
   {
      const char* key_start;
      const char* val_start;
      bool escaped = false;

      while (*p == ' ' || *p == ',')
      {
         p++;
      }

      if (*p == '\0')
      {
         break;
      }

      key_start = p;
      while (*p != '\0' && *p != '=')
      {
         p++;
      }

      if (*p != '=' || p == key_start)
      {
         goto error;
      }

      if (n == HISTORY_LABEL_MAX)
      {
         goto error;
      }

      out[n].key = key_start;
      out[n].key_len = (size_t)(p - key_start);
      p++;

      if (*p != '"')
      {
         goto error;
      }
      p++;

      val_start = p;
      while (*p != '\0')
      {
         if (!escaped && *p == '"')
         {
            break;
         }
         escaped = (*p == '\\' && !escaped);
         p++;
      }

      if (*p != '"')
      {
         goto error;
      }

      out[n].val = val_start;
      out[n].val_len = (size_t)(p - val_start);
      n++;
      p++;

      while (*p == ' ')
      {
         p++;
      }

      if (*p != '\0' && *p != ',')
      {
         goto error;
      }
   }

   *count = n;
   return 0;

error:

   *count = 0;
   return 1;
}

bool
pgexporter_history_label_find(const char* labels, const char* key, char* out, size_t out_size)
{
   struct history_label pairs[HISTORY_LABEL_MAX];
   size_t key_len;
   int count = 0;
   int i;

   if (out == NULL || out_size == 0)
   {
      return false;
   }

   out[0] = '\0';

   if (labels == NULL || key == NULL)
   {
      return false;
   }

   if (label_parse(labels, pairs, &count))
   {
      return false;
   }

   key_len = strlen(key);

   for (i = 0; i < count; i++)
   {
      if (pairs[i].key_len == key_len && memcmp(pairs[i].key, key, key_len) == 0)
      {
         size_t len = pairs[i].val_len;

         if (len >= out_size)
         {
            len = out_size - 1;
         }

         memcpy(out, pairs[i].val, len);
         out[len] = '\0';
         return true;
      }
   }

   return false;
}

int
pgexporter_history_label_hash(const char* labels, char* out)
{
   struct history_label pairs[HISTORY_LABEL_MAX];
   EVP_MD_CTX* ctx = NULL;
   unsigned char digest[EVP_MAX_MD_SIZE];
   unsigned int digest_len = 0;
   int count = 0;
   int i;

   if (out == NULL)
   {
      goto error;
   }

   out[0] = '\0';

   ctx = EVP_MD_CTX_new();
   if (ctx == NULL)
   {
      goto error;
   }

   if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (labels != NULL && label_parse(labels, pairs, &count))
   {
      /* Unrecognised label set: hash verbatim, still stable for this input */
      if (EVP_DigestUpdate(ctx, labels, strlen(labels)) != 1)
      {
         goto error;
      }
   }
   else
   {
      qsort(pairs, (size_t)count, sizeof(struct history_label), label_compare);

      for (i = 0; i < count; i++)
      {
         if (EVP_DigestUpdate(ctx, pairs[i].key, pairs[i].key_len) != 1 ||
             EVP_DigestUpdate(ctx, "=", 1) != 1 ||
             EVP_DigestUpdate(ctx, pairs[i].val, pairs[i].val_len) != 1 ||
             EVP_DigestUpdate(ctx, "\n", 1) != 1)
         {
            goto error;
         }
      }
   }

   if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1)
   {
      goto error;
   }

   if (digest_len < HISTORY_LABEL_HASH_BYTES)
   {
      goto error;
   }

   for (i = 0; i < HISTORY_LABEL_HASH_BYTES; i++)
   {
      pgexporter_snprintf(out + (i * 2), 3, "%02x", digest[i]);
   }

   EVP_MD_CTX_free(ctx);

   return 0;

error:

   if (ctx != NULL)
   {
      EVP_MD_CTX_free(ctx);
   }

   if (out != NULL)
   {
      out[0] = '\0';
   }

   return 1;
}

static const struct
{
   int id;
   const struct history_backend_ops* ops;
} backend_registry[] = {
   {HISTORY_BACKEND_SQLITE, &pgexporter_history_sqlite_ops},
   {HISTORY_BACKEND_POSTGRESQL, &pgexporter_history_postgresql_ops},
   {HISTORY_BACKEND_TIMESCALEDB, &pgexporter_history_timescaledb_ops},
};

#define BACKEND_REGISTRY_SIZE (sizeof(backend_registry) / sizeof(backend_registry[0]))

static const struct history_backend_ops*
lookup_backend(const struct history_store* store)
{
   if (store == NULL)
   {
      return NULL;
   }

   for (size_t i = 0; i < BACKEND_REGISTRY_SIZE; i++)
   {
      if (backend_registry[i].id == store->backend)
      {
         return backend_registry[i].ops;
      }
   }

   pgexporter_log_error("%s: unknown backend %d", store->name, store->backend);
   return NULL;
}

int
pgexporter_history_create(const struct history_store* store)
{
   const struct history_backend_ops* backend = lookup_backend(store);

   if (backend == NULL)
   {
      return 1;
   }

   return backend->create(store);
}

int
pgexporter_history_init(const struct history_store* store)
{
   const struct history_backend_ops* backend = lookup_backend(store);

   if (backend == NULL)
   {
      return 1;
   }

   ops = backend;
   opened = store;

   return ops->init(store);
}

int
pgexporter_history_write_batch(struct history_record* records, int count)
{
   if (ops == NULL)
   {
      return 1;
   }

   if (ops->write_batch(records, count))
   {
      return 1;
   }

   atomic_store(opened->last_store, (int64_t)time(NULL));

   return 0;
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
pgexporter_history_prune(const struct history_store* store)
{
   if (ops == NULL || store == NULL)
   {
      return 1;
   }
   return ops->prune(store);
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
   opened = NULL;
   return ret;
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

bool
pgexporter_history_claim_slot(const struct history_store* store, int interval)
{
   bool expected = false;
   time_t now = time(NULL);

   if (store == NULL || store->port == 0)
   {
      return false;
   }

   if (interval == 0)
   {
      return false;
   }

   if (!atomic_compare_exchange_strong(store->running, &expected, true))
   {
      return false;
   }

   if (interval > 0 &&
       (time_t)atomic_load(store->last_store) / interval == now / interval)
   {
      atomic_store(store->running, false);
      return false;
   }
   atomic_store(store->worker_pid, (int)getpid());

   return true;
}

void
pgexporter_history_release_slot(const struct history_store* store)
{
   if (store == NULL)
   {
      return;
   }

   atomic_store(store->worker_pid, 0);
   atomic_store(store->running, false);
}

void
pgexporter_history_tick(const struct history_store* store)
{
   pid_t pid;

   if (store == NULL)
   {
      return;
   }

   if (!pgexporter_history_claim_slot(store, pgexporter_time_convert(store->interval, FORMAT_TIME_S)))
   {
      return;
   }

   pid = fork();
   if (pid < 0)
   {
      pgexporter_log_error("%s: failed to fork ticker worker", store->name);
      pgexporter_history_release_slot(store);
      return;
   }
   else if (pid > 0)
   {
      /* claim_slot() recorded our own pid; replace it with the worker's so
       * sigchld_cb can clear the flag if the worker dies before resetting it. */
      atomic_store(store->worker_pid, (int)pid);
      return;
   }

   store->collect(store);
   pgexporter_history_release_slot(store);
   exit(0);
}

/**
 * Child-process worker that opens its own database connection and prunes
 * records older than the store's retention. Called after fork(); exit(0)s.
 */
static void
history_retention_worker(const struct history_store* store)
{
   if (pgexporter_history_init(store) != 0)
   {
      pgexporter_log_error("%s: failed to init history db for retention", store->name);
      goto child_done;
   }

   if (pgexporter_history_prune(store) != 0)
   {
      pgexporter_log_error("%s: prune failed", store->name);
   }

child_done:
   pgexporter_history_shutdown();
   atomic_store(store->retention_pid, 0);
   atomic_store(store->retention_running, false);
   exit(0);
}

void
pgexporter_history_retention_tick(const struct history_store* store)
{
   pid_t pid;
   bool expected = false;

   if (store == NULL || store->port == 0)
   {
      return;
   }

   if (!atomic_compare_exchange_strong(store->retention_running, &expected, true))
   {
      /* Previous retention worker still running */
      return;
   }

   pid = fork();
   if (pid < 0)
   {
      pgexporter_log_error("%s: failed to fork retention worker", store->name);
      atomic_store(store->retention_running, false);
      return;
   }
   else if (pid > 0)
   {
      /* Record worker pid so sigchld_cb can clear the running flag
       * if the worker dies before resetting it itself. */
      atomic_store(store->retention_pid, (int)pid);
      return;
   }

   history_retention_worker(store);
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
pgexporter_history_serve(const struct history_store* store, SSL* ssl, int fd)
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

   if (pgexporter_history_init(store))
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

         base_url = pgexporter_format_and_append(base_url, "https://localhost:%d%s", store->port, redirect_path);

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

   if (strncmp(path, "/history/", strlen("/history/")) != 0)
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

   /* Force root to be a JSON array even when 0 records are returned, so the
    * response is always "[]", never "{}" for an empty result. */
   root->type = JSONArray;
   if (pgexporter_deque_create(false, (struct deque**)&root->elements))
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
