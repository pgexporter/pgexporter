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
#include <pgexporter.h>
#include <art.h>
#include <bridge.h>
#include <deque.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus_client.h>
#include <queries.h>
#include <security.h>
#include <shmem.h>
#include <stddef.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define CHUNK_SIZE 32768

#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_METRICS 2
#define BAD_REQUEST  3

static int resolve_page(struct message* msg);
static int badrequest_page(int client_fd);
static int unknown_page(int client_fd);
static int home_page(int client_fd);
static int metrics_page(int client_fd);
static int bad_request(int client_fd);

static int send_chunk(int client_fd, char* data);

static bool is_bridge_cache_configured(void);
static bool is_bridge_cache_valid(void);
static bool bridge_cache_append(char* data);
static bool bridge_cache_finalize(void);
static size_t bridge_cache_size_to_alloc(void);
static void bridge_cache_invalidate(void);

static bool is_bridge_json_cache_configured(void);
static bool bridge_json_cache_set(char* data);
static size_t bridge_json_cache_size_to_alloc(void);

static void bridge_metrics(int client_fd);
static void bridge_json_metrics(int client_fd);

void
pgexporter_bridge(int client_fd)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   status = pgexporter_read_timeout_message(
      NULL, client_fd, config->authentication_timeout, &msg);

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      home_page(client_fd);
   }
   else if (page == PAGE_METRICS)
   {
      metrics_page(client_fd);
   }
   else if (page == PAGE_UNKNOWN)
   {
      unknown_page(client_fd);
   }
   else
   {
      bad_request(client_fd);
   }

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);

error:

   badrequest_page(client_fd);

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(1);
}

void
pgexporter_bridge_json(int client_fd)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   status = pgexporter_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME || page == PAGE_METRICS)
   {
      bridge_json_metrics(client_fd);
   }
   else if (page == PAGE_UNKNOWN)
   {
      unknown_page(client_fd);
   }
   else
   {
      bad_request(client_fd);
   }

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);

error:

   badrequest_page(client_fd);

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(1);
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (msg->length < 3 || strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgexporter_log_debug("Bridge: Not a GET request");
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
badrequest_page(int client_fd)
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

   status = pgexporter_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static int
unknown_page(int client_fd)
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

   status = pgexporter_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static int
home_page(int client_fd)
{
   char* data = NULL;
   int status;
   time_t now;
   char time_buf[32];
   struct message msg;

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

   status = pgexporter_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);
   data = NULL;

   data = pgexporter_vappend(data, 13,
                             "<html>\n", "<head>\n",
                             "  <title>pgexporter: Bridge</title>\n",
                             "</head>\n",
                             "<body>\n",
                             "  <h1>pgexporter</h1>\n",
                             "  Bridge for Prometheus\n",
                             "  <p>\n",
                             "  <a href=\"/metrics\">Metrics</a>\n",
                             "  <p>\n",
                             "  <a href=\"https://pgexporter.github.io/\">pgexporter.github.io/</a>\n",
                             "</body>\n",
                             "</html>\n"
                             );

   send_chunk(client_fd, data);
   free(data);
   data = NULL;

   /* Footer */
   data = pgexporter_append(data, "\r\n\r\n");

   send_chunk(client_fd, data);
   free(data);
   data = NULL;

   return 0;

error:

   free(data);

   return 1;
}

