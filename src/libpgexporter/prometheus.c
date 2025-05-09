/*
 * Copyright (C) 2025 The pgexporter community
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
#include "art.h"
#include "prometheus_client.h"
#include "value.h"
#include <openssl/crypto.h>
#include <pgexporter.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <queries.h>
#include <query_alts.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define CHUNK_SIZE 32768

#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_METRICS 2
#define BAD_REQUEST  3

#define MAX_ARR_LENGTH 256
#define NUMBER_OF_HISTOGRAM_COLUMNS 4

#define INPUT_NO   0
#define INPUT_DATA 1
#define INPUT_WAL  2

/**
 * This is a linked list of queries with the data received from the server
 * as well as the query sent to the server and other meta data.
 **/
typedef struct query_list
{
   struct query* query;
   struct query_list* next;
   struct query_alts* query_alt;
   char tag[MISC_LENGTH];
   int sort_type;
   bool error;
} query_list_t;

static int resolve_page(struct message* msg);
static int badrequest_page(SSL* client_ssl, int client_fd);
static int unknown_page(SSL* client_ssl, int client_fd);
static int home_page(SSL* client_ssl, int client_fd);
static int metrics_page(SSL* client_ssl, int client_fd);
static int bad_request(SSL* client_ssl, int client_fd);
static int redirect_page(SSL* client_ssl, int client_fd, char* path);

static bool collector_pass(const char* collector);


static void general_information(SSL* client_ssl, int client_fd);
static void core_information(SSL* client_ssl, int client_fd);
static void extension_information(SSL* client_ssl, int client_fd);
static void extension_function(SSL* client_ssl, int client_fd, char* function, int input, char* description, char* type);
static void server_information(SSL* client_ssl, int client_fd);
static void version_information(SSL* client_ssl, int client_fd);
static void uptime_information(SSL* client_ssl, int client_fd);
static void primary_information(SSL* client_ssl, int client_fd);
static void settings_information(SSL* client_ssl, int client_fd);
static void custom_metrics(SSL* client_ssl, int client_fd); // Handles custom metrics provided in YAML format, both internal and external
static void append_help_info(char** data, char* tag, char* name, char* description);
static void append_type_info(char** data, char* tag, char* name, int typeId);

static void handle_histogram(struct art* art, query_list_t* temp);
static void handle_gauge_counter(struct art* art, query_list_t* temp);

static int send_chunk(SSL* client_ssl, int client_fd, char* data);
static int parse_list(char* list_str, char** strs, int* n_strs);

static char* get_value(char* tag, char* name, char* val);
static int safe_prometheus_key_additional_length(char* key);
static char* safe_prometheus_key(char* key);
static void safe_prometheus_key_free(char* key);

static bool is_metrics_cache_configured(void);
static bool is_metrics_cache_valid(void);
static bool metrics_cache_append(char* data);
static bool metrics_cache_finalize(void);
static size_t metrics_cache_size_to_alloc(void);
static void metrics_cache_invalidate(void);

// ART related funcs

static void output_metrics_from_art(int client_fd, struct art* art, SSL* ssl);
static void cleanup_metrics_art(struct art* metrics_art);

static struct prometheus_metric*
create_metric(const char* name, const char* help, const char* type);
static struct prometheus_attributes*
create_attributes();

static void
add_attribute(struct prometheus_attributes* attrs, const char* key, const char* value);
static void
add_value(struct prometheus_attributes* attrs, const char* value);
static void
add_server_attribute(struct prometheus_attributes* attrs, const char* server_name);
static void
add_metric_data(struct prometheus_metric* metric, struct prometheus_attributes* attrs);
static void
add_metric_to_art(struct art* metrics_art, struct prometheus_metric* metric);

static void
add_simple_gauge(struct art* metrics_art, const char* name, const char* help,
                 const char* server_name, const char* value);

void
pgexporter_prometheus(SSL* client_ssl, int client_fd)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;
   if (client_ssl)
   {
      char buffer[5] = {0};

      recv(client_fd, buffer, 5, MSG_PEEK);

      if ((unsigned char)buffer[0] == 0x16 || (unsigned char)buffer[0] == 0x80) // SSL/TLS request
      {
         if (SSL_accept(client_ssl) <= 0)
         {
            pgexporter_log_error("Failed to accept SSL connection");
            goto error;
         }
      }
      else
      {
         char* path = "/";
         char* base_url = NULL;

         if (pgexporter_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg) != MESSAGE_STATUS_OK)
         {
            pgexporter_log_error("Failed to read message");
            goto error;
         }

         char* path_start = strstr(msg->data, " ");
         if (path_start)
         {
            path_start++;
            char* path_end = strstr(path_start, " ");
            if (path_end)
            {
               *path_end = '\0';
               path = path_start;
            }
         }

         base_url = pgexporter_format_and_append(base_url, "https://localhost:%d%s", config->metrics, path);

         if (redirect_page(NULL, client_fd, base_url) != MESSAGE_STATUS_OK)
         {
            pgexporter_log_error("Failed to redirect to: %s", base_url);
            free(base_url);
            goto error;
         }

         pgexporter_close_ssl(client_ssl);
         pgexporter_disconnect(client_fd);

         pgexporter_memory_destroy();
         pgexporter_stop_logging();

         free(base_url);

         exit(0);
      }
   }
   status = pgexporter_read_timeout_message(client_ssl, client_fd, config->authentication_timeout, &msg);


   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      home_page(client_ssl, client_fd);
   }
   else if (page == PAGE_METRICS)
   {
      metrics_page(client_ssl, client_fd);
   }
   else if (page == PAGE_UNKNOWN)
   {
      unknown_page(client_ssl, client_fd);
   }
   else
   {
      bad_request(client_ssl, client_fd);
   }

   pgexporter_close_ssl(client_ssl);
   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);

error:

   badrequest_page(client_ssl, client_fd);

   pgexporter_close_ssl(client_ssl);
   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(1);
}

void
pgexporter_prometheus_reset(void)
{
   signed char cache_is_free;
   struct configuration* config;
   struct prometheus_cache* cache;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)prometheus_cache_shmem;

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      metrics_cache_invalidate();

      atomic_store(&config->logging_info, 0);
      atomic_store(&config->logging_warn, 0);
      atomic_store(&config->logging_error, 0);
      atomic_store(&config->logging_fatal, 0);

      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      /* Sleep for 1ms */
      SLEEP_AND_GOTO(1000000L, retry_cache_locking);
   }
}

void
pgexporter_prometheus_logging(int type)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   switch (type)
   {
      case PGEXPORTER_LOGGING_LEVEL_INFO:
         atomic_fetch_add(&config->logging_info, 1);
         break;
      case PGEXPORTER_LOGGING_LEVEL_WARN:
         atomic_fetch_add(&config->logging_warn, 1);
         break;
      case PGEXPORTER_LOGGING_LEVEL_ERROR:
         atomic_fetch_add(&config->logging_error, 1);
         break;
      case PGEXPORTER_LOGGING_LEVEL_FATAL:
         atomic_fetch_add(&config->logging_fatal, 1);
         break;
      default:
         break;
   }
}

static int
redirect_page(SSL* client_ssl, int client_fd, char* path)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgexporter_append(data, "HTTP/1.1 301 Moved Permanently\r\n");
   data = pgexporter_append(data, "Location: ");
   data = pgexporter_append(data, path);
   data = pgexporter_append(data, "\r\n");
   data = pgexporter_append(data, "Date: ");
   data = pgexporter_append(data, &time_buf[0]);
   data = pgexporter_append(data, "\r\n");
   data = pgexporter_append(data, "Content-Length: 0\r\n");
   data = pgexporter_append(data, "Connection: close\r\n");
   data = pgexporter_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);

   free(data);

   return status;
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (msg->length < 3 || strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgexporter_log_debug("Prometheus: Not a GET request");
      return BAD_REQUEST;
   }

   index = 4;
   from = (char*)msg->data + index;

   while (pgexporter_read_byte(msg->data + index) != ' ')
   {
      index++;
   }

   pgexporter_write_byte(msg->data + index, '\0');

   if (strcmp(from, "/") == 0 || strcmp(from, "/index.html") == 0)
   {
      return PAGE_HOME;
   }
   else if (strcmp(from, "/metrics") == 0)
   {
      return PAGE_METRICS;
   }

   return PAGE_UNKNOWN;
}

static int
badrequest_page(SSL* client_ssl, int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgexporter_vappend(data, 4,
                             "HTTP/1.1 400 Bad Request\r\n",
                             "Date: ",
                             &time_buf[0],
                             "\r\n"
                             );

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);

   free(data);

   return status;
}

static int
unknown_page(SSL* client_ssl, int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgexporter_vappend(data, 4,
                             "HTTP/1.1 403 Forbidden\r\n",
                             "Date: ",
                             &time_buf[0],
                             "\r\n"
                             );

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);

   free(data);

   return status;
}

