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
#include <internal.h>
#include <logging.h>
#include <query_alts.h>
#include <shmem.h>
#include <utils.h>
#include <json_configuration.h>

/* system */
#include <json.h>
#include <errno.h>

/* JSON Parsing */

/* JSON file's structure definitions */

// Column's Value's Structure
typedef struct json_column
{
   char* name;
   char* description;
   char* type;
} __attribute__ ((aligned (64))) json_column_t;

// Query's Value's Structure
typedef struct json_query
{
   bool is_histogram;
   char* query;
   char version;
   json_column_t* columns;
   int n_columns;
} __attribute__ ((aligned (64))) json_query_t;

// Metric's Value's Structure
typedef struct json_metric
{
   json_query_t* queries;
   int n_queries;
   char* tag;
   char* sort;
   char* collector;
   char* server;
} __attribute__ ((aligned (64))) json_metric_t;

// Config's Structure
typedef struct json_config
{
   json_metric_t* metrics;
   int n_metrics;
   char default_version;
} __attribute__ ((aligned (64))) json_config_t;

// Parses the value of `metrics` array in JSON
static int parse_metrics(struct json* metrics_array, json_config_t* config);

// Parses the value of `queries` array in JSON
static int parse_queries(struct json* queries_array, json_metric_t* metric);

// Parses the value of `columns` array in JSON
static int parse_columns(struct json* columns_array, json_query_t* query);

// Free allocated memory for JSON columns
static void free_json_columns(json_column_t** columns, size_t n_columns);

// Free allocated memory for JSON queries' structure
static void free_json_queries(json_query_t** queries, size_t n_queries);

// Free allocated memory for JSON metric's structure
static void free_json_metrics(json_metric_t** metrics, size_t n_metrics);

// Free allocated memory for JSON config
static void free_json_config(json_config_t* config);

// Extract the meaning of the `json_config` and load the metrics into `prometheus`
static int semantics_json(struct prometheus* prometheus, int prometheus_idx, json_config_t* json_config);

// Read and parse a single JSON file into the prometheus metrics structure
int pgexporter_read_json(struct prometheus* prometheus, int prometheus_idx, char* filename, int* number_of_metrics);

// Get all JSON files from a directory
int get_json_files(char* base, int* number_of_json_files, char*** files);

// Check if given filename has a JSON extension
bool is_json_file(char* filename);

int
pgexporter_read_json_metrics_configuration(void* shmem)
{
   struct configuration* config;
   int idx_metrics = 0;
   int number_of_metrics = 0;
   int number_of_json_files = 0;
   char** json_files = NULL;
   char* json_path = NULL;

   config = (struct configuration*) shmem;
   idx_metrics = config->number_of_metrics;

   if (pgexporter_is_file(config->metrics_path))
   {
      number_of_metrics = 0;
      if (pgexporter_read_json(config->prometheus, idx_metrics, config->metrics_path, &number_of_metrics))
      {
         pgexporter_log_error("pgexporter_read_json_metrics_configuration error JSON metrics file: %s", config->metrics_path);
         return 1;
      }
      idx_metrics += number_of_metrics;
   }
   else if (pgexporter_is_directory(config->metrics_path))
   {
      get_json_files(config->metrics_path, &number_of_json_files, &json_files);
      for (int i = 0; i < number_of_json_files; i++)
      {
         number_of_metrics = 0;

         json_path = pgexporter_vappend(json_path, 3,
                                        config->metrics_path,
                                        "/",
                                        json_files[i]
                                        );

         if (pgexporter_read_json(config->prometheus, idx_metrics, json_path, &number_of_metrics))
         {
            free(json_path);
            json_path = NULL;
            for (int j = 0; j < number_of_json_files; j++)
            {
               free(json_files[j]);
            }
            free(json_files);
            json_files = NULL;
            return 1;
         }
         idx_metrics += number_of_metrics;
         free(json_path);
         json_path = NULL;
      }
      for (int j = 0; j < number_of_json_files; j++)
      {
         free(json_files[j]);
      }
      free(json_files);
      json_files = NULL;
   }

   config->number_of_metrics = idx_metrics;
   return 0;
}

