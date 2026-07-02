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
#include <bridge.h>
#include <http_server.h>
#include <cache.h>
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

#define CHUNK_SIZE                       32768
#define DEFAULT_BLOCKING_TIMEOUT_SECONDS 30

static int home_page(SSL* ssl, int fd);
static int metrics_page(SSL* ssl, int fd);

static bool is_bridge_cache_configured(void);
static bool is_bridge_cache_valid(void);
static bool bridge_cache_append(char* data);
static bool bridge_cache_finalize(void);
static size_t bridge_cache_size_to_alloc(void);
static void bridge_cache_invalidate(void);

static bool is_bridge_json_cache_configured(void);
static bool bridge_json_cache_set(char* data);
static size_t bridge_json_cache_size_to_alloc(void);

static void bridge_metrics(SSL* ssl, int client_fd);
static int bridge_json_metrics(SSL* ssl, int fd);

static struct http_route bridge_routes[] = {
   {"/", home_page},
   {"/index.html", home_page},
   {"/metrics", metrics_page},
};

static struct http_route bridge_json_routes[] = {
   {"/", bridge_json_metrics},
   {"/index.html", bridge_json_metrics},
   {"/metrics", bridge_json_metrics},
};

void
pgexporter_bridge(int client_fd)
{
   struct http_server_request* req = NULL;

   pgexporter_start_logging();
   pgexporter_memory_init();

   if (pgexporter_http_server_parse(NULL, client_fd, &req) != MESSAGE_STATUS_OK)
   {
      pgexporter_http_respond_400(NULL, client_fd);
   }
   else
   {
      pgexporter_http_server_dispatch(NULL, client_fd, req,
                                      bridge_routes,
                                      sizeof(bridge_routes) / sizeof(bridge_routes[0]));
   }

   pgexporter_http_server_request_destroy(req);
   pgexporter_disconnect(client_fd);
   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);
}

void
pgexporter_bridge_json(int client_fd)
{
   struct http_server_request* req = NULL;

   pgexporter_start_logging();
   pgexporter_memory_init();

   if (pgexporter_http_server_parse(NULL, client_fd, &req) != MESSAGE_STATUS_OK)
   {
      pgexporter_http_respond_400(NULL, client_fd);
   }
   else
   {
      pgexporter_http_server_dispatch(NULL, client_fd, req,
                                      bridge_json_routes,
                                      sizeof(bridge_json_routes) / sizeof(bridge_json_routes[0]));
   }

   pgexporter_http_server_request_destroy(req);
   pgexporter_disconnect(client_fd);
   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);
}

static int
home_page(SSL* ssl, int fd)
{
   char* data = NULL;
   int status;

   status = pgexporter_http_respond_chunked_start(ssl, fd, "text/html; charset=utf-8");
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

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
                             "</html>\n");

   pgexporter_http_respond_chunked_write(ssl, fd, data);
   free(data);
   data = NULL;

   pgexporter_http_respond_chunked_end(ssl, fd);

   return 0;

error:

   free(data);

   return 1;
}

static int
metrics_page(SSL* ssl, int fd)
{
   time_t start_time;
   int dt;
   int status;
   struct prometheus_cache* cache;
   signed char cache_is_free;
   struct configuration* config;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)bridge_cache_shmem;

   start_time = time(NULL);

retry_cache_locking:
   cache_is_free = STATE_FREE;
   if (atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
   {
      // Can we serve the message out of cache?
      if (is_bridge_cache_configured() && is_bridge_cache_valid())
      {
         // serve the message directly out of the cache
         pgexporter_log_debug("Serving bridge out of cache (%d/%d bytes valid until %lld)",
                              strlen(cache->data), cache->size, cache->valid_until);

         status = pgexporter_http_respond_chunked_start(ssl, fd, "text/plain; charset=utf-8");
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         pgexporter_http_respond_chunked_write(ssl, fd, cache->data);
         pgexporter_http_respond_chunked_end(ssl, fd);
      }
      else
      {
         pgexporter_log_debug("Serving bridge fresh");

         bridge_cache_invalidate();

         status = pgexporter_http_respond_chunked_start(ssl, fd,
                                                        "text/plain; version=0.0.1; charset=utf-8");
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }

         bridge_metrics(ssl, fd);
         pgexporter_http_respond_chunked_end(ssl, fd);
      }

      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      dt = (int)difftime(time(NULL), start_time);
      if (dt >= (pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) > 0 ? pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) : DEFAULT_BLOCKING_TIMEOUT_SECONDS))
      {
         goto error;
      }

      /* Sleep for 10ms */
      SLEEP_AND_GOTO(10000000L, retry_cache_locking);
   }

   return 0;