static int
home_page(SSL* client_ssl, int client_fd)
{
   char* data = NULL;
   int status;
   time_t now;
   char time_buf[32];
   struct message msg;
   struct configuration* config;

   config = (struct configuration*) shmem;

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgexporter_vappend(data, 7,
                             "HTTP/1.1 200 OK\r\n",
                             "Content-Type: text/html; charset=utf-8\r\n",
                             "Date: ",
                             &time_buf[0],
                             "\r\n",
                             "Transfer-Encoding: chunked\r\n",
                             "\r\n"
                             );

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);
   data = NULL;

   data = pgexporter_vappend(data, 12,
                             "<html>\n",
                             "<head>\n",
                             "  <title>pgexporter</title>\n",
                             "</head>\n",
                             "<body>\n",
                             "  <h1>pgexporter</h1>\n",
                             "  Prometheus exporter for PostgreSQL\n",
                             "  <p>\n",
                             "  <a href=\"/metrics\">Metrics</a>\n",
                             "  <p>\n",
                             "  Support for\n",
                             "  <ul>\n"
                             );

   send_chunk(client_ssl, client_fd, data);
   free(data);
   data = NULL;

   data = pgexporter_vappend(data, 4,
                             "  <li>pgexporter_logging_info</li>\n",
                             "  <li>pgexporter_logging_warn</li>\n",
                             "  <li>pgexporter_logging_error</li>\n",
                             "  <li>pgexporter_logging_fatal</li>\n"
                             );

   send_chunk(client_ssl, client_fd, data);
   free(data);
   data = NULL;

   if (config->number_of_metrics == 0)
   {
      data = pgexporter_vappend(data, 7,
                                "  <li>pg_database</li>\n",
                                "  <li>pg_locks</li>\n",
                                "  <li>pg_replication_slots</li>\n",
                                "  <li>pg_settings</li>\n",
                                "  <li>pg_stat_bgwriter</li>\n",
                                "  <li>pg_stat_database</li>\n",
                                "  <li>pg_stat_database_conflicts</li>\n"
                                );
   }
   else
   {
      for (int i = 0; i < config->number_of_metrics; i++)
      {
         data = pgexporter_vappend(data, 3,
                                   "  <li>",
                                   config->prometheus[i].tag,
                                   "</li>\n"
                                   );
      }
   }

   send_chunk(client_ssl, client_fd, data);
   free(data);
   data = NULL;

   data = pgexporter_vappend(data, 5,
                             "  </ul>\n",
                             "  <p>\n",
                             "  <a href=\"https://pgexporter.github.io/\">pgexporter.github.io/</a>\n",
                             "</body>\n",
                             "</html>\n"
                             );

   /* Footer */
   data = pgexporter_append(data, "\r\n\r\n");

   send_chunk(client_ssl, client_fd, data);
   free(data);
   data = NULL;

   return 0;

error:

   free(data);

   return 1;
}

static int
metrics_page(SSL* client_ssl, int client_fd)
{
   char* data = NULL;
   time_t start_time;
   int dt;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;
   struct prometheus_cache* cache;
   signed char cache_is_free;
   struct configuration* config;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   memset(&msg, 0, sizeof(struct message));

   start_time = time(NULL);

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      // can serve the message out of cache?
      if (is_metrics_cache_configured() && is_metrics_cache_valid())
      {
         // serve the message directly out of the cache
         pgexporter_log_debug("Serving metrics out of cache (%d/%d bytes valid until %lld)",
                              strlen(cache->data),
                              cache->size,
                              cache->valid_until);

         msg.kind = 0;
         msg.length = strlen(cache->data);
         msg.data = cache->data;

         status = pgexporter_write_message(client_ssl, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      else
      {
         // build the message without the cache
         metrics_cache_invalidate();

         now = time(NULL);

         memset(&time_buf, 0, sizeof(time_buf));
         ctime_r(&now, &time_buf[0]);
         time_buf[strlen(time_buf) - 1] = 0;

         data = pgexporter_vappend(data, 5,
                                   "HTTP/1.1 200 OK\r\n",
                                   "Content-Type: text/plain; version=0.0.1; charset=utf-8\r\n",
                                   "Date: ",
                                   &time_buf[0],
                                   "\r\n"
                                   );
         metrics_cache_append(data);  // cache here to avoid the chunking for the cache
         data = pgexporter_vappend(data, 2,
                                   "Transfer-Encoding: chunked\r\n",
                                   "\r\n"
                                   );

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgexporter_write_message(client_ssl, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         free(data);
         data = NULL;

         pgexporter_open_connections();

         /* General Metric Collector */
         general_information(client_ssl, client_fd);
         core_information(client_ssl, client_fd);
         server_information(client_ssl, client_fd);
         version_information(client_ssl, client_fd);
         uptime_information(client_ssl, client_fd);
         primary_information(client_ssl, client_fd);
         settings_information(client_ssl, client_fd);
         extension_information(client_ssl, client_fd);

         custom_metrics(client_ssl, client_fd);

         pgexporter_close_connections();

         /* Footer */
         data = pgexporter_append(data, "0\r\n\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgexporter_write_message(client_ssl, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         metrics_cache_finalize();
      }

      // free the cache
      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      dt = (int)difftime(time(NULL), start_time);
      if (dt >= (config->blocking_timeout > 0 ? config->blocking_timeout : 30))
      {
         goto error;
      }

      /* Sleep for 10ms */
      SLEEP_AND_GOTO(10000000L, retry_cache_locking);
   }

   free(data);

   return 0;

error:
   free(data);

   return 1;
}

static int
bad_request(SSL* client_ssl, int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   data = pgexporter_vappend(data, 4,
                             "HTTP/1.1 400 Bad Request\r\n",
                             "Date: ",
                             &time_buf[0],
                             "\r\n"
                             );

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);

   free(data);

   return status;
}

static bool
collector_pass(const char* collector)
{
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   if (config->number_of_collectors == 0)
   {
      return true;
   }

   for (int i = 0; i < config->number_of_collectors; i++)
   {
      if (!strcmp(config->collectors[i], collector))
      {
         return true;
      }
   }

   return false;
}

static void
general_information(SSL* client_ssl, int client_fd)
{
   struct configuration* config;
   struct art* metrics_art = NULL;
   char value_buffer[32];

   config = (struct configuration*)shmem;

   /* Create ART for this category */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for general information metrics");
      return;
     
   }

   /* pgexporter_state */
   add_simple_gauge(metrics_art, "pgexporter_state", "The state of pgexporter",
                    "pgexporter", "1");

   /* pgexporter_logging_info */
   sprintf(value_buffer, "%ld", atomic_load(&config->logging_info));
   add_simple_gauge(metrics_art, "pgexporter_logging_info",
                    "The number of INFO logging statements",
                    "pgexporter", value_buffer);

   /* pgexporter_logging_warn */
   sprintf(value_buffer, "%ld", atomic_load(&config->logging_warn));
   add_simple_gauge(metrics_art, "pgexporter_logging_warn",
                    "The number of WARN logging statements",
                    "pgexporter", value_buffer);

   /* pgexporter_logging_error */
   sprintf(value_buffer, "%ld", atomic_load(&config->logging_error));
   add_simple_gauge(metrics_art, "pgexporter_logging_error",
                    "The number of ERROR logging statements",
                    "pgexporter", value_buffer);

   /* pgexporter_logging_fatal */
   sprintf(value_buffer, "%ld", atomic_load(&config->logging_fatal));
   add_simple_gauge(metrics_art, "pgexporter_logging_fatal",
                    "The number of FATAL logging statements",
                    "pgexporter", value_buffer);

   /* Output metrics and clean up */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);
   cleanup_metrics_art(metrics_art);
}

static void
server_information(SSL* client_ssl, int client_fd)
{
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;

   /* Create ART for this category */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for server information metrics");
      return;
   }

   /* Create a single metric for "pgexporter_postgresql_active" */
   struct prometheus_metric* metric =
      create_metric("pgexporter_postgresql_active",
                    "The state of PostgreSQL",
                    "gauge");
   if (metric == NULL)
   {
      pgexporter_log_error("Failed to allocate metric for server information");
      return;
   }

   /*
    * For each configured server, add an attribute showing
    * which server name it is and whether it's active (fd != -1).
    */
   for (int srv = 0; srv < config->number_of_servers; srv++)
   {
      struct prometheus_attributes* attrs = create_attributes();
      if (attrs == NULL)
      {
         pgexporter_log_error("Failed to allocate attributes for server %d", srv);
         continue;
      }

      /*
       * The "server" attribute is the server name,
       * and if the fd != -1, then the server is considered active (1),
       * otherwise it's inactive (0).
       */
      add_server_attribute(attrs, config->servers[srv].name);

      char value_str[4] = {0};
      if (config->servers[srv].fd != -1)
      {
         strcpy(value_str, "1");
      }
      else
      {
         strcpy(value_str, "0");
      }

      add_value(attrs, value_str);
      add_metric_data(metric, attrs);
   }

   /* Add the metric to the ART */
   add_metric_to_art(metrics_art, metric);

   /* Output metrics and clean up */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);
   cleanup_metrics_art(metrics_art);
}