int
pgexporter_read_json(struct prometheus* prometheus, int prometheus_idx, char* filename, int* number_of_metrics)
{
   struct json* root = NULL;
   json_config_t json_config;
   int ret = 0;

   memset(&json_config, 0, sizeof(json_config_t));

   if (pgexporter_json_read_file(filename, &root))
   {
      pgexporter_log_error("pgexporter: Error reading JSON file: %s", filename);
      return 1;
   }

   if (pgexporter_json_contains_key(root, "version"))
   {
      json_config.default_version = (char)pgexporter_json_get(root, "version");
   }

   if (pgexporter_json_contains_key(root, "metrics"))
   {
      struct json* metrics_array = (struct json*)pgexporter_json_get(root, "metrics");
      if (parse_metrics(metrics_array, &json_config))
      {
         pgexporter_json_destroy(root);
         return 1;
      }
   }
   else
   {
      pgexporter_log_error("pgexporter: missing 'metrics' key in JSON file: %s", filename);
      pgexporter_json_destroy(root);
      return 1;
   }

   *number_of_metrics += json_config.n_metrics;

   ret = semantics_json(prometheus, prometheus_idx, &json_config);

   pgexporter_json_destroy(root);
   free_json_config(&json_config);
   return ret;
}

static int
parse_columns(struct json* columns_array, json_query_t* query)
{
   struct json_iterator* iter = NULL;

   if (pgexporter_json_iterator_create(columns_array, &iter))
   {
      return 1;
   }

   query->n_columns = pgexporter_json_array_length(columns_array);

   query->columns = malloc(sizeof(json_column_t) * query->n_columns);
   memset(query->columns, 0, sizeof(json_column_t) * query->n_columns);

   int column_idx = 0;

   while (pgexporter_json_iterator_next(iter))
   {
      struct json* column = (struct json*)iter->value->data;
      json_column_t* current_column = &query->columns[column_idx];

      // Required fields
      if (!pgexporter_json_contains_key(column, "type"))
      {
         pgexporter_log_error("Missing required field: type");
         return 1;
      }
      current_column->type = strdup((char*)pgexporter_json_get(column, "type"));

      // Optional fields
      if (pgexporter_json_contains_key(column, "description"))
      {
         current_column->description = strdup((char*)pgexporter_json_get(column, "description"));
      }
      else
      {
         current_column->description = strdup("");     // empty default
      }
      if (pgexporter_json_contains_key(column, "name"))
      {
         current_column->name = strdup((char*)pgexporter_json_get(column, "name"));
      }
      else
      {
         current_column->name = strdup("");     // empty default
      }

      // Check if this is a histogram type
      if (strcmp(current_column->type, "histogram") == 0)
      {
         query->is_histogram = true;
      }

      column_idx++;
   }

   pgexporter_json_iterator_destroy(iter);
   return 0;
}

static int
parse_queries(struct json* queries_array, json_metric_t* metric)
{
   struct json_iterator* iter = NULL;

   if (pgexporter_json_iterator_create(queries_array, &iter))
   {
      return 1;
   }

   metric->n_queries = pgexporter_json_array_length(queries_array);

   metric->queries = malloc(sizeof(json_query_t) * metric->n_queries);
   memset(metric->queries, 0, sizeof(json_query_t) * metric->n_queries);

   int query_idx = 0;

   while (pgexporter_json_iterator_next(iter))
   {
      struct json* query = (struct json*)iter->value->data;
      json_query_t* current_query = &metric->queries[query_idx];

      // Required field
      if (!pgexporter_json_contains_key(query, "query"))
      {
         pgexporter_log_error("Missing required field: query");
         return 1;
      }
      current_query->query = strdup((char*)pgexporter_json_get(query, "query"));

      // Version - optional with default from config
      if (pgexporter_json_contains_key(query, "version"))
      {
         uintptr_t version_val = pgexporter_json_get(query, "version");
         current_query->version = (char)version_val;
      }

      if (pgexporter_json_contains_key(query, "columns"))
      {
         struct json* columns = (struct json*)pgexporter_json_get(query, "columns");
         if (parse_columns(columns, current_query))
         {
            pgexporter_log_error("Error parsing columns for query index %d", query_idx);
            return 1;
         }
      }
      query_idx++;
   }

   pgexporter_json_iterator_destroy(iter);
   return 0;
}

