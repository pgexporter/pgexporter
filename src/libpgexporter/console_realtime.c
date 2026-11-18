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
#include <console.h>
#include <http.h>
#include <http_server.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <prometheus_client.h>
#include <management.h>
#include <message.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

#include <console_provider.h>

struct prefix_count
{
   char* prefix;
   int count;
};

struct category_candidate
{
   char* prefix;
   int count;
   int depth;
   double score;
};

/* Constants for category selection */
#define MIN_GROUP_SIZE                 2
#define MAX_DEPTH                      4

#define METRIC_LIST_INITIAL_CAP        64
#define PREFIX_COUNT_INITIAL_CAP       32
#define CATEGORY_CANDIDATE_INITIAL_CAP 16
#define CATEGORY_SELECT_INITIAL_CAP    16

static int build_categories_from_bridge(struct prometheus_bridge* bridge, struct console_page* console);
static int record_prefix_counts(const char* metric_name, struct prefix_count** counts, int* size, int* capacity);
static int add_or_increment_prefix(struct prefix_count** counts, int* size, int* capacity, const char* prefix);
static int count_prefix_depth(const char* prefix);
static int build_category_candidates(struct prefix_count* counts, int size, struct category_candidate** candidates, int* candidate_count);
static int compare_candidates_by_score(const void* a, const void* b);
static char** select_global_categories(struct category_candidate* candidates, int candidate_count, int* selected_count);
static char* find_best_category(const char* metric_name, char** categories, int category_count);
static char* extract_category_prefix(char* metric_name);
static char* fallback_category_from_last_underscore(char* metric_name);
static struct console_metric* create_metric_from_prometheus_attrs(struct prometheus_metric* prom_metric, const char* display_name, struct prometheus_attributes* attrs);
static int extract_labels_from_prometheus_attrs(struct prometheus_attributes* attrs, struct console_metric* metric);

struct console_realtime_ctx
{
   int endpoint;
};

static int console_refresh_metrics(int endpoint, struct console_page* console);
static int console_refresh_status(struct console_page* console);

static int
console_refresh_metrics(int endpoint, struct console_page* console)
{
   struct prometheus_bridge* bridge = NULL;
   struct configuration* config = NULL;
   int effective_endpoint = endpoint;

   if (console == NULL)
   {
      pgexporter_log_error("Invalid console parameter");
      goto error;
   }

   config = (struct configuration*)shmem;
   if (config != NULL)
   {
      if (config->number_of_endpoints <= 0 || effective_endpoint >= config->number_of_endpoints || config->endpoints[effective_endpoint].port == 0)
      {
         if (config->metrics > 0)
         {
            effective_endpoint = 0;
            config->number_of_endpoints = 1;
            /* Use loopback if host is wildcard/empty */
            const char* h = (strlen(config->host) == 0 || strcmp(config->host, "*") == 0 || strcmp(config->host, "0.0.0.0") == 0) ? "127.0.0.1" : config->host;
            strncpy(config->endpoints[0].host, h, MISC_LENGTH - 1);
            config->endpoints[0].host[MISC_LENGTH - 1] = '\0';
            config->endpoints[0].port = config->metrics;
            config->endpoints[0].tls = strlen(config->metrics_cert_file) > 0;
         }
         else
         {
            pgexporter_log_error("No Prometheus endpoint configured and metrics listener disabled");
            goto error;
         }
      }
   }

   if (pgexporter_prometheus_client_create_bridge(&bridge))
   {
      pgexporter_log_error("Failed to create Prometheus bridge");
      goto error;
   }

   if (pgexporter_prometheus_client_get(effective_endpoint, bridge))
   {
      pgexporter_log_error("Failed to fetch metrics from endpoint %d", effective_endpoint);
      goto error;
   }

   if (build_categories_from_bridge(bridge, console))
   {
      pgexporter_log_error("Failed to build categories from metrics");
      goto error;
   }

   pgexporter_prometheus_client_destroy_bridge(bridge);

   console->refresh_time = time(NULL);

   return 0;

error:
   if (bridge != NULL)
   {
      pgexporter_prometheus_client_destroy_bridge(bridge);
   }

   return 1;
}