static void
version_information(SSL* client_ssl, int client_fd)
{
   int ret;
   int server_idx;
   char* safe_key1 = NULL;
   char* safe_key2 = NULL;
   struct query* all = NULL;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;
   struct prometheus_metric* metric = NULL;

   /* Create ART for this metric category */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for version information metrics");
      return;
   }

   /* Collect version info for each configured server */
   for (server_idx = 0; server_idx < config->number_of_servers; server_idx++)
   {
      if (config->servers[server_idx].fd != -1)
      {
         ret = pgexporter_query_version(server_idx, &query);
         if (ret == 0 && query != NULL)
         {
            all = pgexporter_merge_queries(all, query, SORT_NAME);
         }
         query = NULL;
      }
   }

   if (all != NULL && all->tuples != NULL)
   {
      /* Create a new metric: pgexporter_postgresql_version */
      metric = create_metric("pgexporter_postgresql_version",
                             "The PostgreSQL version",
                             "gauge");
      if (!metric)
      {
         pgexporter_log_error("Failed to create version metric");
         pgexporter_free_query(all);
         cleanup_metrics_art(metrics_art);
         return;
      }

      current = all->tuples;
      server_idx = 0;

      while (current != NULL)
      {
         /* Each tuple has at least these columns:
          * 0 -> full version string
          * 1 -> minor version
          */
         struct prometheus_attributes* attrs = create_attributes();
         if (attrs == NULL)
         {
            pgexporter_log_error("Failed to create attributes for version info");
            break;
         }

         /* Server attribute */
         add_server_attribute(attrs, config->servers[current->server].name);

         /* version attribute */
         safe_key1 = safe_prometheus_key(pgexporter_get_column(0, current));
         add_attribute(attrs, "version", safe_key1);
         safe_prometheus_key_free(safe_key1);

         /* minor_version attribute */
         safe_key2 = safe_prometheus_key(pgexporter_get_column(1, current));
         add_attribute(attrs, "minor_version", safe_key2);
         safe_prometheus_key_free(safe_key2);

         /* Add a constant "1" value - we measure that this server has a particular version. */
         add_value(attrs, "1");

         /* Attach these attributes to our metric */
         add_metric_data(metric, attrs);

         current = current->next;
         server_idx++;
      }

      /* Finally, add the metric to our ART */
      add_metric_to_art(metrics_art, metric);
   }

   /* Send the metrics to the client and clean up */
   output_metrics_from_art(client_fd, metrics_art,client_ssl);
   pgexporter_free_query(all);
   cleanup_metrics_art(metrics_art);
}

static void
uptime_information(SSL* client_ssl, int client_fd)
{
   int ret;
   int server_idx;
   char* safe_key = NULL;
   struct query* all = NULL;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;
   struct prometheus_metric* metric = NULL;

   /* Create an ART for uptime metrics */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for uptime information metrics");
      return;
   }

   /*
    * Query uptime for each server that is currently connected
    */
   for (server_idx = 0; server_idx < config->number_of_servers; server_idx++)
   {
      if (config->servers[server_idx].fd != -1)
      {
         ret = pgexporter_query_uptime(server_idx, &query);
         if (ret == 0 && query != NULL)
         {
            all = pgexporter_merge_queries(all, query, SORT_NAME);
         }
         query = NULL; /* the merge took ownership of query */
      }
   }

   /*
    * The merged query 'all' contains tuples for each connected server
    * showing how long PostgreSQL has been up.
    */
   if (all != NULL && all->tuples != NULL)
   {
      /* Create a new metric for uptime */
      metric = create_metric("pgexporter_postgresql_uptime",
                             "The PostgreSQL uptime in seconds",
                             "counter");
      if (!metric)
      {
         pgexporter_log_error("Failed to create uptime metric");
         pgexporter_free_query(all);
         cleanup_metrics_art(metrics_art);
         return;
      }

      current = all->tuples;

      while (current != NULL)
      {
         struct prometheus_attributes* attrs = create_attributes();
         if (attrs == NULL)
         {
            pgexporter_log_error("Failed to allocate attributes for uptime");
            break;
         }

         /* "server" attribute for each server */
         add_server_attribute(attrs, config->servers[current->server].name);

         /*
          * The actual uptime value is in the first column (index 0).
          * We parse it or just treat it as a string.
          */
         safe_key = safe_prometheus_key(pgexporter_get_column(0, current));
         add_value(attrs, safe_key);
         safe_prometheus_key_free(safe_key);

         /* Add the attributes to the metric */
         add_metric_data(metric, attrs);

         current = current->next;
      }

      /* Finally, add this metric to the ART */
      add_metric_to_art(metrics_art, metric);
   }

   /* Output metrics to the client, then clean up */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);
   pgexporter_free_query(all);
   cleanup_metrics_art(metrics_art);
}

static void
primary_information(SSL* client_ssl, int client_fd)
{
   int ret;
   int server_idx;
   struct query* all = NULL;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;
   struct prometheus_metric* metric = NULL;

   /* Create ART for this category */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for primary information metrics");
      return;
   }

   /* Gather "primary" info from all active servers */
   for (server_idx = 0; server_idx < config->number_of_servers; server_idx++)
   {
      if (config->servers[server_idx].fd != -1)
      {
         ret = pgexporter_query_primary(server_idx, &query);
         if (ret == 0 && query != NULL)
         {
            all = pgexporter_merge_queries(all, query, SORT_NAME);
         }
         query = NULL;
      }
   }

   if (all != NULL && all->tuples != NULL)
   {
      /* Create the metric: pgexporter_postgresql_primary */
      metric = create_metric("pgexporter_postgresql_primary",
                             "Is the PostgreSQL instance the primary",
                             "gauge");
      if (metric == NULL)
      {
         pgexporter_log_error("Failed to create primary metric");
         pgexporter_free_query(all);
         cleanup_metrics_art(metrics_art);
         return;
      }

      current = all->tuples;
      while (current != NULL)
      {
         /* For each tuple, create a new prometheus_attributes block */
         struct prometheus_attributes* attrs = create_attributes();
         if (attrs == NULL)
         {
            pgexporter_log_error("Failed to allocate attributes for primary metric");
            break;
         }

         /* Add the server attribute (the server name) */
         add_server_attribute(attrs, config->servers[current->server].name);

         /*
          * The first column indicates primary status:
          * "t" => primary (1), otherwise (0).
          */
         if (!strcmp("t", pgexporter_get_column(0, current)))
         {
            add_value(attrs, "1");
         }
         else
         {
            add_value(attrs, "0");
         }

         /* Attach the attribute set to our metric */
         add_metric_data(metric, attrs);

         current = current->next;
      }

      /* Add the metric to the ART */
      add_metric_to_art(metrics_art, metric);
   }

   /* Output metrics and clean up */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);
   pgexporter_free_query(all);
   cleanup_metrics_art(metrics_art);
}

static void
core_information(SSL* client_ssl, int client_fd)
{
   struct art* metrics_art = NULL;

   /* Create an ART for core information metrics */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for core information metrics");
      return;
   }

   /*
    * Create a single metric "pgexporter_version".
    * We use "counter" here, but you can choose "gauge" if preferable.
    */
   struct prometheus_metric* metric =
      create_metric("pgexporter_version",
                    "The pgexporter version",
                    "counter");

   if (metric == NULL)
   {
      pgexporter_log_error("Failed to allocate metric for pgexporter version");
      cleanup_metrics_art(metrics_art);
      return;
   }

   /*
    * Create attributes. We'll store the pgexporter version
    * in a label named "pgexporter_version", and a value of "1"
    * for the metric itself.
    */
   struct prometheus_attributes* attrs = create_attributes();
   if (attrs == NULL)
   {
      pgexporter_log_error("Failed to allocate attributes for core information");
      free(metric->name);
      free(metric->help);
      free(metric->type);
      free(metric);
      cleanup_metrics_art(metrics_art);
      return;
   }

   /*
    * Add label: pgexporter_version="VERSION"
    * where VERSION is a compile-time constant from the build system
    * or a #define in pgexporter.h
    */
   add_attribute(attrs, "pgexporter_version", VERSION);

   /* Add a simple numeric value */
   add_value(attrs, "1");

   /* Attach these attributes to our metric */
   add_metric_data(metric, attrs);

   /* Now add the metric to the ART */
   add_metric_to_art(metrics_art, metric);

   /* Output all the metrics in the ART and clean up */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);
   cleanup_metrics_art(metrics_art);
}

