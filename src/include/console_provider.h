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

#ifndef PGEXPORTER_CONSOLE_PROVIDER_H
#define PGEXPORTER_CONSOLE_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <stdbool.h>
#include <time.h>

/**
 * @struct console_label
 * Key/value label pair on a metric
 */
struct console_label
{
   char* key;   /**< Label key */
   char* value; /**< Label value */
};

/**
 * @struct console_metric
 * A lightweight metric structure optimized for console display
 */
struct console_metric
{
   char* name;                   /**< Full/leaf metric name */
   char* type;                   /**< Metric type (gauge, counter, histogram, etc.) */
   char* help;                   /**< Description of the metric */
   double value;                 /**< The numeric value of the metric */
   char* server;                 /**< Server name associated with this metric */
   struct console_label* labels; /**< Array of key/value labels */
   int label_count;              /**< Number of labels */
};

/**
 * @struct console_category
 * A category of related metrics, grouped by name prefix
 */
struct console_category
{
   char* name;                     /**< Category name */
   struct console_metric* metrics; /**< Array of metrics in this category */
   int metric_count;               /**< Number of metrics in category */
};

/**
 * @struct console_server
 * Server information for console display
 */
struct console_server
{
   char* name;  /**< Server name */
   bool active; /**< Whether server is active */
};

/**
 * @struct console_status
 * Management status information for display in console
 */
struct console_status
{
   char* status;                   /**< Overall status ("Running" / "Unavailable") */
   char* version;                  /**< pgexporter version */
   int num_servers;                /**< Number of configured servers */
   char* last_updated;             /**< ISO timestamp of last update */
   struct console_server* servers; /**< Array of server information */
};

/**
 * @struct console_page
 * Complete console state for rendering a page or API payload
 */
struct console_page
{
   struct console_category* categories; /**< Array of metric categories */
   int category_count;                  /**< Number of categories */
   struct console_status* status;       /**< Management status info */
   time_t refresh_time;                 /**< When metrics were last refreshed */
   char* brand_name;                    /**< Application name for branding */
   char* metric_prefix;                 /**< Metric prefix to strip */
   char* mode;                          /**< Active mode ("realtime" or "historical") */
};

/**
 * @struct console_query
 * Query parameters passed to a console data provider
 */
struct console_query
{
   const char* mode; /**< Requested data mode ("realtime" by default) */
   time_t timestamp; /**< Target timestamp (0 for realtime) */
   int duration;     /**< Time range in seconds (0 for point-in-time) */
};

/**
 * @struct console_data_provider
 * Pluggable backend interface for supplying normalized metrics and status to the console
 */
struct console_data_provider
{
   const char* name;
   int (*init)(void** ctx);
   int (*populate_page)(void* ctx, const struct console_query* query, struct console_page* page);
   void (*destroy)(void* ctx);
};

/**
 * Get the real-time Prometheus bridge data provider
 * @return Pointer to the static real-time provider vtable
 */
const struct console_data_provider*
pgexporter_console_realtime_provider(void);

/**
 * Find or create a category by name inside a console_page
 * @param console The console page
 * @param name The category name
 * @return Pointer to the category, or NULL on allocation failure
 */
struct console_category*
pgexporter_console_find_or_create_category(struct console_page* console, const char* name);

/**
 * Append a metric to a category (takes ownership of metric fields)
 * @param category The target category
 * @param metric The metric to copy into the category array
 * @return 0 on success, otherwise 1
 */
int
pgexporter_console_category_add_metric(struct console_category* category, struct console_metric* metric);

/**
 * Free all fields owned by a single console_metric
 * @param metric The metric to clean up
 */
void
pgexporter_console_metric_free(struct console_metric* metric);

#ifdef __cplusplus
}
#endif

#endif
