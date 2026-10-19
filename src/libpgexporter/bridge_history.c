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
#include <bridge_history.h>
#include <deque.h>
#include <history.h>
#include <logging.h>
#include <prometheus_client.h>
#include <utils.h>

/* system */
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void bridge_history_collect(const struct history_store* store);

const struct history_store*
pgexporter_bridge_history_store(void)
{
   static struct history_store store;
   struct configuration* config = (struct configuration*)shmem;

   if (config == NULL)
   {
      return NULL;
   }

   store.name = "bridge history";
   store.port = config->bridge_history;
   store.backend = config->bridge_history_backend;
   store.path = config->bridge_history_path;
   store.pg_host = config->bridge_history_postgresql_host;
   store.pg_port = config->bridge_history_postgresql_port;
   store.pg_database = config->bridge_history_postgresql_database;
   store.pg_user = config->bridge_history_user.username;
   store.pg_password = config->bridge_history_user.password;
   store.pg_tls = config->bridge_history_postgresql_tls;
   store.pg_tls_cert_file = config->bridge_history_postgresql_tls_cert_file;
   store.pg_tls_key_file = config->bridge_history_postgresql_tls_key_file;
   store.pg_tls_ca_file = config->bridge_history_postgresql_tls_ca_file;
   store.interval = config->bridge_history_interval;
   store.retention = config->bridge_history_retention;
   store.running = &config->bridge_history_worker_running;
   store.last_store = &config->bridge_history_last_store_time;
   store.worker_pid = &config->bridge_history_worker_pid;
   store.retention_running = &config->bridge_history_retention_worker_running;
   store.retention_pid = &config->bridge_history_retention_worker_pid;
   store.collect = bridge_history_collect;

   return &store;
}

/* The bridge stores label values unescaped; restore the exposition escapes */
static char*
append_label_value(char* labels, const char* value)
{
   for (const char* p = value; p != NULL && *p != '\0'; p++)
   {
      switch (*p)
      {
         case '\\':
            labels = pgexporter_append(labels, "\\\\");
            break;
         case '"':
            labels = pgexporter_append(labels, "\\\"");
            break;
         case '\n':
            labels = pgexporter_append(labels, "\\n");
            break;
         case '\t':
            labels = pgexporter_append(labels, "\\t");
            break;
         case '\r':
            labels = pgexporter_append(labels, "\\r");
            break;
         default:
            labels = pgexporter_append_char(labels, *p);
            break;
      }
   }

   return labels;
}

int
pgexporter_bridge_history_store_metrics(struct prometheus_bridge* bridge)
{
   int capacity = 100;
   int count = 0;
   struct history_record* records = NULL;
   struct art_iterator* metrics_iterator = NULL;
   struct deque_iterator* definition_iterator = NULL;
   struct deque_iterator* attribute_iterator = NULL;
   char* labels = NULL;
   time_t now = time(NULL);
   int status = 1;

   if (bridge == NULL)
   {
      return 1;
   }

   if (bridge->metrics == NULL)
   {
      return 0;
   }

   records = malloc(sizeof(struct history_record) * capacity);
   if (records == NULL)
   {
      return 1;
   }

   if (pgexporter_art_iterator_create(bridge->metrics, &metrics_iterator))
   {
      goto cleanup;
   }

   while (pgexporter_art_iterator_next(metrics_iterator))
   {
      struct prometheus_metric* metric = (struct prometheus_metric*)metrics_iterator->value->data;

      if (pgexporter_deque_iterator_create(metric->definitions, &definition_iterator))
      {
         goto cleanup;
      }

      while (pgexporter_deque_iterator_next(definition_iterator))
      {
         struct prometheus_attributes* attrs = (struct prometheus_attributes*)definition_iterator->value->data;
         struct prometheus_value* value = (struct prometheus_value*)pgexporter_deque_peek_last(attrs->values, NULL);
         char server[MISC_LENGTH] = {0};

         if (value == NULL || value->value == NULL)
         {
            continue;
         }

         if (pgexporter_deque_iterator_create(attrs->attributes, &attribute_iterator))
         {
            goto cleanup;
         }

         while (pgexporter_deque_iterator_next(attribute_iterator))
         {
            struct prometheus_attribute* attr = (struct prometheus_attribute*)attribute_iterator->value->data;

            if (labels != NULL)
            {
               labels = pgexporter_append_char(labels, ',');
            }
            labels = pgexporter_append(labels, attr->key);
            labels = pgexporter_append(labels, "=\"");
            labels = append_label_value(labels, attr->value);
            labels = pgexporter_append_char(labels, '"');

            if (!strcmp(attr->key, "server"))
            {
               pgexporter_snprintf(server, sizeof(server), "%s", attr->value);
            }
         }

         pgexporter_deque_iterator_destroy(attribute_iterator);
         attribute_iterator = NULL;

         if (count >= capacity)
         {
            struct history_record* new_records = NULL;

            capacity *= 2;
            new_records = realloc(records, sizeof(struct history_record) * capacity);
            if (new_records == NULL)
            {
               pgexporter_log_error("bridge history: realloc failed");
               goto cleanup;
            }
            records = new_records;
         }

         memset(&records[count], 0, sizeof(struct history_record));
         records[count].ts = now;
         records[count].value = strtod(value->value, NULL);
         pgexporter_snprintf(records[count].metric, PROMETHEUS_LENGTH, "%s", metric->name);
         pgexporter_snprintf(records[count].server, MISC_LENGTH, "%s", server);
         records[count].labels = labels;
         labels = NULL;

         count++;
      }

      pgexporter_deque_iterator_destroy(definition_iterator);
      definition_iterator = NULL;
   }

   status = count > 0 ? pgexporter_history_write_batch(records, count) : 0;

cleanup:
   free(labels);
   pgexporter_deque_iterator_destroy(attribute_iterator);
   pgexporter_deque_iterator_destroy(definition_iterator);
   pgexporter_art_iterator_destroy(metrics_iterator);
   pgexporter_history_records_free(records, count);

   return status;
}

/**
 * Collect callback of the bridge store: scrapes every bridge endpoint and
 * persists the result as one bridge history snapshot.
 */
static void
bridge_history_collect(const struct history_store* store)
{
   struct configuration* config = (struct configuration*)shmem;
   struct prometheus_bridge* bridge = NULL;

   if (pgexporter_prometheus_client_create_bridge(&bridge))
   {
      pgexporter_log_error("bridge history: failed to create bridge");
      goto done;
   }

   /* A failed endpoint is left out so it shows as a gap, not a lost snapshot */
   for (int i = 0; i < config->number_of_endpoints; i++)
   {
      pgexporter_prometheus_client_get(i, bridge);
   }

   if (pgexporter_history_init(store) != 0)
   {
      pgexporter_log_error("bridge history: failed to init history db");
      goto done;
   }

   pgexporter_bridge_history_store_metrics(bridge);

done:
   pgexporter_prometheus_client_destroy_bridge(bridge);
   pgexporter_history_shutdown();
}

void
pgexporter_bridge_history_tick_cb(void)
{
   pgexporter_history_tick(pgexporter_bridge_history_store());
}

void
pgexporter_bridge_history_retention_tick_cb(void)
{
   pgexporter_history_retention_tick(pgexporter_bridge_history_store());
}

void
pgexporter_bridge_history_http(SSL* ssl, int fd)
{
   pgexporter_history_serve(pgexporter_bridge_history_store(), ssl, fd);
}