static void
extension_information(SSL* client_ssl, int client_fd)
{
   bool cont = true;
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;
   struct query* query = NULL;
   struct tuple* tuple = NULL;

   /* Expose only if collector is default or specifically requested. */
   if (!collector_pass("extension"))
   {
      pgexporter_log_debug("extension_information disabled");
      return;
   }

   /* Create the ART for extension information metrics */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for extension information metrics");
      return;
   }

   /*
    * For each configured server where extension is enabled,
    * try to retrieve and parse the list of extension functions
    * and gather their metrics.
    */
   for (int server = 0; cont && server < config->number_of_servers; server++)
   {
      /* Only proceed if this server is flagged for extension support and has an open fd */
      if (config->servers[server].extension && config->servers[server].fd != -1)
      {
         /* Query which extension-related functions are available */
         pgexporter_query_get_functions(server, &query);
         if (query == NULL)
         {
            /* If no query data is returned, disable extension for this server and continue */
            config->servers[server].extension = false;
            pgexporter_log_trace("extension_information disabled for server %d", server);
            continue;
         }

         tuple = query->tuples;
         while (tuple != NULL)
         {
            /*
             * Each tuple has:
             *   tuple->data[0]: function name
             *   tuple->data[1]: "f"/"t" (is location argument needed or not)
             *   tuple->data[2]: function description
             *   tuple->data[3]: function type (gauge, counter, etc.)
             *
             * If location-argument is "f", it means no data/wal argument needed.
             * If location-argument is "t", we handle data & wal paths.
             */

            if (!strcmp(tuple->data[1], "f") || !strcmp(tuple->data[1], "false"))
            {
               /*
                * The function does NOT need data/wal arguments. Report (function_name){column_attributes}=1
                *
                * We'll create a metric whose name is the function name, help is from tuple->data[2], etc.
                */
               struct prometheus_metric* metric =
                  create_metric(tuple->data[0],
                                tuple->data[2],
                                tuple->data[3]); /* usually gauge/counter/histogram */

               if (metric != NULL)
               {
                  struct prometheus_attributes* attrs = create_attributes();
                  if (attrs != NULL)
                  {
                     /* server attribute */
                     add_server_attribute(attrs, config->servers[server].name);

                     /* The function name is the metric name, so just store "1" as value. */
                     add_value(attrs, "1");

                     /* Add the data to the metric */
                     add_metric_data(metric, attrs);

                     /* Finally, add the metric to the ART */
                     add_metric_to_art(metrics_art, metric);
                  }
                  else
                  {
                     free(metric->name);
                     free(metric->help);
                     free(metric->type);
                     free(metric);
                  }
               }
            }
            else
            {
               /*
                * If "t", this extension function expects a location argument for data or wal.
                * We'll build two metrics: one for data, one for wal (if set).
                *
                * For each query, it will produce one or more rows showing usage.
                * In a simpler approach, we just create two new metrics: function_data, function_wal
                * with additional "location" label = config->servers[server].data or .wal
                *
                * We'll create functionName_data and functionName_wal, each with "server" and "location" attributes.
                */
               char metric_data_name[256];
               char metric_wal_name[256];

               memset(metric_data_name, 0, sizeof(metric_data_name));
               memset(metric_wal_name, 0, sizeof(metric_wal_name));

               /* function_data / function_wal */
               snprintf(metric_data_name, sizeof(metric_data_name),
                        "%s_data", tuple->data[0]);
               snprintf(metric_wal_name, sizeof(metric_wal_name),
                        "%s_wal", tuple->data[0]);

               /* 1) data metric */
               if (strlen(config->servers[server].data) > 0)
               {
                  struct prometheus_metric* metric_data_loc =
                     create_metric(metric_data_name,
                                   tuple->data[2],
                                   tuple->data[3]);
                  if (metric_data_loc != NULL)
                  {
                     struct prometheus_attributes* attrs = create_attributes();
                     if (attrs != NULL)
                     {
                        /* server attribute */
                        add_server_attribute(attrs, config->servers[server].name);
                        /* location attribute */
                        add_attribute(attrs, "location", config->servers[server].data);

                        /* store the function’s returned value or just "1" if you want a presence metric */
                        add_value(attrs, "1");
                        add_metric_data(metric_data_loc, attrs);

                        add_metric_to_art(metrics_art, metric_data_loc);
                     }
                     else
                     {
                        free(metric_data_loc->name);
                        free(metric_data_loc->help);
                        free(metric_data_loc->type);
                        free(metric_data_loc);
                     }
                  }
               }

               /* 2) wal metric */
               if (strlen(config->servers[server].wal) > 0)
               {
                  struct prometheus_metric* metric_wal_loc =
                     create_metric(metric_wal_name,
                                   tuple->data[2],
                                   tuple->data[3]);
                  if (metric_wal_loc != NULL)
                  {
                     struct prometheus_attributes* attrs = create_attributes();
                     if (attrs != NULL)
                     {
                        /* server attribute */
                        add_server_attribute(attrs, config->servers[server].name);
                        /* location attribute */
                        add_attribute(attrs, "location", config->servers[server].wal);

                        /* store the function’s returned value or just "1" if you prefer */
                        add_value(attrs, "1");
                        add_metric_data(metric_wal_loc, attrs);

                        add_metric_to_art(metrics_art, metric_wal_loc);
                     }
                     else
                     {
                        free(metric_wal_loc->name);
                        free(metric_wal_loc->help);
                        free(metric_wal_loc->type);
                        free(metric_wal_loc);
                     }
                  }
               }
            }

            tuple = tuple->next;
         } /* end while (tuple) */

         pgexporter_free_query(query);
         query = NULL;
         cont = false;
      }
   }

   /* Output the metrics from the ART and clean up */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);
   cleanup_metrics_art(metrics_art);
}

static void
settings_information(SSL* client_ssl, int client_fd)
{
   int ret;
   struct query* all = NULL;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;

   /*
    * Only collect these metrics if the "settings" collector
    * is not explicitly disabled. If the user wants it disabled,
    * collector_pass("settings") will return false.
    */
   if (!collector_pass("settings"))
   {
      pgexporter_log_debug("settings_information disabled");
      return;
   }

   /* Create an ART for settings metrics */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for settings information metrics");
      return;
   }

   /*
    * Query settings for each connected server
    */
   for (int srv = 0; srv < config->number_of_servers; srv++)
   {
      if (config->servers[srv].fd != -1)
      {
         ret = pgexporter_query_settings(srv, &query);
         if (ret == 0 && query != NULL)
         {
            all = pgexporter_merge_queries(all, query, SORT_DATA0);
         }
         query = NULL;
      }
   }

   /*
    * Now 'all' holds the merged list of settings from each server.
    * Each row typically has:
    *   Column 0: setting name
    *   Column 1: setting value
    *   Column 2: setting short description
    */
   if (all != NULL && all->tuples != NULL)
   {
      current = all->tuples;

      while (current != NULL)
      {
         /* safe_prometheus_key can sanitize any special characters. */
         char* safe_key = safe_prometheus_key(pgexporter_get_column(0, current));

         /*
          * Construct a metric name:
          *   "pgexporter_<TAG>_<SETTING_NAME>"
          * The 'all->tag' might be "settings" or similar, set by the query.
          */
         char metric_name[256];
         memset(metric_name, 0, sizeof(metric_name));
         if (strlen(all->tag) > 0)
         {
            snprintf(metric_name, sizeof(metric_name),
                     "pgexporter_%s_%s",
                     all->tag,
                     safe_key);
         }
         else
         {
            snprintf(metric_name, sizeof(metric_name),
                     "pgexporter_settings_%s",
                     safe_key);
         }

         /*
          * Find an existing metric in the ART or create a new one.
          * We store each distinct setting name in a distinct metric.
          */
         uintptr_t existing = pgexporter_art_search(metrics_art, metric_name);
         struct prometheus_metric* metric = NULL;

         if (existing != 0)
         {
            metric = (struct prometheus_metric*)existing;
         }
         else
         {
            /* Create a new metric for this setting */
            metric = create_metric(
               metric_name,
               /* The short description is in column 2: */
               pgexporter_get_column(2, current),
               "gauge"
               );
            if (!metric)
            {
               pgexporter_log_error("Failed to create settings metric: %s", metric_name);
               safe_prometheus_key_free(safe_key);
               break;
            }
            add_metric_to_art(metrics_art, metric);
         }

         /*
          * For each server that has the same setting name,
          * create new attributes. The loop collects them.
          * We'll combine contiguous rows with the same setting name.
          */
         while (current != NULL)
         {
            char* current_name = pgexporter_get_column(0, current);
            if (strcmp(current_name, pgexporter_get_column(0, current->next)) == 0)
            {
               /*
                * If the next row is the same setting name, we'll handle it here
                * so we can group them. But we must also check boundary conditions.
                */
            }

            /*
             * Make attributes object.
             * We'll store server="..." plus the numeric value.
             */
            struct prometheus_attributes* attrs = create_attributes();
            if (!attrs)
            {
               pgexporter_log_error("Failed to allocate attributes for setting %s", current_name);
               break;
            }

            /* Add the server attribute */
            add_server_attribute(attrs, config->servers[current->server].name);

            /*
             * Convert the setting value to an appropriate numeric string
             * in case it is boolean or something else:
             */
            char* val = get_value(all->tag,
                                  pgexporter_get_column(0, current),
                                  pgexporter_get_column(1, current));

            /* Add the numeric string to our attributes */
            add_value(attrs, val);

            /* Attach the attributes to the metric */
            add_metric_data(metric, attrs);

            /*
             * Move to the next row, but if it’s a different setting name,
             * we break from the inner loop to handle a new metric.
             */
            if (!current->next ||
                strcmp(pgexporter_get_column(0, current),
                       pgexporter_get_column(0, current->next)) != 0)
            {
               break;
            }
            current = current->next;
         }

         safe_prometheus_key_free(safe_key);
         current = current->next;
      }
   }

   /* Send the metrics to the client */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);

   /* Clean up */
   pgexporter_free_query(all);
   cleanup_metrics_art(metrics_art);
}