error:

   return 1;
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

   return pgexporter_time_is_valid(config->bridge_cache_max_age) &&
          config->bridge_cache_max_size != PROMETHEUS_BRIDGE_CACHE_DISABLED;
}

/**
 * Checks if the cache is still valid, and therefore can be
 * used to serve as a response.
 * A cache is considered valid if it has non-empty payload and
 * a timestamp in the future.
 *
 * @return true if the cache is still valid
 */
static bool
is_bridge_cache_valid(void)
{
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)bridge_cache_shmem;

   return pgexporter_cache_is_valid(cache);
}

int
pgexporter_bridge_init_cache(size_t* p_size, void** p_shmem)
{
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   cache_size = bridge_cache_size_to_alloc();

   if (pgexporter_cache_init(cache_size, p_size, p_shmem))
   {
      goto error;
   }

   return 0;

error:
   // disable caching
   config->bridge_cache_max_age = PGEXPORTER_TIME_DISABLED;
   config->bridge_cache_max_size = PROMETHEUS_BRIDGE_CACHE_DISABLED;
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

   pgexporter_cache_invalidate(cache);
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
   struct prometheus_cache* cache;

   cache = (struct prometheus_cache*)bridge_cache_shmem;

   if (!is_bridge_cache_configured())
   {
      return false;
   }

   return pgexporter_cache_append(cache, data);
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

   cache = (struct prometheus_cache*)bridge_cache_shmem;
   config = (struct configuration*)shmem;

   if (!is_bridge_cache_configured())
   {
      return false;
   }

   return pgexporter_cache_finalize(cache, config->bridge_cache_max_age);
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
   struct configuration* config;
   size_t cache_size = 0;

   config = (struct configuration*)shmem;

   cache_size = bridge_json_cache_size_to_alloc();

   if (pgexporter_cache_init(cache_size, p_size, p_shmem))
   {
      goto error;
   }

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
bridge_metrics(SSL* ssl, int client_fd)
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
               if (dt >= (pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) > 0 ? pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) : DEFAULT_BLOCKING_TIMEOUT_SECONDS))
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
         if (dt >= (pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) > 0 ? pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) : DEFAULT_BLOCKING_TIMEOUT_SECONDS))
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

      pgexporter_http_respond_chunked_write(ssl, client_fd, data);

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

static int
bridge_json_metrics(SSL* ssl, int fd)
{
   time_t start_time;
   int dt;
   int status;
   struct prometheus_cache* cache;
   signed char cache_is_free;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   cache = (struct prometheus_cache*)bridge_json_cache_shmem;

   cache_is_free = STATE_FREE;
   start_time = time(NULL);

retry_cache_locking:
   if (is_bridge_json_cache_configured())
   {
      if (!atomic_compare_exchange_strong(&cache->lock, &cache_is_free, STATE_IN_USE))
      {
         dt = (int)difftime(time(NULL), start_time);
         if (dt >= (pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) > 0 ? pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S) : DEFAULT_BLOCKING_TIMEOUT_SECONDS))
         {
            goto error;
         }

         /* Sleep for 10ms */
         SLEEP_AND_GOTO(10000000L, retry_cache_locking);
      }

      status = pgexporter_http_respond_chunked_start(ssl, fd, "text/plain; charset=utf-8");
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (strlen(cache->data) > 0)
      {
         pgexporter_http_respond_chunked_write(ssl, fd, cache->data);
      }
      else
      {
         pgexporter_http_respond_chunked_write(ssl, fd, "{\n}\n");
      }

      pgexporter_http_respond_chunked_end(ssl, fd);

      atomic_store(&cache->lock, STATE_FREE);
   }
   else
   {
      goto error;
   }

   return MESSAGE_STATUS_OK;

error:

   pgexporter_log_error("bridge_json_metrics: failed to serve response");
   return MESSAGE_STATUS_ERROR;
}