static int
console_refresh_status(struct console_page* console)
{
   int socket = -1;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   struct json* payload = NULL;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct configuration* config = NULL;
   char timestamp[128];
   time_t now;
   struct tm* time_info;
   int num_servers = 0;
   int status = 0;

   if (console == NULL || console->status == NULL)
   {
      pgexporter_log_error("Invalid console or status parameter");
      status = 1;
      goto error;
   }

   /* Initialize status with defaults first */
   if (console->status->status == NULL)
   {
      console->status->status = strdup("Unavailable");
   }
   if (console->status->version == NULL)
   {
      console->status->version = strdup(VERSION);
   }
   if (console->status->last_updated == NULL)
   {
      console->status->last_updated = strdup("Unknown");
   }
   if (console->status->num_servers == 0)
   {
      console->status->num_servers = 1;
   }

   config = (struct configuration*)shmem;
   if (config == NULL)
   {
      goto error;
   }

   if (pgexporter_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
   {
      pgexporter_log_debug("Failed to connect to management socket, using default values");
      goto error;
   }

   if (pgexporter_management_request_status(NULL, socket, compression, encryption, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      pgexporter_log_warn("Failed to send status request");
      goto error;
   }

   if (pgexporter_management_read_json(NULL, socket, &compression, &encryption, &payload))
   {
      pgexporter_log_warn("Failed to read status response");
      goto error;
   }

   response = (struct json*)pgexporter_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE);
   if (response == NULL)
   {
      pgexporter_log_warn("No response in payload");
      goto error;
   }

   char* server_version = (char*)pgexporter_json_get(response, MANAGEMENT_ARGUMENT_SERVER_VERSION);
   free(console->status->version);
   if (server_version != NULL)
   {
      console->status->version = strdup(server_version);
   }
   else
   {
      console->status->version = strdup(VERSION);
   }

   if (console->status->servers != NULL)
   {
      for (int i = 0; i < console->status->num_servers; i++)
      {
         free(console->status->servers[i].name);
      }
      free(console->status->servers);
      console->status->servers = NULL;
   }

   num_servers = (int32_t)(uintptr_t)pgexporter_json_get(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS);
   console->status->num_servers = num_servers;

   servers = (struct json*)pgexporter_json_get(response, MANAGEMENT_ARGUMENT_SERVERS);
   if (servers != NULL && servers->type == JSONArray && num_servers > 0)
   {
      int server_idx = 0;
      struct json_iterator* iter = NULL;

      console->status->servers = (struct console_server*)malloc(num_servers * sizeof(struct console_server));
      if (console->status->servers != NULL)
      {
         memset(console->status->servers, 0, num_servers * sizeof(struct console_server));
      }

      if (pgexporter_json_iterator_create(servers, &iter) == 0)
      {
         while (pgexporter_json_iterator_next(iter) && server_idx < num_servers)
         {
            struct json* server = (struct json*)(iter->value->data);
            bool active = (bool)(uintptr_t)pgexporter_json_get(server, MANAGEMENT_ARGUMENT_ACTIVE);
            char* server_name = (char*)pgexporter_json_get(server, MANAGEMENT_ARGUMENT_SERVER);

            if (console->status->servers != NULL)
            {
               console->status->servers[server_idx].name = server_name ? strdup(server_name) : strdup("unknown");
               console->status->servers[server_idx].active = active;
            }

            server_idx++;
         }

         pgexporter_json_iterator_destroy(iter);
      }
   }

   free(console->status->status);
   console->status->status = strdup("Running");

   now = time(NULL);
   time_info = localtime(&now);
   free(console->status->last_updated);
   if (time_info != NULL)
   {
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
      console->status->last_updated = strdup(timestamp);
   }
   else
   {
      console->status->last_updated = strdup("Unknown");
   }

   status = 0;

error:
   if (payload != NULL)
   {
      pgexporter_json_destroy(payload);
   }

   if (socket != -1)
   {
      pgexporter_disconnect(socket);
   }

   if (status != 0)
   {
      free(console->status->status);
      console->status->status = strdup("Unavailable");
   }

   return status;
}

static int
build_categories_from_bridge(struct prometheus_bridge* bridge, struct console_page* console)
{
   struct art_iterator* iter = NULL;
   struct prometheus_metric** metrics = NULL;
   int metric_count = 0;
   int metric_capacity = 0;

   struct prefix_count* prefix_counts = NULL;
   int prefix_count_size = 0;
   int prefix_count_capacity = 0;

   struct category_candidate* candidates = NULL;
   int candidate_count = 0;
   char** selected_categories = NULL;
   int selected_count = 0;

   int status = 0;

   if (bridge == NULL || console == NULL)
   {
      pgexporter_log_error("Invalid parameters for building categories");
      status = 1;
      goto error;
   }

   console->categories = NULL;
   console->category_count = 0;

   if (pgexporter_art_iterator_create(bridge->metrics, &iter))
   {
      pgexporter_log_error("Failed to create ART iterator");
      status = 1;
      goto error;
   }

   /* collect metrics and count shared prefixes */
   while (pgexporter_art_iterator_next(iter))
   {
      struct prometheus_metric* prom_metric = (struct prometheus_metric*)iter->value->data;
      const char* base_name = NULL;

      if (prom_metric == NULL || prom_metric->name == NULL)
      {
         continue;
      }

      base_name = prom_metric->name;
      if (strncmp(base_name, "pgexporter_", strlen("pgexporter_")) == 0)
      {
         base_name = base_name + strlen("pgexporter_");
      }

      if (metric_count == metric_capacity)
      {
         metric_capacity = metric_capacity == 0 ? METRIC_LIST_INITIAL_CAP : metric_capacity * 2;
         struct prometheus_metric** resized = realloc(metrics, metric_capacity * sizeof(struct prometheus_metric*));
         if (resized == NULL)
         {
            pgexporter_log_error("Failed to grow metric list");
            status = 1;
            goto error;
         }
         metrics = resized;
      }

      metrics[metric_count++] = prom_metric;

      if (record_prefix_counts(base_name, &prefix_counts, &prefix_count_size, &prefix_count_capacity))
      {
         pgexporter_log_error("Failed to record prefix counts");
         status = 1;
         goto error;
      }
   }

   pgexporter_art_iterator_destroy(iter);
   iter = NULL;

   /* Build and rank category candidates globally */
   if (build_category_candidates(prefix_counts, prefix_count_size, &candidates, &candidate_count))
   {
      pgexporter_log_error("Failed to build category candidates");
      status = 1;
      goto error;
   }

   selected_categories = select_global_categories(candidates, candidate_count, &selected_count);
   if (selected_categories == NULL)
   {
      pgexporter_log_warn("No categories selected, using fallback");
   }

   /* assign metrics to selected categories */
   for (int i = 0; i < metric_count; i++)
   {
      struct prometheus_metric* prom_metric = metrics[i];
      struct console_category* category = NULL;
      char* category_name = NULL;
      char* leaf_name = NULL;
      const char* base_name = NULL;
      struct deque_iterator* def_iter = NULL;

      if (prom_metric == NULL || prom_metric->name == NULL)
      {
         continue;
      }

      base_name = prom_metric->name;
      if (console->metric_prefix != NULL && strncmp(base_name, console->metric_prefix, strlen(console->metric_prefix)) == 0)
      {
         base_name = base_name + strlen(console->metric_prefix);
      }

      /* Find the best matching category from the globally selected set */
      category_name = find_best_category(base_name, selected_categories, selected_count);
      if (category_name == NULL)
      {
         category_name = extract_category_prefix((char*)base_name);
      }
      if (category_name == NULL)
      {
         category_name = strdup("uncategorized");
      }

      /* Leaf = remainder after category prefix */
      size_t cat_len = strlen(category_name);
      size_t base_len = strlen(base_name);
      if (base_len > cat_len + 1 && base_name[cat_len] == '_')
      {
         leaf_name = strdup(base_name + cat_len + 1);
      }
      else
      {
         leaf_name = strdup(base_name);
      }

      category = pgexporter_console_find_or_create_category(console, category_name);
      free(category_name);
      category_name = NULL;

      if (category == NULL)
      {
         pgexporter_log_warn("Failed to find/create category for %s", prom_metric->name);
         free(leaf_name);
         continue;
      }

      if (prom_metric->definitions != NULL && pgexporter_deque_size(prom_metric->definitions) > 0)
      {
         if (pgexporter_deque_iterator_create(prom_metric->definitions, &def_iter) == 0)
         {
            while (pgexporter_deque_iterator_next(def_iter))
            {
               struct prometheus_attributes* attrs = (struct prometheus_attributes*)def_iter->value->data;
               struct console_metric* console_metric = NULL;

               if (attrs == NULL)
               {
                  continue;
               }

               console_metric = create_metric_from_prometheus_attrs(prom_metric, leaf_name, attrs);
               if (console_metric == NULL)
               {
                  pgexporter_log_warn("Failed to create console metric for %s", prom_metric->name);
                  continue;
               }

               if (pgexporter_console_category_add_metric(category, console_metric))
               {
                  pgexporter_log_warn("Failed to add metric %s to category", prom_metric->name);
                  free(console_metric->name);
                  free(console_metric->type);
                  free(console_metric->help);
                  free(console_metric->server);
                  if (console_metric->labels != NULL)
                  {
                     for (int k = 0; k < console_metric->label_count; k++)
                     {
                        free(console_metric->labels[k].key);
                        free(console_metric->labels[k].value);
                     }
                     free(console_metric->labels);
                  }
                  free(console_metric);
                  continue;
               }

               free(console_metric);
            }
            pgexporter_deque_iterator_destroy(def_iter);
         }
      }

      free(leaf_name);
   }

error:
   if (iter != NULL)
   {
      pgexporter_art_iterator_destroy(iter);
   }

   if (prefix_counts != NULL)
   {
      for (int i = 0; i < prefix_count_size; i++)
      {
         free(prefix_counts[i].prefix);
      }
      free(prefix_counts);
   }

   if (metrics != NULL)
   {
      free(metrics);
   }

   if (candidates != NULL)
   {
      for (int i = 0; i < candidate_count; i++)
      {
         free(candidates[i].prefix);
      }
      free(candidates);
   }

   if (selected_categories != NULL)
   {
      for (int i = 0; i < selected_count; i++)
      {
         free(selected_categories[i]);
      }
      free(selected_categories);
   }

   return status;
}

/**
 * Helper: Extract category prefix from metric name
 * Example: "pg_stat_statements_calls" -> "pg_stat_statements"
 */
static char*
extract_category_prefix(char* metric_name)
{
   char* prefix = NULL;
   if (metric_name == NULL)
   {
      return NULL;
   }

   prefix = fallback_category_from_last_underscore(metric_name);

   return prefix;
}

static char*
fallback_category_from_last_underscore(char* metric_name)
{
   char* prefix = NULL;
   char* last_underscore = NULL;

   if (metric_name == NULL)
   {
      return NULL;
   }

   prefix = strdup(metric_name);
   if (prefix == NULL)
   {
      return NULL;
   }

   last_underscore = strrchr(prefix, '_');
   if (last_underscore != NULL)
   {
      *last_underscore = '\0';
   }

   return prefix;
}

/**
 * Helper: Find or create category

 */
static struct console_metric*
create_metric_from_prometheus_attrs(struct prometheus_metric* prom_metric, const char* display_name, struct prometheus_attributes* attrs)
{
   struct console_metric* metric = NULL;
   struct prometheus_value* value_data = NULL;

   if (prom_metric == NULL || attrs == NULL)
   {
      return NULL;
   }

   metric = (struct console_metric*)malloc(sizeof(struct console_metric));
   if (metric == NULL)
   {
      pgexporter_log_error("Failed to allocate console metric");
      return NULL;
   }

   memset(metric, 0, sizeof(struct console_metric));

   metric->name = strdup(display_name != NULL ? display_name : prom_metric->name);
   metric->type = strdup(prom_metric->type != NULL ? prom_metric->type : "gauge");
   metric->help = strdup(prom_metric->help != NULL ? prom_metric->help : "");
   metric->value = 0.0;
   metric->server = NULL;
   metric->label_count = 0;
   metric->labels = NULL;

   /* Get the last value (most recent timestamp) */
   if (attrs->values != NULL && pgexporter_deque_size(attrs->values) > 0)
   {
      value_data = (struct prometheus_value*)pgexporter_deque_peek_last(attrs->values, NULL);
      if (value_data != NULL && value_data->value != NULL)
      {
         metric->value = atof(value_data->value);
      }
   }

   if (extract_labels_from_prometheus_attrs(attrs, metric))
   {
      pgexporter_log_warn("Failed to extract labels for metric %s", prom_metric->name);
   }

   return metric;
}

static int
extract_labels_from_prometheus_attrs(struct prometheus_attributes* attrs, struct console_metric* metric)
{
   struct deque_iterator* attr_iter = NULL;
   int label_idx = 0;

   if (attrs == NULL || metric == NULL)
   {
      goto error;
   }

   if (attrs->attributes == NULL || pgexporter_deque_size(attrs->attributes) == 0)
   {
      return 0;
   }

   metric->label_count = pgexporter_deque_size(attrs->attributes);
   metric->labels = (struct console_label*)malloc(metric->label_count * sizeof(struct console_label));
   if (metric->labels == NULL)
   {
      metric->label_count = 0;
      goto error;
   }

   memset(metric->labels, 0, metric->label_count * sizeof(struct console_label));

   if (pgexporter_deque_iterator_create(attrs->attributes, &attr_iter))
   {
      free(metric->labels);
      metric->labels = NULL;
      metric->label_count = 0;
      goto error;
   }

   while (pgexporter_deque_iterator_next(attr_iter) && label_idx < metric->label_count)
   {
      struct prometheus_attribute* attr = (struct prometheus_attribute*)attr_iter->value->data;

      if (attr == NULL || attr->key == NULL || attr->value == NULL)
      {
         continue;
      }

      if (strcmp(attr->key, "server") == 0)
      {
         free(metric->server);
         metric->server = strdup(attr->value);
      }

      metric->labels[label_idx].key = strdup(attr->key);
      metric->labels[label_idx].value = strdup(attr->value);

      if (metric->labels[label_idx].key != NULL && metric->labels[label_idx].value != NULL)
      {
         label_idx++;
      }
      else
      {
         free(metric->labels[label_idx].key);
         free(metric->labels[label_idx].value);
         metric->labels[label_idx].key = NULL;
         metric->labels[label_idx].value = NULL;
      }
   }

   pgexporter_deque_iterator_destroy(attr_iter);
   metric->label_count = label_idx;

   return 0;
error:
   if (attr_iter != NULL)
   {
      pgexporter_deque_iterator_destroy(attr_iter);
   }
   return 1;
}

/**
 * Helper: Add or increment a prefix in the counts array
 */
static int
add_or_increment_prefix(struct prefix_count** counts, int* size, int* capacity, const char* prefix)
{
   int found = -1;

   if (counts == NULL || size == NULL || capacity == NULL || prefix == NULL)
   {
      return 1;
   }

   /* Find existing prefix */
   for (int j = 0; j < *size; j++)
   {
      if (strcmp((*counts)[j].prefix, prefix) == 0)
      {
         found = j;
         break;
      }
   }

   if (found == -1)
   {
      /* Prefix not found, add it */
      if (*size == *capacity)
      {
         *capacity = (*capacity == 0) ? PREFIX_COUNT_INITIAL_CAP : (*capacity * 2);
         struct prefix_count* resized = realloc(*counts, *capacity * sizeof(struct prefix_count));
         if (resized == NULL)
         {
            return 1;
         }
         *counts = resized;
      }

      (*counts)[*size].prefix = strdup(prefix);
      if ((*counts)[*size].prefix == NULL)
      {
         return 1;
      }
      (*counts)[*size].count = 1;
      (*size)++;
   }
   else
   {
      (*counts)[found].count += 1;
   }

   return 0;
}

/**
 * Helper: Increment shared prefix counts for a metric name
 */
static int
record_prefix_counts(const char* metric_name, struct prefix_count** counts, int* size, int* capacity)
{
   size_t len = 0;

   if (metric_name == NULL || counts == NULL || size == NULL || capacity == NULL)
   {
      return 1;
   }

   len = strlen(metric_name);

   /* Traverse the string and record prefixes at every underscore boundary */
   for (size_t i = 0; i < len; i++)
   {
      if (metric_name[i] == '_')
      {
         char* prefix = strndup(metric_name, i);
         if (prefix == NULL)
         {
            return 1;
         }

         if (add_or_increment_prefix(counts, size, capacity, prefix))
         {
            free(prefix);
            return 1;
         }
         free(prefix);
      }
   }

   /* Also record the full metric name as a prefix */
   if (add_or_increment_prefix(counts, size, capacity, metric_name))
   {
      return 1;
   }

   return 0;
}

/**
 * Helper: Count the depth (number of underscores) in a prefix
 */
static int
count_prefix_depth(const char* prefix)
{
   int depth = 0;
   if (prefix == NULL)
   {
      return 0;
   }

   for (int i = 0; prefix[i] != '\0'; i++)
   {
      if (prefix[i] == '_')
      {
         depth++;
      }
   }
   return depth;
}

/**
 * Helper: Build category candidates from prefix counts
 * Filters by MIN_GROUP_SIZE and MAX_DEPTH, calculates scores
 */
static int
build_category_candidates(struct prefix_count* counts, int size, struct category_candidate** candidates, int* candidate_count)
{
   struct category_candidate* cands = NULL;
   int count = 0;
   int capacity = 0;
   int status = 0;

   if (counts == NULL || candidates == NULL || candidate_count == NULL)
   {
      status = 1;
      goto error;
   }

   for (int i = 0; i < size; i++)
   {
      int depth = count_prefix_depth(counts[i].prefix);

      if (counts[i].count >= MIN_GROUP_SIZE && depth > 0 && depth <= MAX_DEPTH)
      {
         if (count == capacity)
         {
            capacity = capacity == 0 ? CATEGORY_CANDIDATE_INITIAL_CAP : capacity * 2;
            struct category_candidate* resized = realloc(cands, capacity * sizeof(struct category_candidate));
            if (resized == NULL)
            {
               pgexporter_log_error("Failed to grow category candidates");
               status = 1;
               goto error;
            }
            cands = resized;
         }

         cands[count].prefix = strdup(counts[i].prefix);
         if (cands[count].prefix == NULL)
         {
            pgexporter_log_error("Failed to allocate prefix string");
            status = 1;
            goto error;
         }
         cands[count].count = counts[i].count;
         cands[count].depth = depth;
         /* higher count and moderate depth preferred */
         cands[count].score = counts[i].count * (1.0 + depth * 0.2);
         count++;
      }
   }

   *candidates = cands;
   *candidate_count = count;
   return 0;

error:
   if (cands != NULL)
   {
      for (int i = 0; i < count; i++)
      {
         free(cands[i].prefix);
      }
      free(cands);
   }
   return status;
}

/**
 * Helper: Compare candidates by score (descending)
 */
static int
compare_candidates_by_score(const void* a, const void* b)
{
   const struct category_candidate* ca = (const struct category_candidate*)a;
   const struct category_candidate* cb = (const struct category_candidate*)b;

   if (cb->score > ca->score)
   {
      return 1;
   }
   else if (cb->score < ca->score)
   {
      return -1;
   }
   return 0;
}

/**
 * Helper: Select non-overlapping category prefixes globally
 * Sort by score descending, accept prefix only if not already covered by a longer accepted prefix
 */
static char**
select_global_categories(struct category_candidate* candidates, int candidate_count, int* selected_count)
{
   char** selected = NULL;
   int count = 0;
   int capacity = 0;

   if (candidates == NULL || candidate_count == 0 || selected_count == NULL)
   {
      *selected_count = 0;
      return NULL;
   }

   /* Sort candidates by score descending */
   qsort(candidates, candidate_count, sizeof(struct category_candidate), compare_candidates_by_score);

   for (int i = 0; i < candidate_count; i++)
   {
      int is_covered = 0;

      /* Check if this candidate is already covered by a longer accepted prefix */
      for (int j = 0; j < count; j++)
      {
         size_t sel_len = strlen(selected[j]);
         size_t cand_len = strlen(candidates[i].prefix);

         if (cand_len > sel_len && strncmp(candidates[i].prefix, selected[j], sel_len) == 0 && candidates[i].prefix[sel_len] == '_')
         {
            is_covered = 1;
            break;
         }
      }

      if (!is_covered)
      {
         if (count == capacity)
         {
            capacity = capacity == 0 ? CATEGORY_SELECT_INITIAL_CAP : capacity * 2;
            char** resized = realloc(selected, capacity * sizeof(char*));
            if (resized == NULL)
            {
               for (int k = 0; k < count; k++)
               {
                  free(selected[k]);
               }
               free(selected);
               return NULL;
            }
            selected = resized;
         }

         selected[count] = strdup(candidates[i].prefix);
         count++;
      }
   }

   *selected_count = count;
   return selected;
}

/**
 * Helper: Find the longest matching category for a metric name
 */
static char*
find_best_category(const char* metric_name, char** categories, int category_count)
{
   char* best = NULL;
   size_t best_len = 0;

   if (metric_name == NULL || categories == NULL || category_count == 0)
   {
      return NULL;
   }

   for (int i = 0; i < category_count; i++)
   {
      size_t cat_len = strlen(categories[i]);
      size_t name_len = strlen(metric_name);

      /* Check if category is a prefix of metric_name followed by _ */
      if (name_len > cat_len && strncmp(metric_name, categories[i], cat_len) == 0 && metric_name[cat_len] == '_')
      {
         if (cat_len > best_len)
         {
            best_len = cat_len;
            best = categories[i];
         }
      }
   }

   return best != NULL ? strdup(best) : NULL;
}

/**
 * Helper: Generate HTML table for metrics in a category
 */

static int
realtime_init(void** ctx)
{
   struct console_realtime_ctx* rt = NULL;

   if (ctx == NULL)
   {
      return 1;
   }

   rt = (struct console_realtime_ctx*)calloc(1, sizeof(struct console_realtime_ctx));
   if (rt == NULL)
   {
      return 1;
   }

   *ctx = rt;
   return 0;
}

static int
realtime_populate_page(void* ctx, const struct console_query* query, struct console_page* page)
{
   struct console_realtime_ctx* rt = (struct console_realtime_ctx*)ctx;
   int endpoint = (rt != NULL) ? rt->endpoint : 0;

   (void)query;

   if (page == NULL || page->status == NULL)
   {
      return 1;
   }

   if (console_refresh_metrics(endpoint, page))
   {
      pgexporter_log_error("Failed to refresh realtime metrics");
      return 1;
   }

   if (console_refresh_status(page))
   {
      pgexporter_log_warn("Failed to refresh realtime status");
   }

   page->refresh_time = time(NULL);
   if (page->mode == NULL)
   {
      page->mode = strdup("realtime");
   }

   return 0;
}

static void
realtime_destroy(void* ctx)
{
   free(ctx);
}

static const struct console_data_provider realtime_provider = {
   .name = "realtime",
   .init = realtime_init,
   .populate_page = realtime_populate_page,
   .destroy = realtime_destroy,
};

const struct console_data_provider*
pgexporter_console_realtime_provider(void)
{
   return &realtime_provider;
}