static void
custom_metrics(SSL* client_ssl, int client_fd)
{
   struct configuration* config = (struct configuration*)shmem;
   struct art* metrics_art = NULL;
   query_list_t* q_list = NULL;
   query_list_t* temp = NULL;

   /*
    * Create an ART for storing all custom metrics.
    */
   if (pgexporter_art_create(&metrics_art))
   {
      pgexporter_log_error("Failed to create ART for custom metrics");
      return;
   }

   /*
    * We'll gather queries for each custom metric in this linked list (q_list).
    * For each "prometheus" metric configured (config->prometheus[i]),
    * we find the servers that match and run queries, storing results.
    */
   for (int i = 0; i < config->number_of_metrics; i++)
   {
      struct prometheus* prom = &config->prometheus[i];

      /* Expose only if default or specified via collector_pass(). */
      if (!collector_pass(prom->collector))
      {
         continue;
      }

      /* For each server, see if we run the intended query. */
      for (int srv_idx = 0; srv_idx < config->number_of_servers; srv_idx++)
      {
         if (config->servers[srv_idx].fd == -1)
         {
            /* not connected, skip */
            continue;
         }

         /* primary vs replica check if required by the config. */
         if ((prom->server_query_type == SERVER_QUERY_PRIMARY &&
              config->servers[srv_idx].state != SERVER_PRIMARY) ||
             (prom->server_query_type == SERVER_QUERY_REPLICA &&
              config->servers[srv_idx].state != SERVER_REPLICA))
         {
            /* skip servers that don't match the requested type */
            continue;
         }

         /* Find the correct query variant for this server */
         struct query_alts* query_alt = pgexporter_get_query_alt(prom->root, srv_idx);
         if (!query_alt)
         {
            continue;
         }

         /* Allocate a new node in our linked list for storing query results. */
         query_list_t* node = malloc(sizeof(query_list_t));
         memset(node, 0, sizeof(query_list_t));

         /* We'll store the query alt pointer and the tag from the config. */
         memcpy(node->tag, prom->tag, MISC_LENGTH);
         node->query_alt = query_alt;
         node->sort_type = prom->sort_type;

         /* Actually run the query. For histogram or gauge/counter. */
         if (query_alt->is_histogram)
         {
            node->error = pgexporter_custom_query(
               srv_idx,
               query_alt->query,
               prom->tag,
               -1,
               NULL,
               &node->query);
         }
         else
         {
            /* Prepare column names for the custom query. */
            char** names = malloc(query_alt->n_columns * sizeof(char*));
            for (int j = 0; j < query_alt->n_columns; j++)
            {
               names[j] = query_alt->columns[j].name;
            }

            node->error = pgexporter_custom_query(
               srv_idx,
               query_alt->query,
               prom->tag,
               query_alt->n_columns,
               names,
               &node->query);

            free(names);
         }

         /* Append this node to our linked list q_list. */
         if (!q_list)
         {
            q_list = node;
            temp = q_list;
         }
         else
         {
            temp->next = node;
            temp = node;
         }
      }
   }

   /*
    * Now we have a linked list of query results (q_list). We'll process them:
    * For each result, either handle it as a histogram or as gauge/counter
    * and store data in metrics_art.
    */
   temp = q_list;
   while (temp)
   {
      if (temp->error == 0 && temp->query != NULL && temp->query->tuples != NULL)
      {
         if (temp->query_alt->is_histogram)
         {
            handle_histogram(metrics_art, temp);
         }
         else
         {
            handle_gauge_counter(metrics_art, temp);
         }
      }
      temp = temp->next;
   }

   /*
    * We've populated the ART with metrics inside handle_histogram / handle_gauge_counter.
    * Now just output all metrics in one pass using our common helper functions,
    * then clean up everything.
    */
   output_metrics_from_art(client_fd, metrics_art, client_ssl);

   /*
    * Free the queries in q_list, then free the list itself.
    */
   temp = q_list;
   query_list_t* last = NULL;
   while (temp)
   {
      pgexporter_free_query(temp->query);
      last = temp;
      temp = temp->next;
      free(last);
   }
   q_list = NULL;

   /* Finally, free the ART structure. */
   cleanup_metrics_art(metrics_art);
}

static void
handle_gauge_counter(struct art* metrics_art, query_list_t* temp)
{
   struct configuration* config;
   struct prometheus_metric* metric = NULL;
   struct prometheus_attributes* attrs = NULL;
   struct prometheus_attribute* attr = NULL;
   struct prometheus_value* val = NULL;
   char metric_name[256];

   config = (struct configuration*)shmem;

   if (!temp || !temp->query || !temp->query->tuples)
   {
      /* Skip */
      return;
   }

   for (int i = 0; i < temp->query_alt->n_columns; i++)
   {
      if (temp->query_alt->columns[i].type == LABEL_TYPE)
      {
         /* Dealt with later */
         continue;
      }

      /* Create metric name */
      if (strlen(temp->query_alt->columns[i].name) > 0)
      {
         snprintf(metric_name, sizeof(metric_name), "pgexporter_%s_%s", temp->tag, temp->query_alt->columns[i].name);
      }
      else
      {
         snprintf(metric_name, sizeof(metric_name), "pgexporter_%s", temp->tag);
      }

      /* Check if metric already exists in the ART */
      uintptr_t existing = pgexporter_art_search(metrics_art, metric_name);
      if (existing != 0)
      {
         /* Metric already exists, use it */
         metric = (struct prometheus_metric*)existing;
      }
      else
      {
         /* Create new metric */
         metric = malloc(sizeof(struct prometheus_metric));
         memset(metric, 0, sizeof(struct prometheus_metric));

         metric->name = strdup(metric_name);
         metric->help = strdup(temp->query_alt->columns[i].description);

         /* Set metric type based on column type */
         if (temp->query_alt->columns[i].type == GAUGE_TYPE)
         {
            metric->type = strdup("gauge");
         }
         else if (temp->query_alt->columns[i].type == COUNTER_TYPE)
         {
            metric->type = strdup("counter");
         }
         else
         {
            /* Default to gauge */
            metric->type = strdup("gauge");
         }

         pgexporter_deque_create(false, &metric->definitions);

         /* Add to ART */
         pgexporter_art_insert(metrics_art, metric_name, (uintptr_t)metric, ValueRef);
      }

      /* Process all tuples */
      struct tuple* tuple = temp->query->tuples;
      while (tuple)
      {
         attrs = malloc(sizeof(struct prometheus_attributes));
         memset(attrs, 0, sizeof(struct prometheus_attributes));

         pgexporter_deque_create(false, &attrs->attributes);
         pgexporter_deque_create(false, &attrs->values);

         /* Add server attribute */
         attr = malloc(sizeof(struct prometheus_attribute));
         memset(attr, 0, sizeof(struct prometheus_attribute));

         attr->key = strdup("server");
         attr->value = strdup(config->servers[tuple->server].name);

         pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

         /* Add label attributes */
         for (int j = 0; j < temp->query_alt->n_columns; j++)
         {
            if (temp->query_alt->columns[j].type != LABEL_TYPE)
            {
               continue;
            }

            attr = malloc(sizeof(struct prometheus_attribute));
            memset(attr, 0, sizeof(struct prometheus_attribute));

            attr->key = strdup(temp->query_alt->columns[j].name);

            /* Use safe_prometheus_key for label values */
            char* safe_key = safe_prometheus_key(pgexporter_get_column(j, tuple));
            attr->value = strdup(safe_key);
            safe_prometheus_key_free(safe_key);

            pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);
         }

         /* Add value */
         val = malloc(sizeof(struct prometheus_value));
         memset(val, 0, sizeof(struct prometheus_value));

         val->timestamp = 0;

         /* Use get_value to convert the value appropriately */
         char* value_str = get_value(temp->tag,
                                     temp->query_alt->columns[i].name,
                                     pgexporter_get_column(i, tuple));
         val->value = strdup(value_str);

         pgexporter_deque_add(attrs->values, NULL, (uintptr_t)val, ValueRef);

         /* Add attributes to definitions */
         pgexporter_deque_add(metric->definitions, NULL, (uintptr_t)attrs, ValueRef);

         tuple = tuple->next;
      }
   }
}

