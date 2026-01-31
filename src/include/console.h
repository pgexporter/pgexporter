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

#ifndef PGEXPORTER_CONSOLE_H
#define PGEXPORTER_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <http.h>
#include <prometheus_client.h>

#include <stdbool.h>
#include <time.h>

/**
 * @struct console_metric
 * A lightweight metric structure optimized for console display
 */
struct console_metric
{
   char* name;      /**< Full metric name (e.g., "pg_stat_statements_calls") */
   char* type;      /**< Metric type (gauge, counter, histogram, etc.) */
   char* help;      /**< Description of the metric */
   double value;    /**< The numeric value of the metric */
   char** labels;   /**< Array of "key=value" label strings */
   int label_count; /**< Number of labels */
};

/**
 * @struct console_category
 * A category of related metrics, grouped by name prefix
 */
struct console_category
{
   char* name;                     /**< Category name (e.g., "pg_stat_statements") */
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
   char* status;                   /**< Overall status */
   char* version;                  /**< pgexporter version */
   int num_servers;                /**< Number of configured servers */
   char* last_updated;             /**< ISO timestamp of last update */
   struct console_server* servers; /**< Array of server information */
};

/**
 * @struct console_page
 * Complete console state for rendering a page
 */
struct console_page
{
   struct console_category* categories; /**< Array of metric categories */
   int category_count;                  /**< Number of categories */
   struct console_status* status;       /**< Management status info */
   time_t refresh_time;                 /**< When metrics were last refreshed */
   char* brand_name;                    /**< Application name for branding */
   char* metric_prefix;                 /**< Metric prefix to strip */
};

/**
 * Create and initialize the console
 * @param endpoint The endpoint index to fetch metrics from
 * @param brand_name Application name for display (NULL for default "Metrics Console")
 * @param metric_prefix Metric prefix to strip (NULL for no stripping)
 * @param result The resulting console_page
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_console_init(int endpoint, const char* brand_name, const char* metric_prefix, struct console_page** result);

/**
 * Handle console HTTP request
 * @param client_ssl The client SSL connection (can be NULL)
 * @param client_fd The client file descriptor
 */
void
pgexporter_console(SSL* client_ssl, int client_fd);

/**
 * Refresh metrics from Prometheus endpoint
 * @param endpoint The endpoint index
 * @param console The console page to update
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_console_refresh_metrics(int endpoint, struct console_page* console);

/**
 * Refresh status information from management protocol
 * @param console The console page to update
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_console_refresh_status(struct console_page* console);

/**
 * Generate HTML content for the console page
 * @param console The console page
 * @param html Output buffer for HTML content
 * @param html_size Output variable for HTML size
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_console_generate_html(struct console_page* console, char** html, size_t* html_size);

/**
 * Generate JSON representation of metrics
 * @param console The console page
 * @param json Output buffer for JSON content
 * @param json_size Output variable for JSON size
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_console_generate_json(struct console_page* console, char** json, size_t* json_size);

/**
 * Get a specific metric by name from console
 * @param console The console page
 * @param metric_name The name of the metric to find
 * @return The metric, or NULL if not found
 */
struct console_metric*
pgexporter_console_get_metric(struct console_page* console, char* metric_name);

/**
 * Get a specific category by name from console
 * @param console The console page
 * @param category_name The name of the category to find
 * @return The category, or NULL if not found
 */
struct console_category*
pgexporter_console_get_category(struct console_page* console, char* category_name);

/**
 * Destroy console page and free all resources
 * @param console The console page
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_console_destroy(struct console_page* console);

#ifdef __cplusplus
}
#endif

#endif