static int
metrics_page(int client_fd)
{
   char* data = NULL;
   time_t start_time;
   int dt;
   char time_buf[32];
   int status;
   struct message msg;
   struct prometheus_cache* cache;
   signed char cache_is_free;
   struct configuration* config;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)bridge_cache_shmem;

   memset(&msg, 0, sizeof(struct message));

   start_time = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&start_time, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      // Can we serve the message out of cache?
      if (is_bridge_cache_configured() && is_bridge_cache_valid())
      {
         // serve the message directly out of the cache
         pgexporter_log_debug("Serving bridge out of cache (%d/%d bytes valid until %lld)",
                              strlen(cache->data),
                              cache->size,
                              cache->valid_until);

         /* Header */
         data = pgexporter_vappend(data, 7,
                                   "HTTP/1.1 200 OK\r\n",
                                   "Content-Type: text/plain; charset=utf-8\r\n",
                                   "Date: ", &time_buf[0], "\r\n",
                                   "Transfer-Encoding: chunked\r\n", "\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgexporter_write_message(NULL, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         free(data);
         data = NULL;

         /* Cache */
         send_chunk(client_fd, cache->data);

         /* Footer */
         data = pgexporter_append(data, "0\r\n\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgexporter_write_message(NULL, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         free(data);
         data = NULL;
      }
      else
      {
         pgexporter_log_debug("Serving bridge fresh");

         bridge_cache_invalidate();

         data = pgexporter_vappend(data, 7,
                                   "HTTP/1.1 200 OK\r\n",
                                   "Content-Type: text/plain; version=0.0.1; charset=utf-8\r\n",
                                   "Date: ", &time_buf[0], "\r\n",
                                   "Transfer-Encoding: chunked\r\n",
                                   "\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgexporter_write_message(NULL, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         free(data);
         data = NULL;

         /* Metrics */
         bridge_metrics(client_fd);

         /* Footer */
         data = pgexporter_append(data, "0\r\n\r\n");

         msg.kind = 0;
         msg.length = strlen(data);
         msg.data = data;

         status = pgexporter_write_message(NULL, client_fd, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
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
bad_request(int client_fd)
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

   status = pgexporter_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static int
send_chunk(int client_fd, char* data)
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

   m = pgexporter_vappend(m, 2, data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgexporter_write_message(NULL, client_fd, &msg);

   free(m);

   return status;

error:

   return MESSAGE_STATUS_ERROR;
}

/**
 * Checks if the Prometheus cache configuration setting
 * (`bridge_cache`) has a non-zero value, that means there
 * are seconds to cache the response.
 *
 * @return true if there is a cache configuration,
 *         false if no cache is active
 */
static bool
is_bridge_cache_configured(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   // cannot have caching if not set bridge!
   if (config->bridge <= 0)
   {
      return false;
   }

   return config->bridge_cache_max_age != PROMETHEUS_BRIDGE_CACHE_DISABLED &&
          config->bridge_cache_max_size != PROMETHEUS_BRIDGE_CACHE_DISABLED;
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
is_bridge_cache_valid(void)
{
   time_t now;

   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)bridge_cache_shmem;

   if (cache->valid_until == 0 || strlen(cache->data) == 0)
   {
      return false;
   }

   now = time(NULL);
   return now <= cache->valid_until;
}

int
pgexporter_bridge_init_cache(size_t* p_size, void** p_shmem)
{
   struct prometheus_cache* cache;
   struct configuration* config;
   size_t cache_size = 0;
   size_t struct_size = 0;

   config = (struct configuration*)shmem;

   // first of all, allocate the overall cache structure
   cache_size = bridge_cache_size_to_alloc();
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
   config->bridge_cache_max_age = config->bridge_cache_max_size = PROMETHEUS_BRIDGE_CACHE_DISABLED;
   pgexporter_log_error("Cannot allocate shared memory for the Prometheus cache!");
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}

/**
 * Provides the size of the cache to allocate.
 *
 * It checks if the bridge cache is configured, and
 * computers the right minimum value between the
 * user configured requested size and the default
 * cache size.
 *
 * @return the cache size to allocate
 */
static size_t
bridge_cache_size_to_alloc(void)
{
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   // which size to use ?
   // either the configured (i.e., requested by user) if lower than the max size
   // or the default value
   if (is_bridge_cache_configured())
   {
      cache_size = config->bridge_cache_max_size;

      if (cache_size > 0)
      {
         cache_size = MIN(config->bridge_cache_max_size, PROMETHEUS_MAX_BRIDGE_CACHE_SIZE);
      }
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
bridge_cache_invalidate(void)
{
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)bridge_cache_shmem;

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
bridge_cache_append(char* data)
{
   size_t origin_length = 0;
   size_t append_length = 0;
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)bridge_cache_shmem;

   if (!is_bridge_cache_configured())
   {
      return false;
   }

   origin_length = strlen(cache->data);
   append_length = strlen(data);
   // need to append the data to the cache
   if (origin_length + append_length >= cache->size)
   {
      // cannot append new data, so invalidate cache
      pgexporter_log_warn("Cannot append %d bytes to the Prometheus cache because it will overflow the size of %d bytes (currently at %d bytes). HINT: try adjusting `bridge_cache_max_size`",
                          append_length,
                          cache->size,
                          origin_length);
      bridge_cache_invalidate();
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
bridge_cache_finalize(void)
{
   struct configuration* config;
   struct prometheus_cache* cache;
   time_t now;

   cache = (struct prometheus_cache*)bridge_cache_shmem;
   config = (struct configuration*)shmem;

   if (!is_bridge_cache_configured())
   {
      return false;
   }

   now = time(NULL);
   cache->valid_until = now + config->bridge_cache_max_age;
   return cache->valid_until > now;
}

/**
 * Checks if the Prometheus cache configuration setting
 * (`bridge_json_cache`) has a non-zero value, that means there
 * are seconds to cache the response.
 *
 * @return true if there is a cache configuration,
 *         false if no cache is active
 */
static bool
is_bridge_json_cache_configured(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   // cannot have caching if not set bridge_json!
   if (config->bridge_json <= 0)
   {
      return false;
   }

   return true;
}

int
pgexporter_bridge_json_init_cache(size_t* p_size, void** p_shmem)
{
   struct prometheus_cache* cache;
   struct configuration* config;
   size_t cache_size = 0;
   size_t struct_size = 0;

   config = (struct configuration*)shmem;

   // first of all, allocate the overall cache structure
   cache_size = bridge_json_cache_size_to_alloc();
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
   config->bridge_json_cache_max_size = PROMETHEUS_BRIDGE_CACHE_DISABLED;
   pgexporter_log_error("Cannot allocate shared memory for the Prometheus cache!");
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}

/**
 * Provides the size of the cache to allocate.
 *
 * It checks if the bridge_json cache is configured, and
 * computers the right minimum value between the
 * user configured requested size and the default
 * cache size.
 *
 * @return the cache size to allocate
 */
static size_t
bridge_json_cache_size_to_alloc(void)
{
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   // which size to use ?
   // either the configured (i.e., requested by user) if lower than the max size
   // or the default value
   if (is_bridge_json_cache_configured())
   {
      cache_size = config->bridge_json_cache_max_size;

      if (cache_size > 0)
      {
         cache_size = MIN(config->bridge_json_cache_max_size, PROMETHEUS_MAX_BRIDGE_JSON_CACHE_SIZE);
      }
   }

   return cache_size;
}

/**
 * Set data to the cache.
 *
 * Requires the caller to hold the lock on the cache!
 *
 * @param data the string to append to the cache
 * @return true on success
 */
static bool
bridge_json_cache_set(char* data)
{
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)bridge_json_cache_shmem;

   if (!is_bridge_json_cache_configured())
   {
      return false;
   }

   memset(cache->data, 0, cache->size);

   if (strlen(data) < cache->size)
   {
      memcpy(cache->data, data, strlen(data));
   }
   else
   {
      pgexporter_log_warn("Bridge/JSON: The data won't fit - %lld > %lld", strlen(data), cache->size);
   }

   return true;
}

static void
bridge_metrics(int client_fd)
{
   time_t start_time;
   int dt;
   signed char cache_is_free;
   signed char cache_json_is_free;
   char* data = NULL;
   struct prometheus_bridge* bridge = NULL;
   struct art_iterator* metrics_iterator = NULL;
   struct prometheus_cache* cache;
   struct prometheus_cache* cache_json;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)bridge_cache_shmem;
   cache_json = (struct prometheus_cache*)bridge_json_cache_shmem;

   if (pgexporter_prometheus_client_create_bridge(&bridge))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_endpoints; i++)
   {
      pgexporter_log_trace("Start: %s:%d",
                           config->endpoints[i].host,
                           config->endpoints[i].port);
      pgexporter_prometheus_client_get(i, bridge);
      pgexporter_log_trace("Done: %s:%d",
                           config->endpoints[i].host,
                           config->endpoints[i].port);
   }

   if (pgexporter_art_iterator_create(bridge->metrics, &metrics_iterator))
   {
      goto error;
   }

   cache_is_free = STATE_FREE;
   cache_json_is_free = STATE_FREE;

   start_time = time(NULL);

retry_cache_locking:
   if (is_bridge_cache_configured())
   {
      if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
      {
         bridge_cache_invalidate();

         if (is_bridge_json_cache_configured())
         {
retry_cache_json_locking:
            if (!atomic_compare_exchange_strong(&cache_json->lock, &cache_json_is_free, STATE_IN_USE))
            {
               dt = (int)difftime(time(NULL), start_time);
               if (dt >= (config->blocking_timeout > 0 ? config->blocking_timeout : 30))
               {
                  goto error;
               }

               /* Sleep for 10ms */
               SLEEP_AND_GOTO(10000000L, retry_cache_json_locking);
            }
         }
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
         goto error;
      }

      while (pgexporter_deque_iterator_next(definition_iterator))
      {
         struct deque_iterator* attributes_iterator = NULL;
         struct prometheus_attributes* attrs_data = (struct prometheus_attributes*)definition_iterator->value->data;
         struct prometheus_value* value_data = NULL;

         if (pgexporter_deque_iterator_create(attrs_data->attributes, &attributes_iterator))
         {
            goto error;
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

      if (is_bridge_cache_configured())
      {
         bridge_cache_append(data);
      }

      send_chunk(client_fd, data);

      pgexporter_deque_iterator_destroy(definition_iterator);

      free(data);
      data = NULL;
   }

   if (is_bridge_json_cache_configured())
   {
      char* arts = pgexporter_art_to_string(bridge->metrics, FORMAT_JSON, NULL, 0);
      bridge_json_cache_set(arts);
      pgexporter_log_trace("%s", arts);
      free(arts);
   }

   if (is_bridge_cache_configured())
   {
      bridge_cache_finalize();
      atomic_store(&cache->lock, STATE_FREE);

      if (is_bridge_json_cache_configured())
      {
         atomic_store(&cache_json->lock, STATE_FREE);
      }
   }

   pgexporter_art_iterator_destroy(metrics_iterator);

   pgexporter_prometheus_client_destroy_bridge(bridge);

   return;

error:

   pgexporter_art_iterator_destroy(metrics_iterator);

   pgexporter_prometheus_client_destroy_bridge(bridge);
}

static void
bridge_json_metrics(int client_fd)
{
   char* data = NULL;
   time_t start_time;
   int dt;
   char time_buf[32];
   int status;
   struct message msg;
   struct prometheus_cache* cache;
   signed char cache_is_free;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)bridge_json_cache_shmem;

   cache_is_free = STATE_FREE;

   start_time = time(NULL);

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&start_time, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

retry_cache_locking:
   if (is_bridge_json_cache_configured())
   {
      if (!atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
      {
         dt = (int)difftime(time(NULL), start_time);
         if (dt >= (config->blocking_timeout > 0 ? config->blocking_timeout : 30))
         {
            goto error;
         }

         /* Sleep for 10ms */
         SLEEP_AND_GOTO(10000000L, retry_cache_locking);
      }

      /* Header */
      data = pgexporter_vappend(data, 7,
                                "HTTP/1.1 200 OK\r\n",
                                "Content-Type: text/plain; charset=utf-8\r\n",
                                "Date: ", &time_buf[0], "\r\n",
                                "Transfer-Encoding: chunked\r\n", "\r\n");

      msg.kind = 0;
      msg.length = strlen(data);
      msg.data = data;

      status = pgexporter_write_message(NULL, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      free(data);
      data = NULL;

      /* Cache */
      if (strlen(cache->data) > 0)
      {
         send_chunk(client_fd, cache->data);
      }
      else
      {
         send_chunk(client_fd, "{\n}\n");
      }

      /* Footer */
      data = pgexporter_append(data, "0\r\n\r\n");

      msg.kind = 0;
      msg.length = strlen(data);
      msg.data = data;

      status = pgexporter_write_message(NULL, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      free(data);
      data = NULL;

      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      goto error;
   }

   return;

error:

   pgexporter_log_error("bridge_json_metrics called");
}