static void
handle_histogram(struct art* metrics_art, query_list_t* temp)
{
   int n_bounds = 0;
   int n_buckets = 0;
   char* bounds_arr[MAX_ARR_LENGTH] = {0};
   char* buckets_arr[MAX_ARR_LENGTH] = {0};
   struct configuration* config;
   struct prometheus_metric* bucket_metric = NULL;
   struct prometheus_metric* sum_metric = NULL;
   struct prometheus_metric* count_metric = NULL;
   struct prometheus_attributes* attrs = NULL;
   struct prometheus_attribute* attr = NULL;
   struct prometheus_value* val = NULL;
   char metric_name[256];

   config = (struct configuration*)shmem;

   int h_idx = 0;
   for (; h_idx < temp->query_alt->n_columns; h_idx++)
   {
      if (temp->query_alt->columns[h_idx].type == HISTOGRAM_TYPE)
      {
         break;
      }
   }

   if (!temp || !temp->query || !temp->query->tuples)
   {
      return;
   }

   struct tuple* tp = temp->query->tuples;

   if (!tp)
   {
      return;
   }

   char* names[4] = {0};

   /* generate column names X_sum, X_count, X, X_bucket*/
   names[0] = pgexporter_vappend(names[0], 2,
                                 temp->query_alt->columns[h_idx].name,
                                 "_sum"
                                 );
   names[1] = pgexporter_vappend(names[1], 2,
                                 temp->query_alt->columns[h_idx].name,
                                 "_count"
                                 );
   names[2] = pgexporter_vappend(names[2], 1,
                                 temp->query_alt->columns[h_idx].name
                                 );
   names[3] = pgexporter_vappend(names[3], 2,
                                 temp->query_alt->columns[h_idx].name,
                                 "_bucket"
                                 );

   /* Create bucket metric */
   snprintf(metric_name, sizeof(metric_name), "pgexporter_%s_bucket", temp->tag);

   /* Check if bucket metric already exists in the ART */
   uintptr_t existing_bucket = pgexporter_art_search(metrics_art, metric_name);
   if (existing_bucket != 0)
   {
      /* Metric already exists, use it */
      bucket_metric = (struct prometheus_metric*)existing_bucket;
   }
   else
   {
      /* Create new bucket metric */
      bucket_metric = malloc(sizeof(struct prometheus_metric));
      memset(bucket_metric, 0, sizeof(struct prometheus_metric));

      bucket_metric->name = strdup(metric_name);
      bucket_metric->help = strdup(temp->query_alt->columns[h_idx].description);
      bucket_metric->type = strdup("histogram");

      pgexporter_deque_create(false, &bucket_metric->definitions);

      /* Add to ART */
      pgexporter_art_insert(metrics_art, bucket_metric->name, (uintptr_t)bucket_metric, ValueRef);
   }

   /* Create sum metric */
   snprintf(metric_name, sizeof(metric_name), "pgexporter_%s_sum", temp->tag);

   /* Check if sum metric already exists in the ART */
   uintptr_t existing_sum = pgexporter_art_search(metrics_art, metric_name);
   if (existing_sum != 0)
   {
      /* Metric already exists, use it */
      sum_metric = (struct prometheus_metric*)existing_sum;
   }
   else
   {
      /* Create new sum metric */
      sum_metric = malloc(sizeof(struct prometheus_metric));
      memset(sum_metric, 0, sizeof(struct prometheus_metric));

      sum_metric->name = strdup(metric_name);
      sum_metric->help = strdup(temp->query_alt->columns[h_idx].description);
      sum_metric->type = strdup("histogram");

      pgexporter_deque_create(false, &sum_metric->definitions);

      /* Add to ART */
      pgexporter_art_insert(metrics_art, sum_metric->name, (uintptr_t)sum_metric, ValueRef);
   }

   /* Create count metric */
   snprintf(metric_name, sizeof(metric_name), "pgexporter_%s_count", temp->tag);

   /* Check if count metric already exists in the ART */
   uintptr_t existing_count = pgexporter_art_search(metrics_art, metric_name);
   if (existing_count != 0)
   {
      /* Metric already exists, use it */
      count_metric = (struct prometheus_metric*)existing_count;
   }
   else
   {
      /* Create new count metric */
      count_metric = malloc(sizeof(struct prometheus_metric));
      memset(count_metric, 0, sizeof(struct prometheus_metric));

      count_metric->name = strdup(metric_name);
      count_metric->help = strdup(temp->query_alt->columns[h_idx].description);
      count_metric->type = strdup("histogram");

      pgexporter_deque_create(false, &count_metric->definitions);

      /* Add to ART */
      pgexporter_art_insert(metrics_art, count_metric->name, (uintptr_t)count_metric, ValueRef);
   }

   struct tuple* current = temp->query->tuples;

   while (current)
   {
      /* Parse bucket bounds and values */
      char* bounds_str = pgexporter_get_column_by_name(names[2], temp->query, current);
      parse_list(bounds_str, bounds_arr, &n_bounds);

      char* buckets_str = pgexporter_get_column_by_name(names[3], temp->query, current);
      parse_list(buckets_str, buckets_arr, &n_buckets);

      /* Process each bucket */
      for (int i = 0; i < n_bounds; i++)
      {
         attrs = malloc(sizeof(struct prometheus_attributes));
         memset(attrs, 0, sizeof(struct prometheus_attributes));

         pgexporter_deque_create(false, &attrs->attributes);
         pgexporter_deque_create(false, &attrs->values);

         /* Add le attribute */
         attr = malloc(sizeof(struct prometheus_attribute));
         memset(attr, 0, sizeof(struct prometheus_attribute));

         attr->key = strdup("le");
         attr->value = strdup(bounds_arr[i]);

         pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

         /* Add server attribute */
         attr = malloc(sizeof(struct prometheus_attribute));
         memset(attr, 0, sizeof(struct prometheus_attribute));

         attr->key = strdup("server");
         attr->value = strdup(config->servers[current->server].name);

         pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

         /* Add other attributes */
         for (int j = 0; j < h_idx; j++)
         {
            attr = malloc(sizeof(struct prometheus_attribute));
            memset(attr, 0, sizeof(struct prometheus_attribute));

            attr->key = strdup(temp->query_alt->columns[j].name);
            attr->value = strdup(pgexporter_get_column(j, current));

            pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);
         }

         /* Add value */
         val = malloc(sizeof(struct prometheus_value));
         memset(val, 0, sizeof(struct prometheus_value));

         val->timestamp = 0;
         val->value = strdup(buckets_arr[i]);

         pgexporter_deque_add(attrs->values, NULL, (uintptr_t)val, ValueRef);

         /* Add to bucket metric */
         pgexporter_deque_add(bucket_metric->definitions, NULL, (uintptr_t)attrs, ValueRef);
      }

      /* Add +Inf bucket */
      attrs = malloc(sizeof(struct prometheus_attributes));
      memset(attrs, 0, sizeof(struct prometheus_attributes));

      pgexporter_deque_create(false, &attrs->attributes);
      pgexporter_deque_create(false, &attrs->values);

      /* Add le attribute */
      attr = malloc(sizeof(struct prometheus_attribute));
      memset(attr, 0, sizeof(struct prometheus_attribute));

      attr->key = strdup("le");
      attr->value = strdup("+Inf");

      pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

      /* Add server attribute */
      attr = malloc(sizeof(struct prometheus_attribute));
      memset(attr, 0, sizeof(struct prometheus_attribute));

      attr->key = strdup("server");
      attr->value = strdup(config->servers[current->server].name);

      pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

      /* Add other attributes */
      for (int j = 0; j < h_idx; j++)
      {
         attr = malloc(sizeof(struct prometheus_attribute));
         memset(attr, 0, sizeof(struct prometheus_attribute));

         attr->key = strdup(temp->query_alt->columns[j].name);
         attr->value = strdup(pgexporter_get_column(j, current));

         pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);
      }

      /* Add value */
      val = malloc(sizeof(struct prometheus_value));
      memset(val, 0, sizeof(struct prometheus_value));

      val->timestamp = time(NULL);
      val->value = strdup(pgexporter_get_column_by_name(names[1], temp->query, current));

      pgexporter_deque_add(attrs->values, NULL, (uintptr_t)val, ValueRef);

      /* Add to bucket metric */
      pgexporter_deque_add(bucket_metric->definitions, NULL, (uintptr_t)attrs, ValueRef);

      /* Add sum */
      attrs = malloc(sizeof(struct prometheus_attributes));
      memset(attrs, 0, sizeof(struct prometheus_attributes));

      pgexporter_deque_create(false, &attrs->attributes);
      pgexporter_deque_create(false, &attrs->values);

      /* Add server attribute */
      attr = malloc(sizeof(struct prometheus_attribute));
      memset(attr, 0, sizeof(struct prometheus_attribute));

      attr->key = strdup("server");
      attr->value = strdup(config->servers[current->server].name);

      pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

      /* Add other attributes */
      for (int j = 0; j < h_idx; j++)
      {
         attr = malloc(sizeof(struct prometheus_attribute));
         memset(attr, 0, sizeof(struct prometheus_attribute));

         attr->key = strdup(temp->query_alt->columns[j].name);
         attr->value = strdup(pgexporter_get_column(j, current));

         pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);
      }

      /* Add value */
      val = malloc(sizeof(struct prometheus_value));
      memset(val, 0, sizeof(struct prometheus_value));

      val->timestamp = 0;
      val->value = strdup(pgexporter_get_column_by_name(names[0], temp->query, current));

      pgexporter_deque_add(attrs->values, NULL, (uintptr_t)val, ValueRef);

      /* Add to sum metric */
      pgexporter_deque_add(sum_metric->definitions, NULL, (uintptr_t)attrs, ValueRef);

      /* Add count */
      attrs = malloc(sizeof(struct prometheus_attributes));
      memset(attrs, 0, sizeof(struct prometheus_attributes));

      pgexporter_deque_create(false, &attrs->attributes);
      pgexporter_deque_create(false, &attrs->values);

      /* Add server attribute */
      attr = malloc(sizeof(struct prometheus_attribute));
      memset(attr, 0, sizeof(struct prometheus_attribute));

      attr->key = strdup("server");
      attr->value = strdup(config->servers[current->server].name);

      pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);

      /* Add other attributes */
      for (int j = 0; j < h_idx; j++)
      {
         attr = malloc(sizeof(struct prometheus_attribute));
         memset(attr, 0, sizeof(struct prometheus_attribute));

         attr->key = strdup(temp->query_alt->columns[j].name);
         attr->value = strdup(pgexporter_get_column(j, current));

         pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);
      }

      /* Add value */
      val = malloc(sizeof(struct prometheus_value));
      memset(val, 0, sizeof(struct prometheus_value));

      val->timestamp = time(NULL);
      val->value = strdup(pgexporter_get_column_by_name(names[1], temp->query, current));

      pgexporter_deque_add(attrs->values, NULL, (uintptr_t)val, ValueRef);

      /* Add to count metric */
      pgexporter_deque_add(count_metric->definitions, NULL, (uintptr_t)attrs, ValueRef);

      current = current->next;
   }

   /* Clean up */
   for (int i = 0; i < n_bounds; i++)
   {
      free(bounds_arr[i]);
   }

   for (int i = 0; i < n_buckets; i++)
   {
      free(buckets_arr[i]);
   }

   free(names[0]);
   free(names[1]);
   free(names[2]);
   free(names[3]);
}