static int
parse_metrics(struct json* metrics_array, json_config_t* config)
{
   struct json_iterator* iter = NULL;

   if (pgexporter_json_iterator_create(metrics_array, &iter))
   {
      return 1;
   }

   config->n_metrics = pgexporter_json_array_length(metrics_array);

   config->metrics = malloc(sizeof(json_metric_t) * config->n_metrics);
   memset(config->metrics, 0, sizeof(json_metric_t) * config->n_metrics);

   int metric_idx = 0;

   while (pgexporter_json_iterator_next(iter))
   {
      struct json* metric = (struct json*)iter->value->data;
      json_metric_t* current_metric = &config->metrics[metric_idx];

      if (!pgexporter_json_contains_key(metric, "tag") ||
          !pgexporter_json_contains_key(metric, "collector"))
      {
         pgexporter_log_error("Missing required fields: tag or collector");
         return 1;
      }
      current_metric->tag = strdup((char*)pgexporter_json_get(metric, "tag"));
      current_metric->collector = strdup((char*)pgexporter_json_get(metric, "collector"));

      // Optional fields
      if (pgexporter_json_contains_key(metric, "sort"))
      {
         current_metric->sort = strdup((char*)pgexporter_json_get(metric, "sort"));
      }
      else
      {
         current_metric->sort = strdup("name");     // default
      }

      if (pgexporter_json_contains_key(metric, "server"))
      {
         current_metric->server = strdup((char*)pgexporter_json_get(metric, "server"));
      }
      else
      {
         current_metric->server = strdup("both");     // default
      }

      if (pgexporter_json_contains_key(metric, "queries"))
      {
         struct json* queries = (struct json*)pgexporter_json_get(metric, "queries");
         if (parse_queries(queries, current_metric))
         {
            pgexporter_log_error("Error parsing queries for metric %d", metric_idx);
            return 1;
         }
      }
      metric_idx++;
   }

   pgexporter_json_iterator_destroy(iter);
   return 0;
}

int
get_json_files(char* base, int* number_of_json_files, char*** files)
{
   int number_of_all_files = 0;
   char** all_files = NULL;
   char** array = NULL;
   int nof = 0;
   int n;

   if (pgexporter_get_files(base, &number_of_all_files, &all_files))
   {
      goto error;
   }

   for (int i = 0; i < number_of_all_files; i++)
   {
      if (is_json_file(all_files[i]))
      {
         nof++;
      }
   }

   array = (char**)malloc(sizeof(char*) * nof);
   n = 0;

   for (int i = 0; i < number_of_all_files; i++)
   {
      if (is_json_file(all_files[i]))
      {
         array[n] = (char*)malloc(strlen(all_files[i]) + 1);
         memset(array[n], 0, strlen(all_files[i]) + 1);
         memcpy(array[n], all_files[i], strlen(all_files[i]));
         n++;
      }
   }

   *number_of_json_files = nof;
   *files = array;

error:
   for (int i = 0; i < number_of_all_files; i++)
   {
      free(all_files[i]);
   }
   free(all_files);

   return 0;
}

bool
is_json_file(char* file)
{
   if (pgexporter_ends_with(file, ".json"))
   {
      return true;
   }
   return false;
}

static void
free_json_columns(json_column_t** columns, size_t n_columns)
{
   for (int i = 0; i < n_columns; i++)
   {
      if ((*columns)[i].description)
      {
         free((*columns)[i].description);
      }
      if ((*columns)[i].name)
      {
         free((*columns)[i].name);
      }
      if ((*columns)[i].type)
      {
         free((*columns)[i].type);
      }
   }

   free(*columns);
   *columns = NULL;
}

static void
free_json_queries(json_query_t** queries, size_t n_queries)
{
   for (int i = 0 ; i < n_queries; i++)
   {
      if ((*queries)[i].query)
      {
         free((*queries)[i].query);
      }
      if ((*queries)[i].columns)
      {
         free_json_columns(&(*queries)[i].columns, (*queries)[i].n_columns);
      }
   }

   free(*queries);
   *queries = NULL;
}

static void
free_json_metrics(json_metric_t** metrics, size_t n_metrics)
{
   for (int i = 0; i < n_metrics; i++)
   {
      if ((*metrics)[i].tag)
      {
         free((*metrics)[i].tag);
      }
      if ((*metrics)[i].sort)
      {
         free((*metrics)[i].sort);
      }
      if ((*metrics)[i].collector)
      {
         free((*metrics)[i].collector);
      }
      if ((*metrics)[i].server)
      {
         free((*metrics)[i].server);
      }
      if ((*metrics)[i].queries)
      {
         free_json_queries(&(*metrics)[i].queries, (*metrics)[i].n_queries);
      }
   }

   free(*metrics);
   *metrics = NULL;
}

static void
free_json_config(json_config_t* config)
{
   if (config->metrics)
   {
      free_json_metrics(&config->metrics, config->n_metrics);
   }
}