static int
parse_list(char* list_str, char** strs, int* n_strs)
{
   int idx = 0;
   char* data = NULL;
   char* p = NULL;
   int len = strlen(list_str);

   /**
    * If the `list_str` is `{c1,c2,c3,...,cn}`, and if the `strlen(list_str)`
    * is `x`, then it takes `x + 1` bytes in memory including the null character.
    *
    * `data` will have `list_str` without the first and last bracket (so `data` will
    * just be `c1,c2,c3,...,cn`) and thus `strlen(data)` will be `x - 2`, and
    * so will take `x - 1` bytes in memory including the null character.
    */
   data = (char*) malloc((len - 1) * sizeof(char));
   memset(data, 0, (len - 1) * sizeof(char));

   /**
    * If list_str is `{c1,c2,c3,...,cn}`, then and if `len(list_str)` is `len`
    * then this starts from `c1`, and goes for `len - 2`, so till `cn`, so the
    * `data` string becomes `c1,c2,c3,...,cn`
    */
   strncpy(data, list_str + 1, len - 2);

   p = strtok(data, ",");
   while (p)
   {
      strs[idx] = NULL;
      strs[idx] = pgexporter_append(strs[idx], p);
      idx++;
      p = strtok(NULL, ",");
   }

   *n_strs = idx;
   free(data);
   return 0;
}

static int
send_chunk(SSL* client_ssl, int client_fd, char* data)
{
   int status;
   char* m = NULL;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   m = malloc(20);

   if (m == NULL)
   {
      goto error;
   }

   memset(m, 0, 20);

   sprintf(m, "%zX\r\n", strlen(data));

   m = pgexporter_vappend(m, 2,
                          data,
                          "\r\n"
                          );

   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);

   free(m);

   return status;

error:

   return MESSAGE_STATUS_ERROR;
}

static char*
get_value(char* tag __attribute__((unused)), char* name __attribute__((unused)), char* val)
{
   char* end = NULL;

   /* Empty to 0 */
   if (val == NULL || !strcmp(val, ""))
   {
      return "0";
   }

   /* Bool */
   if (!strcmp(val, "off") || !strcmp(val, "f") || !strcmp(val, "(disabled)"))
   {
      return "0";
   }
   else if (!strcmp(val, "on") || !strcmp(val, "t"))
   {
      return "1";
   }

   if (!strcmp(val, "NaN"))
   {
      return val;
   }

   /* long */
   strtol(val, &end, 10);
   if (*end == '\0')
   {
      return val;
   }
   errno = 0;

   /* double */
   strtod(val, &end);
   if (*end == '\0')
   {
      return val;
   }
   errno = 0;

   /* pgexporter_log_trace("get_value(%s/%s): %s", tag, name, val); */

   /* Map general strings to 1 */
   return "1";
}

static int
safe_prometheus_key_additional_length(char* key)
{
   int count = 0;
   int i = 0;

   while (key[i] != '\0')
   {
      if (key[i] == '"' || key[i] == '\\')
      {
         count++;
      }
      i++;
   }

   /* pgexporter_log_trace("key(%s): %d", key, count); */
   return count;
}

static char*
safe_prometheus_key(char* key)
{
   size_t i = 0;
   size_t j = 0;
   char* escaped = NULL;

   if (key == NULL || strlen(key) == 0)
   {
      return "";
   }

   escaped = (char*) malloc(strlen(key) + safe_prometheus_key_additional_length(key) + 1);
   while (key[i] != '\0')
   {
      if (key[i] == '.')
      {
         if (i == strlen(key) - 1)
         {
            escaped[j] = '\0';
         }
         else
         {
            escaped[j] = '_';
         }
      }
      else
      {
         if (key[i] == '"' || key[i] == '\\')
         {
            escaped[j] = '\\';
            j++;
         }
         escaped[j] = key[i];
      }

      i++;
      j++;
   }
   escaped[j] = '\0';
   return escaped;
}

static void
safe_prometheus_key_free(char* key)
{
   if (key != NULL && strlen(key) > 0)
   {
      free(key);
   }
}

/**
 * Checks if the Prometheus cache configuration setting
 * (`metrics_cache`) has a non-zero value, that means there
 * are seconds to cache the response.
 *
 * @return true if there is a cache configuration,
 *         false if no cache is active
 */
static bool
is_metrics_cache_configured(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   // cannot have caching if not set metrics!
   if (config->metrics == 0)
   {
      return false;
   }

   return config->metrics_cache_max_age != PGEXPORTER_PROMETHEUS_CACHE_DISABLED;
}

/**
 * Checks if the cache is still valid, and therefore can be
 * used to serve as a response.
 * A cache is considred valid if it has non-empty payload and
 * a timestamp in the future.
 *
 * @return true if the cache is still valid
 */
static bool
is_metrics_cache_valid(void)
{
   time_t now;

   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   if (cache->valid_until == 0 || strlen(cache->data) == 0)
   {
      return false;
   }

   now = time(NULL);
   return now <= cache->valid_until;
}

int
pgexporter_init_prometheus_cache(size_t* p_size, void** p_shmem)
{
   struct prometheus_cache* cache;
   struct configuration* config;
   size_t cache_size = 0;
   size_t struct_size = 0;

   config = (struct configuration*)shmem;

   // first of all, allocate the overall cache structure
   cache_size = metrics_cache_size_to_alloc();
   struct_size = sizeof(struct prometheus_cache);

   if (pgexporter_create_shared_memory(struct_size + cache_size, config->hugepage, (void*) &cache))
   {
      goto error;
   }

   memset(cache, 0, struct_size + cache_size);
   cache->valid_until = 0;
   cache->size = cache_size;
   atomic_init(&cache->lock, STATE_FREE);

   // success! do the memory swap
   *p_shmem = cache;
   *p_size = cache_size + struct_size;
   return 0;

error:
   // disable caching
   config->metrics_cache_max_age = config->metrics_cache_max_size = PGEXPORTER_PROMETHEUS_CACHE_DISABLED;
   pgexporter_log_error("Cannot allocate shared memory for the Prometheus cache!");
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}

/**
 * Provides the size of the cache to allocate.
 *
 * It checks if the metrics cache is configured, and
 * computers the right minimum value between the
 * user configured requested size and the default
 * cache size.
 *
 * @return the cache size to allocate
 */
static size_t
metrics_cache_size_to_alloc(void)
{
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   // which size to use ?
   // either the configured (i.e., requested by user) if lower than the max size
   // or the default value
   if (is_metrics_cache_configured())
   {
      cache_size = config->metrics_cache_max_size > 0
            ? MIN(config->metrics_cache_max_size, PROMETHEUS_MAX_CACHE_SIZE)
            : PROMETHEUS_DEFAULT_CACHE_SIZE;
   }

   return cache_size;
}

/**
 * Invalidates the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * Invalidating the cache means that the payload is zero-filled
 * and that the valid_until field is set to zero too.
 */
static void
metrics_cache_invalidate(void)
{
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   memset(cache->data, 0, cache->size);
   cache->valid_until = 0;
}

/**
 * Appends data to the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * If the input data is empty, nothing happens.
 * The data is appended only if the cache does not overflows, that
 * means the current size of the cache plus the size of the data
 * to append does not exceed the current cache size.
 * If the cache overflows, the cache is flushed and marked
 * as invalid.
 * This makes safe to call this method along the workflow of
 * building the Prometheus response.
 *
 * @param data the string to append to the cache
 * @return true on success
 */
static bool
metrics_cache_append(char* data)
{
   size_t origin_length = 0;
   size_t append_length = 0;
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;

   if (!is_metrics_cache_configured())
   {
      return false;
   }

   origin_length = strlen(cache->data);
   append_length = strlen(data);
   // need to append the data to the cache
   if (origin_length + append_length >= cache->size)
   {
      // cannot append new data, so invalidate cache
      pgexporter_log_debug("Cannot append %d bytes to the Prometheus cache because it will overflow the size of %d bytes (currently at %d bytes). HINT: try adjusting `metrics_cache_max_size`",
                           append_length,
                           cache->size,
                           origin_length);
      metrics_cache_invalidate();
      return false;
   }

   // append the data to the data field
   memcpy(cache->data + origin_length, data, append_length);
   cache->data[origin_length + append_length + 1] = '\0';
   return true;
}

/**
 * Finalizes the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * This method should be invoked when the cache is complete
 * and therefore can be served.
 *
 * @return true if the cache has a validity
 */