static int
semantics_json(struct prometheus* prometheus, int prometheus_idx, json_config_t* json_config)
{
   struct prometheus* prom = NULL;

   for (int i = 0; i < json_config->n_metrics; i++)
   {
      if (prometheus_idx + i >= NUMBER_OF_METRICS)
      {
         pgexporter_log_error("The number of metrics exceed the maximum limit of %d.", NUMBER_OF_METRICS);
         return 1;
      }

      if (json_config->metrics[i].tag == NULL)
      {
         pgexporter_log_error("No tag defined for '%s' (%d)",
                              json_config->metrics[i].queries != NULL ? json_config->metrics[i].queries->query : "Unknown",
                              prometheus_idx);
         return 1;
      }

      if (json_config->metrics[i].collector == NULL)
      {
         pgexporter_log_error("No collector defined for '%s' (%d)",
                              json_config->metrics[i].queries != NULL ? json_config->metrics[i].queries->query : "Unknown",
                              prometheus_idx);
         return 1;
      }

      prom = &prometheus[prometheus_idx + i];

      memcpy(prom->tag, json_config->metrics[i].tag, MIN(MISC_LENGTH - 1, strlen(json_config->metrics[i].tag)));
      memcpy(prom->collector, json_config->metrics[i].collector, MIN(MAX_COLLECTOR_LENGTH - 1, strlen(json_config->metrics[i].collector)));

      // Sort Type
      if (!json_config->metrics[i].sort || !strcmp(json_config->metrics[i].sort, "name"))
      {
         prom->sort_type = SORT_NAME;
      }
      else if (!strcmp(json_config->metrics[i].sort, "data"))
      {
         prom->sort_type = SORT_DATA0;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected sort_type %s", json_config->metrics[i].sort);
         return 1;
      }

      // Server Query Type
      if (!json_config->metrics[i].server || !strcmp(json_config->metrics[i].server, "both"))
      {
         prom->server_query_type = SERVER_QUERY_BOTH;
      }
      else if (!strcmp(json_config->metrics[i].server, "primary"))
      {
         prom->server_query_type = SERVER_QUERY_PRIMARY;
      }
      else if (!strcmp(json_config->metrics[i].server, "replica"))
      {
         prom->server_query_type = SERVER_QUERY_REPLICA;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected server %s", json_config->metrics[i].server);
         return 1;
      }

      // Queries
      for (int j = 0; j < json_config->metrics[i].n_queries; j++)
      {
         struct query_alts* new_query = NULL;
         void* new_query_shmem = NULL;

         pgexporter_create_shared_memory(sizeof(struct query_alts), HUGEPAGE_OFF, &new_query_shmem);
         new_query = (struct query_alts*) new_query_shmem;

         new_query->n_columns = MIN(json_config->metrics[i].queries[j].n_columns, MAX_NUMBER_OF_COLUMNS);

         memcpy(new_query->query, json_config->metrics[i].queries[j].query, MIN(MAX_QUERY_LENGTH - 1, strlen(json_config->metrics[i].queries[j].query)));
         new_query->version = json_config->metrics[i].queries[j].version;

         // Columns
         for (int k = 0; k < new_query->n_columns; k++)
         {
            // Name
            if (json_config->metrics[i].queries[j].columns[k].name)
            {
               memcpy(new_query->columns[k].name, json_config->metrics[i].queries[j].columns[k].name, MIN(MISC_LENGTH - 1, strlen(json_config->metrics[i].queries[j].columns[k].name)));
            }

            // Description
            if (json_config->metrics[i].queries[j].columns[k].description)
            {
               memcpy(new_query->columns[k].description, json_config->metrics[i].queries[j].columns[k].description, MIN(MISC_LENGTH - 1, strlen(json_config->metrics[i].queries[j].columns[k].description)));
            }

            // Type
            if (!strcmp(json_config->metrics[i].queries[j].columns[k].type, "label"))
            {
               new_query->columns[k].type = LABEL_TYPE;
            }
            else if (!strcmp(json_config->metrics[i].queries[j].columns[k].type, "counter"))
            {
               new_query->columns[k].type = COUNTER_TYPE;
            }
            else if (!strcmp(json_config->metrics[i].queries[j].columns[k].type, "gauge"))
            {
               new_query->columns[k].type = GAUGE_TYPE;
            }
            else if (!strcmp(json_config->metrics[i].queries[j].columns[k].type, "histogram"))
            {
               new_query->columns[k].type = HISTOGRAM_TYPE;
               new_query->is_histogram = true;
            }
            else
            {
               pgexporter_log_error("pgexporter: unexpected type %s", json_config->metrics[i].queries[j].columns[k].type);
               return 1;
            }
         }

         if (json_config->metrics[i].queries[j].version == 0)
         {
            new_query->version = json_config->default_version;
         }

         prom->root = pgexporter_insert_node_avl(prom->root, &new_query);
      }
   }

   return 0;
}