static bool
metrics_cache_finalize(void)
{
   struct configuration* config;
   struct prometheus_cache* cache;
   time_t now;

   cache = (struct prometheus_cache*)prometheus_cache_shmem;
   config = (struct configuration*)shmem;

   if (!is_metrics_cache_configured())
   {
      return false;
   }

   now = time(NULL);
   cache->valid_until = now + config->metrics_cache_max_age;
   return cache->valid_until > now;
}

/* Common function to output metrics from an ART */
static void
output_metrics_from_art(int client_fd, struct art* metrics_art, SSL* ssl)
{
   struct art_iterator* metrics_iterator = NULL;
   char* data = NULL;

   if (pgexporter_art_iterator_create(metrics_art, &metrics_iterator))
   {
      pgexporter_log_error("Failed to create iterator for metrics");
      return;
   }

   while (pgexporter_art_iterator_next(metrics_iterator))
   {
      struct prometheus_metric* metric_data = (struct prometheus_metric*)metrics_iterator->value->data;
      struct deque_iterator* definition_iterator = NULL;

      data = pgexporter_append(data, "#HELP ");
      data = pgexporter_append(data, metric_data->name);
      data = pgexporter_append_char(data, ' ');
      data = pgexporter_append(data, metric_data->help);
      data = pgexporter_append_char(data, '\n');

      data = pgexporter_append(data, "#TYPE ");
      data = pgexporter_append(data, metric_data->name);
      data = pgexporter_append_char(data, ' ');
      data = pgexporter_append(data, metric_data->type);
      data = pgexporter_append_char(data, '\n');

      if (pgexporter_deque_iterator_create(metric_data->definitions, &definition_iterator))
      {
         pgexporter_log_error("Failed to create definition iterator");
         continue;
      }

      while (pgexporter_deque_iterator_next(definition_iterator))
      {
         struct deque_iterator* attributes_iterator = NULL;
         struct prometheus_attributes* attrs_data = (struct prometheus_attributes*)definition_iterator->value->data;
         struct prometheus_value* value_data = NULL;

         if (pgexporter_deque_iterator_create(attrs_data->attributes, &attributes_iterator))
         {
            pgexporter_log_error("Failed to create attributes iterator");
            continue;
         }

         value_data = (struct prometheus_value*)pgexporter_deque_peek_last(attrs_data->values, NULL);

         data = pgexporter_append(data, metric_data->name);
         data = pgexporter_append_char(data, '{');

         while (pgexporter_deque_iterator_next(attributes_iterator))
         {
            struct prometheus_attribute* attr_data = (struct prometheus_attribute*)attributes_iterator->value->data;

            data = pgexporter_append(data, attr_data->key);
            data = pgexporter_append(data, "=\"");
            data = pgexporter_append(data, attr_data->value);
            data = pgexporter_append_char(data, '\"');

            if (pgexporter_deque_iterator_has_next(attributes_iterator))
            {
               data = pgexporter_append(data, ", ");
            }
         }

         data = pgexporter_append(data, "} ");
         data = pgexporter_append(data, value_data->value);

         data = pgexporter_append_char(data, '\n');

         pgexporter_deque_iterator_destroy(attributes_iterator);
      }

      data = pgexporter_append_char(data, '\n');

      pgexporter_deque_iterator_destroy(definition_iterator);
   }

   /* Add to cache and send */
   if (data != NULL)
   {
      metrics_cache_append(data);
      send_chunk(ssl,client_fd, data);
      free(data);
   }

   pgexporter_art_iterator_destroy(metrics_iterator);
}

/* Common function to clean up an ART */
static void
cleanup_metrics_art(struct art* metrics_art)
{
   struct art_iterator* metrics_iterator = NULL;

   if (metrics_art == NULL)
   {
      return;
   }

   /* Free each metric in the ART */
   if (pgexporter_art_iterator_create(metrics_art, &metrics_iterator))
   {
      pgexporter_log_error("Failed to create iterator for cleanup");
      return;
   }

   while (pgexporter_art_iterator_next(metrics_iterator))
   {
      struct prometheus_metric* metric_data = (struct prometheus_metric*)metrics_iterator->value->data;
      struct deque_iterator* definition_iterator = NULL;

      if (pgexporter_deque_iterator_create(metric_data->definitions, &definition_iterator))
      {
         pgexporter_log_error("Failed to create definition iterator for cleanup");
         continue;
      }

      while (pgexporter_deque_iterator_next(definition_iterator))
      {
         struct prometheus_attributes* attrs_data = (struct prometheus_attributes*)definition_iterator->value->data;
         struct deque_iterator* attributes_iterator = NULL;
         struct deque_iterator* values_iterator = NULL;

         if (pgexporter_deque_iterator_create(attrs_data->attributes, &attributes_iterator))
         {
            pgexporter_log_error("Failed to create attributes iterator for cleanup");
            continue;
         }

         while (pgexporter_deque_iterator_next(attributes_iterator))
         {
            struct prometheus_attribute* attr_data = (struct prometheus_attribute*)attributes_iterator->value->data;
            free(attr_data->key);
            free(attr_data->value);
            free(attr_data);
         }

         pgexporter_deque_iterator_destroy(attributes_iterator);
         pgexporter_deque_destroy(attrs_data->attributes);

         if (pgexporter_deque_iterator_create(attrs_data->values, &values_iterator))
         {
            pgexporter_log_error("Failed to create values iterator for cleanup");
            continue;
         }

         while (pgexporter_deque_iterator_next(values_iterator))
         {
            struct prometheus_value* value_data = (struct prometheus_value*)values_iterator->value->data;
            free(value_data->value);
            free(value_data);
         }

         pgexporter_deque_iterator_destroy(values_iterator);
         pgexporter_deque_destroy(attrs_data->values);

         free(attrs_data);
      }

      pgexporter_deque_iterator_destroy(definition_iterator);
      pgexporter_deque_destroy(metric_data->definitions);

      free(metric_data->name);
      free(metric_data->help);
      free(metric_data->type);
      free(metric_data);
   }

   pgexporter_art_iterator_destroy(metrics_iterator);
   pgexporter_art_destroy(metrics_art);
}

static struct prometheus_metric*
create_metric(const char* name, const char* help, const char* type)
{
   struct prometheus_metric* metric = malloc(sizeof(struct prometheus_metric));
   if (metric == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for metric");
      return NULL;
   }

   memset(metric, 0, sizeof(struct prometheus_metric));

   metric->name = strdup(name);
   metric->help = strdup(help);
   metric->type = strdup(type);

   pgexporter_deque_create(false, &metric->definitions);

   return metric;
}

/* Helper function to create attributes for a metric */
static struct prometheus_attributes*
create_attributes(void)
{
   struct prometheus_attributes* attrs = malloc(sizeof(struct prometheus_attributes));
   if (attrs == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for attributes");
      return NULL;
   }

   memset(attrs, 0, sizeof(struct prometheus_attributes));

   pgexporter_deque_create(false, &attrs->attributes);
   pgexporter_deque_create(false, &attrs->values);

   return attrs;
}

/* Helper function to add an attribute to attributes */
static void
add_attribute(struct prometheus_attributes* attrs, const char* key, const char* value)
{
   struct prometheus_attribute* attr = malloc(sizeof(struct prometheus_attribute));
   if (attr == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for attribute");
      return;
   }

   memset(attr, 0, sizeof(struct prometheus_attribute));

   attr->key = strdup(key);
   attr->value = strdup(value);

   pgexporter_deque_add(attrs->attributes, NULL, (uintptr_t)attr, ValueRef);
}

/* Helper function to add a value to attributes */
static void
add_value(struct prometheus_attributes* attrs, const char* value)
{
   struct prometheus_value* val = malloc(sizeof(struct prometheus_value));
   if (val == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for value");
      return;
   }

   memset(val, 0, sizeof(struct prometheus_value));

   val->timestamp = 0;
   val->value = strdup(value);

   pgexporter_deque_add(attrs->values, NULL, (uintptr_t)val, ValueRef);
}

/* Helper function to add a server attribute */
static void
add_server_attribute(struct prometheus_attributes* attrs, const char* server_name)
{
   add_attribute(attrs, "server", server_name);
}

/* Helper function to add attributes and value to a metric */
static void
add_metric_data(struct prometheus_metric* metric, struct prometheus_attributes* attrs)
{
   pgexporter_deque_add(metric->definitions, NULL, (uintptr_t)attrs, ValueRef);
}

/* Helper function to add a metric to an ART */
static void
add_metric_to_art(struct art* metrics_art, struct prometheus_metric* metric)
{
   pgexporter_art_insert(metrics_art, metric->name, (uintptr_t)metric, ValueRef);
}

/* Helper function to create a simple gauge metric with a single value */
static void
add_simple_gauge(struct art* metrics_art, const char* name, const char* help,
                 const char* server_name, const char* value)
{
   struct prometheus_metric* metric = create_metric(name, help, "gauge");
   if (metric == NULL)
   {
      return;
   }

   struct prometheus_attributes* attrs = create_attributes();
   if (attrs == NULL)
   {
      free(metric->name);
      free(metric->help);
      free(metric->type);
      free(metric);
      return;
   }

   add_server_attribute(attrs, server_name);
   add_value(attrs, value);
   add_metric_data(metric, attrs);
   add_metric_to_art(metrics_art, metric);
}
