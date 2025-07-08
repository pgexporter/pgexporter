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
#include <extension.h>
#include <ext_query_alts.h>
#include <internal.h>
#include <logging.h>
#include <pg_query_alts.h>
#include <shmem.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>
#include <value.h>
#include <yaml_configuration.h>

/* system */
#include <yaml.h>
#include <errno.h>

static int pgexporter_read_yaml(struct prometheus* prometheus, int prometheus_idx, char* filename, int* number_of_metrics);

static int get_yaml_files(char* base, int* number_of_yaml_files, char*** files);
static bool is_yaml_file(char* filename);

/* YAML Parsing */

/* YAML file's structure definitions */

// Column's Value's Structure
typedef struct yaml_column
{
   char* name;
   char* description;
   char* type;
} __attribute__ ((aligned (64))) yaml_column_t;

// Query's Value's Structure
typedef struct yaml_query
{
   bool is_histogram;
   char* query;
   char version;
   yaml_column_t* columns;
   int n_columns;

   // For extensions
   char* version_str;
} __attribute__ ((aligned (64))) yaml_query_t;

// Metric's Value's Structure
typedef struct yaml_metric
{
   yaml_query_t* queries;
   int n_queries;
   char* tag;
   char* sort;
   char* collector;
   char* server;
   bool exec_on_all_dbs;
} __attribute__ ((aligned (64))) yaml_metric_t;

// Config's Structure
typedef struct yaml_config
{
   yaml_metric_t* metrics;
   int n_metrics;
   char default_version;

   // For extensions
   bool is_extension;
   char* extension_name;
} __attribute__ ((aligned (64))) yaml_config_t;

// Possible states of YAML parser (for additional check over libyaml's state)
typedef enum {
   PARSER_INIT,
   PARSER_STREAM_START,
   PARSER_STREAM_END,
   PARSER_DOC_START,
   PARSER_DOC_END,
   PARSER_MAP_START,
   PARSER_MAP_END,
   PARSER_SEQ_START,
   PARSER_SEQ_END,
   PARSER_KEY,
   PARSER_VALUE
} parser_state_t;

// Parses a YAML file from its file pointer and loads it into `yaml_config`
static int parse_yaml(FILE* file, yaml_config_t* yaml_config);

// Parses the value of `metrics` key in YAML
static int parse_metrics(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, yaml_config_t* yaml_config, yaml_metric_t** metrics, int* n_metrics);

// Parses the value of `queries` key in YAML
static int parse_queries(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, yaml_config_t* yaml_config, yaml_query_t** queries, int* n_queries);

// Parses the value of `columns` key in YAML
static int parse_columns(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, yaml_query_t* query, yaml_column_t** columns, int* n_columns);

// Extract an integer scalar from a parsed string in YAML, and convert it into a character
static int parse_itoc(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, char* dest);

// Extract an integer scalar from a parsed string in YAML
static int parse_int(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, int* dest);

// Parse a string scalar in YAML
static int parse_string(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, char** dest);

// Parse 'database' key in YAML
static int parse_exec_all_dbs(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, bool* exec_on_all_dbs);

// Parse a scalar value in YAML
static int parse_value(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr);

// Free allocated memory for YAML config
static void free_yaml_config(yaml_config_t* config);

// Free allocated memory for YAML metric's structure
static void free_yaml_metrics(yaml_metric_t** metrics, size_t n_metrics);

// Free allocated memory for YAML queries' structure
static void free_yaml_queries(yaml_query_t** queries, size_t n_queries);

// Free allocated memory for YAML columns
static void free_yaml_columns(yaml_column_t** columns, size_t n_columns);

// Extract the meaning of the `yaml_config` and load the metrics into `prometheus`
static int semantics_yaml(struct prometheus* prometheus, int prometheus_idx, yaml_config_t* yaml_config);

// Extension helper functions
static struct extension_metrics* search_or_add_extension(struct configuration* config, char* extension_name);
static int semantics_extension_yaml(struct configuration* config, yaml_config_t* yaml_config);
static int pgexporter_validate_yaml_metrics(struct configuration* config, yaml_config_t* yaml_config);

int
pgexporter_read_metrics_configuration(void* shmem)
{
   struct configuration* config;
   int idx_metrics = 0;
   int number_of_metrics = 0;
   int number_of_yaml_files = 0;
   char** yaml_files = NULL;
   char* yaml_path = NULL;

   config = (struct configuration*) shmem;
   idx_metrics = config->number_of_metrics;

   if (pgexporter_is_file(config->metrics_path))
   {
      number_of_metrics = 0;
      if (pgexporter_read_yaml(config->prometheus, idx_metrics, config->metrics_path, &number_of_metrics))
      {
         return 1;
      }
      idx_metrics += number_of_metrics;
   }
   else if (pgexporter_is_directory(config->metrics_path))
   {
      get_yaml_files(config->metrics_path, &number_of_yaml_files, &yaml_files);
      for (int i = 0; i < number_of_yaml_files; i++)
      {
         number_of_metrics = 0;

         yaml_path = pgexporter_vappend(yaml_path, 3,
                                        config->metrics_path,
                                        "/",
                                        yaml_files[i]
                                        );

         if (pgexporter_read_yaml(config->prometheus, idx_metrics, yaml_path, &number_of_metrics))
         {
            free(yaml_path);
            yaml_path = NULL;
            for (int j = 0; j < number_of_yaml_files; j++)
            {
               free(yaml_files[j]);
            }
            free(yaml_files);
            yaml_files = NULL;
            return 1;
         }
         idx_metrics += number_of_metrics;
         free(yaml_path);
         yaml_path = NULL;
      }
      for (int j = 0; j < number_of_yaml_files; j++)
      {
         free(yaml_files[j]);
      }
      free(yaml_files);
      yaml_files = NULL;
   }
   config->number_of_metrics = idx_metrics;

   return 0;
}

int
pgexporter_read_internal_yaml_metrics(struct configuration* config, bool start)
{
   int number_of_metrics = 0;
   int ret;
   FILE* internal_yaml_ptr = fmemopen(INTERNAL_YAML, strlen(INTERNAL_YAML), "r");

   ret = pgexporter_read_yaml_from_file_pointer(config->prometheus, 0, &number_of_metrics, internal_yaml_ptr);
   fclose(internal_yaml_ptr);

   if (ret)
   {
      return 1;
   }

   if (start)
   {
      config->number_of_metrics = 0;
   }

   config->number_of_metrics += number_of_metrics;

   return 0;
}

static int
pgexporter_read_yaml(struct prometheus* prometheus, int prometheus_idx, char* filename, int* number_of_metrics)
{
   FILE* file;

   file = fopen(filename, "r");
   if (file == NULL)
   {
      pgexporter_log_error("pgexporter: fopen error %s", strerror(errno));
      return 1;
   }

   int ret = pgexporter_read_yaml_from_file_pointer(prometheus, prometheus_idx, number_of_metrics, file);

   fclose(file);

   return ret;
}

static int
pgexporter_validate_yaml_metrics(struct configuration* config, yaml_config_t* yaml_config)
{
   struct art* existing_metrics_art = NULL;
   struct art* temp_art = NULL;
   struct art* metric_columns_art = NULL;
   struct art* processed_columns = NULL;
   char column_metric_name[MISC_LENGTH];
   char final_metric_name[MISC_LENGTH];
   int i, j, k;

   if (pgexporter_art_create(&existing_metrics_art))
   {
      pgexporter_log_error("Failed to create temporary ART");
      goto error;
   }
   
   for (int idx = 0; idx < config->number_of_metric_names; idx++)
   {
      if (pgexporter_art_insert(existing_metrics_art, config->metric_names[idx], 1, ValueInt32))
      {
         pgexporter_log_error("Failed to insert metric name into temporary ART");
         goto error;
      }
   }

   if (pgexporter_art_create(&temp_art))
   {
      pgexporter_log_error("Failed to create temporary ART");
      goto error;
   }

   /* temp_art detects duplicate within this file */
   for (i = 0; i < yaml_config->n_metrics; i++)
   {
      if (yaml_config->metrics[i].tag == NULL)
      {
         pgexporter_log_error("No tag defined for metric %d", i);
         goto error;
      }

      /* Collect unique column names for this metric across all query versions */
      if (pgexporter_art_create(&metric_columns_art))
      {
         pgexporter_log_error("Failed to create metric columns ART");
         goto error;
      }

      /* Iterate through all queries for this metric to collect unique column names */
      for (j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {
         for (k = 0; k < yaml_config->metrics[i].queries[j].n_columns; k++)
         {
            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))
            {
               continue;
            }

            /* Generate the column-specific metric name */
            if (yaml_config->metrics[i].queries[j].columns[k].name &&
                strlen(yaml_config->metrics[i].queries[j].columns[k].name) > 0)
            {
               snprintf(column_metric_name, sizeof(column_metric_name), "%s",
                        yaml_config->metrics[i].queries[j].columns[k].name);
            }
            else
            {
               /* If no column name, use empty string to represent the base metric */
               snprintf(column_metric_name, sizeof(column_metric_name), "");
            }

            /* Add to metric-specific ART to track unique columns for this metric */
            if (pgexporter_art_insert(metric_columns_art, column_metric_name, 1, ValueInt32))
            {
               pgexporter_log_error("Failed to insert column name into metric columns ART");
               goto error;
            }
         }
      }

      /* Now validate the unique column names for this metric */
      /* We need to iterate through the unique columns we collected */
      /* Since we can't easily iterate through an ART, we'll rebuild the final names */

      /* Re-iterate but now only check unique column names */
      if (pgexporter_art_create(&processed_columns))
      {
         pgexporter_log_error("Failed to create processed columns ART");
         goto error;
      }

      for (j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {
         for (k = 0; k < yaml_config->metrics[i].queries[j].n_columns; k++)
         {
            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))
            {
               continue;
            }

            /* Generate the column-specific metric name */
            if (yaml_config->metrics[i].queries[j].columns[k].name &&
                strlen(yaml_config->metrics[i].queries[j].columns[k].name) > 0)
            {
               snprintf(column_metric_name, sizeof(column_metric_name), "%s",
                        yaml_config->metrics[i].queries[j].columns[k].name);
            }
            else
            {
               snprintf(column_metric_name, sizeof(column_metric_name), "");
            }

            /* Skip if we already processed this column name for this metric */
            if (pgexporter_art_contains_key(processed_columns, column_metric_name))
            {
               continue;
            }

            /* Mark this column as processed */
            if (pgexporter_art_insert(processed_columns, column_metric_name, 1, ValueInt32))
            {
               pgexporter_log_error("Failed to insert into processed columns ART");
               goto error;
            }

            /* Generate the final metric name */
            if (yaml_config->is_extension)
            {
               snprintf(final_metric_name, sizeof(final_metric_name), "%s_%s",
                        yaml_config->extension_name, yaml_config->metrics[i].tag);
            }
            else
            {
               snprintf(final_metric_name, sizeof(final_metric_name), "%s",
                        yaml_config->metrics[i].tag);
            }

            if (strlen(column_metric_name) > 0)
            {
               snprintf(final_metric_name + strlen(final_metric_name),
                        sizeof(final_metric_name) - strlen(final_metric_name),
                        "_%s", column_metric_name);
            }

            if (!pgexporter_is_valid_metric_name(final_metric_name))
            {
               pgexporter_log_error("Invalid characters in metric name: pgexporter_%s", final_metric_name);
               goto error;
            }

            /* Check for duplicates against global ART */
            if (pgexporter_art_contains_key(existing_metrics_art, final_metric_name))
            {
               pgexporter_log_error("Duplicate metric name with previously loaded files: pgexporter_%s", final_metric_name);
               goto error;
            }

            /* Check for duplicates within this file */
            if (pgexporter_art_contains_key(temp_art, final_metric_name))
            {
               pgexporter_log_error("Duplicate metric name within same file: pgexporter_%s", final_metric_name);
               goto error;
            }

            /* Add to temp ART for this file */
            if (pgexporter_art_insert(temp_art, final_metric_name, 1, ValueInt32))
            {
               pgexporter_log_error("Failed to insert metric name into temporary ART");
               goto error;
            }
         }
      }

      pgexporter_art_destroy(processed_columns);
      processed_columns = NULL;
      pgexporter_art_destroy(metric_columns_art);
      metric_columns_art = NULL;
   }

   pgexporter_art_destroy(existing_metrics_art);
   pgexporter_art_destroy(temp_art);
   return 0;

error:
   pgexporter_art_destroy(existing_metrics_art);
   pgexporter_art_destroy(processed_columns);
   pgexporter_art_destroy(metric_columns_art);
   pgexporter_art_destroy(temp_art);
   return 1;
}

int
pgexporter_read_yaml_from_file_pointer(struct prometheus* prometheus, int prometheus_idx, int* number_of_metrics, FILE* file)
{
   int ret = 0;
   yaml_config_t yaml_config;
   struct configuration* config = (struct configuration*)shmem;

   memset(&yaml_config, 0, sizeof(yaml_config_t));

   if (parse_yaml(file, &yaml_config))
   {
      ret = 1;
      goto end;
   }

   *number_of_metrics += yaml_config.n_metrics;

   /* Validate before inserting them */
   if (pgexporter_validate_yaml_metrics(config, &yaml_config))
   {
      pgexporter_log_error("YAML contains duplicate metric names");
      ret = 1;
      goto end;
   }

   if (yaml_config.is_extension)
   {
      if (semantics_extension_yaml(config, &yaml_config))
      {
         ret = 1;
         goto end;
      }
   }
   else
   {
      if (semantics_yaml(prometheus, prometheus_idx, &yaml_config))
      {
         ret = 1;
         goto end;
      }
   }

end:
   if (yaml_config.extension_name)
   {
      free(yaml_config.extension_name);
   }
   free_yaml_config(&yaml_config);

   return ret;
}

static int
get_yaml_files(char* base, int* number_of_yaml_files, char*** files)
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
      if (is_yaml_file(all_files[i]))
      {
         nof++;
      }
   }

   array = (char**)malloc(sizeof(char*) * nof);
   n = 0;

   for (int i = 0; i < number_of_all_files; i++)
   {
      if (is_yaml_file(all_files[i]))
      {
         array[n] = (char*)malloc(strlen(all_files[i]) + 1);
         memset(array[n], 0, strlen(all_files[i]) + 1);
         memcpy(array[n], all_files[i], strlen(all_files[i]));
         n++;
      }
   }

   *number_of_yaml_files = nof;
   *files = array;

error:
   for (int i = 0; i < number_of_all_files; i++)
   {
      free(all_files[i]);
   }
   free(all_files);

   return 1;
}

static bool
is_yaml_file(char* file)
{
   if (pgexporter_ends_with(file, ".yaml") || pgexporter_ends_with(file, ".yml"))
   {
      return true;
   }
   return false;
}

/* YAML Parsing */

static int
parse_yaml(FILE* file, yaml_config_t* yaml_config)
{
   /* State enables checking if the token that is parsed shouuld be allowed,
      or the possible options of where to go from here. Basically a DFA. */
   parser_state_t state = PARSER_INIT;
   char* buf;

   if (!file)
   {
      return 0;
   }

   yaml_parser_t parser;
   yaml_event_t event;

   if (!yaml_parser_initialize(&parser))
   {
      pgexporter_log_error("pgexporter: yaml_parser_initialize error");
      return 1;
   }

   yaml_parser_set_input_file(&parser, file);

   /* Event based YAML parsing using libyaml as event-based is more suitable for
      complex structures of YAML files such as this case. */
   buf = NULL;

   do
   {
      if (!yaml_parser_parse(&parser, &event))
      {
         yaml_event_delete(&event);
         goto error;
      }

      switch (event.type)
      {

         case YAML_NO_EVENT:
            goto error;

         /* Start */

         case YAML_STREAM_START_EVENT:
            if (state != PARSER_INIT)
            {
               goto error;
            }

            state = PARSER_STREAM_START;
            break;

         case YAML_DOCUMENT_START_EVENT:
            if (state != PARSER_STREAM_START)
            {
               goto error;
            }

            state = PARSER_DOC_START;
            break;

         case YAML_MAPPING_START_EVENT:
            if (state != PARSER_DOC_START && state != PARSER_SEQ_START)
            {
               goto error;
            }

            state = PARSER_MAP_START;
            break;

         /* Keys */

         case YAML_SCALAR_EVENT:
            if (state != PARSER_VALUE && state != PARSER_MAP_START && state != PARSER_SEQ_END)
            {
               goto error;
            }

            state = PARSER_KEY;

            buf = strdup((char*) event.data.scalar.value);

            if (!strcmp(buf, "extension"))
            {
               yaml_config->is_extension = true;
               if (parse_string(&parser, &event, &state, &yaml_config->extension_name))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "version"))
            {
               if (parse_itoc(&parser, &event, &state, &yaml_config->default_version))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "metrics"))
            {
               if (parse_metrics(&parser, &event, &state, yaml_config, &yaml_config->metrics, &yaml_config->n_metrics))
               {
                  goto error;
               }
            }
            else
            {
               goto error;
            }

            free(buf);
            buf = NULL;
            break;

         /* End */

         case YAML_MAPPING_END_EVENT:
            if (state != PARSER_VALUE && state != PARSER_SEQ_END)
            {
               goto error;
            }

            state = PARSER_MAP_END;
            break;

         case YAML_DOCUMENT_END_EVENT:
            if (state != PARSER_MAP_END)
            {
               goto error;
            }

            state = PARSER_DOC_END;
            break;

         case YAML_STREAM_END_EVENT:
            if (state != PARSER_DOC_END)
            {
               goto error;
            }

            state = PARSER_STREAM_END;
            break;

         default:
            goto error;

      }

      if (event.type != YAML_STREAM_END_EVENT)
      {
         yaml_event_delete(&event);
      }

   }
   while (event.type != YAML_STREAM_END_EVENT);

   yaml_parser_delete(&parser);

   return 0;

error:
   if (event.type != YAML_STREAM_END_EVENT)
   {
      yaml_event_delete(&event);
   }

   if (buf)
   {
      free(buf);
      buf = NULL;
   }

   yaml_parser_delete(&parser);
   pgexporter_log_error("pgexporter: Incorrect YAML format\n");
   return 1;
}

static int
parse_metrics(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, yaml_config_t* yaml_config, yaml_metric_t** metrics, int* n_metrics)
{
   yaml_event_delete(event_ptr);

   char* buf = NULL;

   do
   {
      if (!yaml_parser_parse(parser_ptr, event_ptr))
      {
         yaml_event_delete(event_ptr);
         goto error;
      }

      switch (event_ptr->type)
      {

         case YAML_NO_EVENT:
            goto error;

         /* Start */

         case YAML_SEQUENCE_START_EVENT:
            if (*state_ptr != PARSER_KEY)
            {
               goto error;
            }

            *state_ptr = PARSER_SEQ_START;
            break;

         case YAML_MAPPING_START_EVENT:
            if (*state_ptr != PARSER_SEQ_START && *state_ptr != PARSER_MAP_END)
            {
               goto error;
            }
            *metrics = realloc(*metrics, (*n_metrics + 1) * sizeof(yaml_metric_t));
            memset(&(*metrics)[*n_metrics], 0, sizeof(yaml_metric_t));
            *state_ptr = PARSER_MAP_START;
            break;

         /* Keys */

         case YAML_SCALAR_EVENT:
            if (*state_ptr != PARSER_VALUE && *state_ptr != PARSER_MAP_START && *state_ptr != PARSER_SEQ_END)
            {
               goto error;
            }

            *state_ptr = PARSER_KEY;

            buf = strdup((char*) event_ptr->data.scalar.value);

            if (!strcmp(buf, "tag"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*metrics)[*n_metrics].tag))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "metric"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*metrics)[*n_metrics].tag))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "sort"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*metrics)[*n_metrics].sort))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "server"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*metrics)[*n_metrics].server))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "collector"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*metrics)[*n_metrics].collector))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "queries"))
            {
               if (parse_queries(parser_ptr, event_ptr, state_ptr, yaml_config, &(*metrics)[*n_metrics].queries, &(*metrics)[*n_metrics].n_queries))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "database"))
            {
               if (parse_exec_all_dbs(parser_ptr, event_ptr, state_ptr, &(*metrics)[*n_metrics].exec_on_all_dbs))
               {
                  goto error;
               }
            }
            else
            {
               goto error;
            }

            free(buf);
            buf = NULL;
            break;

         /* End */

         case YAML_MAPPING_END_EVENT:
            if (*state_ptr != PARSER_VALUE && *state_ptr != PARSER_SEQ_END)
            {
               goto error;
            }
            (*n_metrics)++;
            *state_ptr = PARSER_MAP_END;
            break;

         case YAML_SEQUENCE_END_EVENT:
            if (*state_ptr != PARSER_MAP_END)
            {
               goto error;
            }
            *state_ptr = PARSER_SEQ_END;
            yaml_event_delete(event_ptr);
            goto ok;

         default:
            goto error;

      }

      yaml_event_delete(event_ptr);
   }
   while (1);

ok:
   return 0;

error:
   if (buf)
   {
      free(buf);
      buf = NULL;
   }

   if (event_ptr && event_ptr->type != YAML_SEQUENCE_END_EVENT)
   {
      yaml_event_delete(event_ptr);
   }
   return 1;
}

static int
parse_queries(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, yaml_config_t* yaml_config, yaml_query_t** queries, int* n_queries)
{
   yaml_event_delete(event_ptr);

   char* buf = NULL;

   do
   {
      if (!yaml_parser_parse(parser_ptr, event_ptr))
      {
         yaml_event_delete(event_ptr);
         goto error;
      }

      switch (event_ptr->type)
      {

         case YAML_NO_EVENT:
            goto error;

         /* Start */

         case YAML_SEQUENCE_START_EVENT:
            if (*state_ptr != PARSER_KEY)
            {
               goto error;
            }

            *state_ptr = PARSER_SEQ_START;
            break;

         case YAML_MAPPING_START_EVENT:
            if (*state_ptr != PARSER_SEQ_START && *state_ptr != PARSER_MAP_END)
            {
               goto error;
            }

            *queries = realloc(*queries, (*n_queries + 1) * sizeof(yaml_query_t));
            memset(&(*queries)[*n_queries], 0, sizeof(yaml_query_t));

            *state_ptr = PARSER_MAP_START;
            break;

         /* Keys */

         case YAML_SCALAR_EVENT:
            if (*state_ptr != PARSER_VALUE && *state_ptr != PARSER_MAP_START && *state_ptr != PARSER_SEQ_END)
            {
               goto error;
            }

            *state_ptr = PARSER_KEY;

            buf = strdup((char*) event_ptr->data.scalar.value);

            if (!strcmp(buf, "query"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*queries)[*n_queries].query))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "version"))
            {
               if (yaml_config->is_extension)
               {
                  if (parse_string(parser_ptr, event_ptr, state_ptr, &(*queries)[*n_queries].version_str))
                  {
                     goto error;
                  }
               }
               else
               {
                  if (parse_int(parser_ptr, event_ptr, state_ptr, (int*) &(*queries)[*n_queries].version))
                  {
                     goto error;
                  }
               }
            }
            else if (!strcmp(buf, "columns"))
            {
               if (parse_columns(parser_ptr, event_ptr, state_ptr, &(*queries)[*n_queries], &(*queries)[*n_queries].columns, &(*queries)[*n_queries].n_columns))
               {
                  goto error;
               }
            }
            else
            {
               goto error;
            }

            free(buf);
            buf = NULL;
            break;

         /* End */

         case YAML_MAPPING_END_EVENT:
            if (*state_ptr != PARSER_VALUE && *state_ptr != PARSER_SEQ_END)
            {
               goto error;
            }
            (*n_queries)++;
            *state_ptr = PARSER_MAP_END;
            break;

         case YAML_SEQUENCE_END_EVENT:
            if (*state_ptr != PARSER_MAP_END)
            {
               goto error;
            }
            *state_ptr = PARSER_SEQ_END;
            goto ok;

         default:
            goto error;

      }

   }
   while (1);

ok:
   return 0;

error:
   if (buf)
   {
      free(buf);
      buf = NULL;
   }

   if (event_ptr && event_ptr->type != YAML_SEQUENCE_END_EVENT)
   {
      yaml_event_delete(event_ptr);
   }
   return 1;
}

static int
parse_columns(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, yaml_query_t* query, yaml_column_t** columns, int* n_columns)
{
   yaml_event_delete(event_ptr);

   char* buf = NULL;

   do
   {

      if (!yaml_parser_parse(parser_ptr, event_ptr))
      {
         yaml_event_delete(event_ptr);
         goto error;
      }

      switch (event_ptr->type)
      {

         case YAML_NO_EVENT:
            goto error;

         /* Start */

         case YAML_SEQUENCE_START_EVENT:
            if (*state_ptr != PARSER_KEY)
            {
               goto error;
            }

            *state_ptr = PARSER_SEQ_START;
            break;

         case YAML_MAPPING_START_EVENT:
            if (*state_ptr != PARSER_SEQ_START && *state_ptr != PARSER_MAP_END)
            {
               goto error;
            }
            *columns = realloc(*columns, (*n_columns + 1) * sizeof(yaml_column_t));
            memset(&(*columns)[*n_columns], 0, sizeof(yaml_column_t));
            *state_ptr = PARSER_MAP_START;
            break;

         /* Keys */

         case YAML_SCALAR_EVENT:
            if (*state_ptr != PARSER_VALUE && *state_ptr != PARSER_MAP_START && *state_ptr != PARSER_SEQ_END)
            {
               goto error;
            }

            *state_ptr = PARSER_KEY;

            buf = strdup((char*) event_ptr->data.scalar.value);

            if (!strcmp(buf, "name"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*columns)[*n_columns].name))
               {
                  goto error;
               }
            }
            else if (!strcmp(buf, "type"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*columns)[*n_columns].type))
               {
                  goto error;
               }

               if (!strcmp((*columns)[*n_columns].type, "histogram"))
               {
                  query->is_histogram = true;
               }
            }
            else if (!strcmp(buf, "description"))
            {
               if (parse_string(parser_ptr, event_ptr, state_ptr, &(*columns)[*n_columns].description))
               {
                  goto error;
               }
            }
            else
            {
               goto error;
            }

            free(buf);
            buf = NULL;
            break;

         /* End */

         case YAML_MAPPING_END_EVENT:
            if (*state_ptr != PARSER_VALUE && *state_ptr != PARSER_SEQ_END)
            {
               goto error;
            }
            (*n_columns)++;
            *state_ptr = PARSER_MAP_END;
            break;

         case YAML_SEQUENCE_END_EVENT:
            if (*state_ptr != PARSER_MAP_END)
            {
               goto error;
            }
            *state_ptr = PARSER_SEQ_END;
            break;

         default:
            goto error;

      }

   }
   while (event_ptr->type != YAML_SEQUENCE_END_EVENT);

   return 0;

error:
   if (buf)
   {
      free(buf);
      buf = NULL;
   }

   if (event_ptr && event_ptr->type != YAML_SEQUENCE_END_EVENT)
   {
      yaml_event_delete(event_ptr);
   }
   return 1;
}

static int
parse_itoc(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, char* dest)
{
   yaml_event_delete(event_ptr);

   if (parse_value(parser_ptr, event_ptr, state_ptr))
   {
      return 1;
   }

   *dest = atoi((char*) event_ptr->data.scalar.value);
   yaml_event_delete(event_ptr);
   return 0;
}

static int
parse_int(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, int* dest)
{
   yaml_event_delete(event_ptr);

   if (parse_value(parser_ptr, event_ptr, state_ptr))
   {
      return 1;
   }

   *dest = atoi((char*) event_ptr->data.scalar.value);
   yaml_event_delete(event_ptr);
   return 0;
}

static int
parse_string(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, char** dest)
{
   yaml_event_delete(event_ptr);

   if (parse_value(parser_ptr, event_ptr, state_ptr))
   {
      return 1;
   }

   *dest = strdup((char*) event_ptr->data.scalar.value);
   yaml_event_delete(event_ptr);
   return 0;
}

static int
parse_exec_all_dbs(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr, bool* exec_on_all_dbs)
{
   char* dest = NULL;
   bool eoad = false;

   if (parse_string(parser_ptr, event_ptr, state_ptr, &dest))
   {
      goto error;
   }

   if (!strcmp(dest, "all"))
   {
      eoad = true;
   }
   else
   {
      eoad = false;
   }

   free(dest);
   *exec_on_all_dbs = eoad;
   return 0;

error:
   free(dest);
   return 1;
}

static int
parse_value(yaml_parser_t* parser_ptr, yaml_event_t* event_ptr, parser_state_t* state_ptr)
{
   yaml_event_delete(event_ptr);

   if (!yaml_parser_parse(parser_ptr, event_ptr))
   {
      return 1;
   }

   switch (event_ptr->type)
   {
      case YAML_SCALAR_EVENT:
         if (*state_ptr != PARSER_KEY)
         {
            return 1;
         }
         break;

      default:
         return 1;
   }

   *state_ptr = PARSER_VALUE;

   return 0;
}

static void
free_yaml_config(yaml_config_t* config)
{
   if (config->metrics)
   {
      free_yaml_metrics(&config->metrics, config->n_metrics);
   }
}

static void
free_yaml_metrics(yaml_metric_t** metrics, size_t n_metrics)
{

   for (size_t i = 0; i < n_metrics; i++)
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
         free_yaml_queries(&(*metrics)[i].queries, (*metrics)[i].n_queries);
      }
   }

   free(*metrics);
   *metrics = NULL;
}

static void
free_yaml_queries(yaml_query_t** queries, size_t n_queries)
{
   for (size_t i = 0 ; i < n_queries; i++)
   {
      if ((*queries)[i].query)
      {
         free((*queries)[i].query);
      }
      if ((*queries)[i].version_str)
      {
         free((*queries)[i].version_str);
      }
      if ((*queries)[i].columns)
      {
         free_yaml_columns(&(*queries)[i].columns, (*queries)[i].n_columns);
      }
   }

   free(*queries);
   *queries = NULL;
}

static void
free_yaml_columns(yaml_column_t** columns, size_t n_columns)
{
   for (size_t i = 0; i < n_columns; i++)
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

static int
semantics_yaml(struct prometheus* prometheus, int prometheus_idx, yaml_config_t* yaml_config)
{
   struct prometheus* prom = NULL;
   struct configuration* config = (struct configuration*)shmem;

   for (int i = 0; i < yaml_config->n_metrics; i++)
   {
      if (prometheus_idx + i >= NUMBER_OF_METRICS)
      {
         pgexporter_log_error("The number of metrics exceed the maximum limit of %d.", NUMBER_OF_METRICS);
         return 1;
      }

      if (yaml_config->metrics[i].tag == NULL)
      {
         pgexporter_log_error("No tag defined for '%s' (%d)",
                              yaml_config->metrics[i].queries != NULL ? yaml_config->metrics[i].queries->query : "Unknown",
                              prometheus_idx);
         return 1;
      }

      if (yaml_config->metrics[i].collector == NULL)
      {
         pgexporter_log_error("No collector defined for '%s' (%d)",
                              yaml_config->metrics[i].queries != NULL ? yaml_config->metrics[i].queries->query : "Unknown",
                              prometheus_idx);
         return 1;
      }

      prom = &prometheus[prometheus_idx + i];

      memcpy(prom->tag, yaml_config->metrics[i].tag, MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].tag)));
      memcpy(prom->collector, yaml_config->metrics[i].collector, MIN(MAX_COLLECTOR_LENGTH - 1, strlen(yaml_config->metrics[i].collector)));

      // Sort Type
      if (!yaml_config->metrics[i].sort || !strcmp(yaml_config->metrics[i].sort, "name"))
      {
         prom->sort_type = SORT_NAME;
      }
      else if (!strcmp(yaml_config->metrics[i].sort, "data"))
      {
         prom->sort_type = SORT_DATA0;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected sort_type %s", yaml_config->metrics[i].sort);
         return 1;
      }

      // Server Query Type
      if (!yaml_config->metrics[i].server || !strcmp(yaml_config->metrics[i].server, "both"))
      {
         prom->server_query_type = SERVER_QUERY_BOTH;
      }
      else if (!strcmp(yaml_config->metrics[i].server, "primary"))
      {
         prom->server_query_type = SERVER_QUERY_PRIMARY;
      }
      else if (!strcmp(yaml_config->metrics[i].server, "replica"))
      {
         prom->server_query_type = SERVER_QUERY_REPLICA;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected server %s", yaml_config->metrics[i].server);
         return 1;
      }

      prom->exec_on_all_dbs = yaml_config->metrics[i].exec_on_all_dbs;

      // Queries
      for (int j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {

         struct pg_query_alts* new_query = NULL;
         void* new_query_shmem = NULL;

         pgexporter_create_shared_memory(sizeof(struct pg_query_alts), HUGEPAGE_OFF, &new_query_shmem);
         new_query = (struct pg_query_alts*) new_query_shmem;

         new_query->node.n_columns = MIN(yaml_config->metrics[i].queries[j].n_columns, MAX_NUMBER_OF_COLUMNS);

         memcpy(new_query->node.query, yaml_config->metrics[i].queries[j].query, MIN(MAX_QUERY_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].query)));
         new_query->pg_version = yaml_config->metrics[i].queries[j].version;

         // Columns
         for (int k = 0; k < new_query->node.n_columns; k++)
         {

            // Name
            if (yaml_config->metrics[i].queries[j].columns[k].name)
            {
               memcpy(new_query->node.columns[k].name, yaml_config->metrics[i].queries[j].columns[k].name, MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].columns[k].name)));
            }

            // Description
            if (yaml_config->metrics[i].queries[j].columns[k].description)
            {
               memcpy(new_query->node.columns[k].description, yaml_config->metrics[i].queries[j].columns[k].description, MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].columns[k].description)));
            }

            // Type
            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))
            {
               new_query->node.columns[k].type = LABEL_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "counter"))
            {
               new_query->node.columns[k].type = COUNTER_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "gauge"))
            {
               new_query->node.columns[k].type = GAUGE_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "histogram"))
            {
               new_query->node.columns[k].type = HISTOGRAM_TYPE;
               new_query->node.is_histogram = true;
            }
            else
            {
               //unexpected type
               pgexporter_log_error("pgexporter: unexpected type %s", yaml_config->metrics[i].queries[j].columns[k].type);
               return 1;
            }

         }

         if (yaml_config->metrics[i].queries[j].version == 0)
         {
            new_query->pg_version = yaml_config->default_version;
         }

         prom->pg_root = pgexporter_insert_pg_node_avl(prom->pg_root, &new_query);
      }
      for (int j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {
         for (int k = 0; k < yaml_config->metrics[i].queries[j].n_columns; k++)
         {
            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))

            {
               continue;
            }

            char final_metric_name[MISC_LENGTH];
            snprintf(final_metric_name, sizeof(final_metric_name), "%s", yaml_config->metrics[i].tag);

            if (yaml_config->metrics[i].queries[j].columns[k].name &&
                strlen(yaml_config->metrics[i].queries[j].columns[k].name) > 0)
            {
               snprintf(final_metric_name + strlen(final_metric_name),
                        sizeof(final_metric_name) - strlen(final_metric_name),
                        "_%s", yaml_config->metrics[i].queries[j].columns[k].name);
            }

            if (config->number_of_metric_names < NUMBER_OF_METRIC_NAMES)
            {
               strncpy(config->metric_names[config->number_of_metric_names], 
                     final_metric_name, 
                     MISC_LENGTH - 1);
               config->metric_names[config->number_of_metric_names][MISC_LENGTH - 1] = '\0';
               config->number_of_metric_names++;
            }
            else
            {
               pgexporter_log_warn("Maximum metric names reached, skipping: %s", final_metric_name);
            }
         }
      }
   }

   return 0;
}

static struct extension_metrics*
search_or_add_extension(struct configuration* config, char* extension_name)
{
   for (int i = 0; i < config->number_of_extensions; i++)
   {
      if (!strcmp(config->extensions[i].extension_name, extension_name))
      {
         return &config->extensions[i];
      }
   }

   if (config->number_of_extensions >= NUMBER_OF_EXTENSIONS)
   {
      pgexporter_log_error("Maximum number of extensions exceeded");
      return NULL;
   }

   struct extension_metrics* ext = &config->extensions[config->number_of_extensions];
   memcpy(ext->extension_name, extension_name, MIN(MISC_LENGTH - 1, strlen(extension_name)));
   ext->number_of_metrics = 0;
   config->number_of_extensions++;

   return ext;
}

static int
semantics_extension_yaml(struct configuration* config, yaml_config_t* yaml_config)
{
   struct extension_metrics* ext = search_or_add_extension(config, yaml_config->extension_name);
   if (!ext)
   {
      return 1;
   }

   for (int i = 0; i < yaml_config->n_metrics; i++)
   {
      if (ext->number_of_metrics >= NUMBER_OF_METRICS)
      {
         pgexporter_log_error("Maximum metrics per extension exceeded for %s", yaml_config->extension_name);
         return 1;
      }

      struct prometheus* prom = &ext->metrics[ext->number_of_metrics];

      if (yaml_config->metrics[i].tag)
      {
         snprintf(prom->tag, MISC_LENGTH, "%s_%s",
                  yaml_config->extension_name, yaml_config->metrics[i].tag);
      }

      if (yaml_config->metrics[i].collector)
      {
         memcpy(prom->collector, yaml_config->metrics[i].collector,
                MIN(MAX_COLLECTOR_LENGTH - 1, strlen(yaml_config->metrics[i].collector)));
      }
      else
      {
         memcpy(prom->collector, yaml_config->extension_name,
                MIN(MAX_COLLECTOR_LENGTH - 1, strlen(yaml_config->extension_name)));
      }

      if (!yaml_config->metrics[i].sort || !strcmp(yaml_config->metrics[i].sort, "name"))
      {
         prom->sort_type = SORT_NAME;
      }
      else if (!strcmp(yaml_config->metrics[i].sort, "data"))
      {
         prom->sort_type = SORT_DATA0;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected sort_type %s", yaml_config->metrics[i].sort);
         return 1;
      }

      if (!yaml_config->metrics[i].server || !strcmp(yaml_config->metrics[i].server, "both"))
      {
         prom->server_query_type = SERVER_QUERY_BOTH;
      }
      else if (!strcmp(yaml_config->metrics[i].server, "primary"))
      {
         prom->server_query_type = SERVER_QUERY_PRIMARY;
      }
      else if (!strcmp(yaml_config->metrics[i].server, "replica"))
      {
         prom->server_query_type = SERVER_QUERY_REPLICA;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected server %s", yaml_config->metrics[i].server);
         return 1;
      }

      for (int j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {
         struct ext_query_alts* new_query = NULL;
         void* new_query_shmem = NULL;

         pgexporter_create_shared_memory(sizeof(struct ext_query_alts), HUGEPAGE_OFF, &new_query_shmem);
         new_query = (struct ext_query_alts*) new_query_shmem;

         new_query->node.n_columns = MIN(yaml_config->metrics[i].queries[j].n_columns, MAX_NUMBER_OF_COLUMNS);

         memcpy(new_query->node.query, yaml_config->metrics[i].queries[j].query,
                MIN(MAX_QUERY_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].query)));

         if (pgexporter_parse_extension_version(yaml_config->metrics[i].queries[j].version_str, &new_query->ext_version))
         {
            pgexporter_log_error("Failed to parse extension version '%s'",
                                 yaml_config->metrics[i].queries[j].version_str);
            return 1;
         }

         for (int k = 0; k < new_query->node.n_columns; k++)
         {
            if (yaml_config->metrics[i].queries[j].columns[k].name)
            {
               memcpy(new_query->node.columns[k].name, yaml_config->metrics[i].queries[j].columns[k].name,
                      MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].columns[k].name)));
            }

            if (yaml_config->metrics[i].queries[j].columns[k].description)
            {
               memcpy(new_query->node.columns[k].description, yaml_config->metrics[i].queries[j].columns[k].description,
                      MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].columns[k].description)));
            }

            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))
            {
               new_query->node.columns[k].type = LABEL_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "counter"))
            {
               new_query->node.columns[k].type = COUNTER_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "gauge"))
            {
               new_query->node.columns[k].type = GAUGE_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "histogram"))
            {
               new_query->node.columns[k].type = HISTOGRAM_TYPE;
               new_query->node.is_histogram = true;
            }
            else
            {
               pgexporter_log_error("pgexporter: unexpected type %s", yaml_config->metrics[i].queries[j].columns[k].type);
               return 1;
            }
         }

         prom->ext_root = pgexporter_insert_extension_node_avl(prom->ext_root, &new_query);
      }

      for (int j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {
         for (int k = 0; k < yaml_config->metrics[i].queries[j].n_columns; k++)
         {
            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))

            {
               continue;
            }

            char final_metric_name[MISC_LENGTH];
            snprintf(final_metric_name, sizeof(final_metric_name), "%s_%s",
                     yaml_config->extension_name, yaml_config->metrics[i].tag);

            if (yaml_config->metrics[i].queries[j].columns[k].name &&
                strlen(yaml_config->metrics[i].queries[j].columns[k].name) > 0)
            {
               snprintf(final_metric_name + strlen(final_metric_name),
                        sizeof(final_metric_name) - strlen(final_metric_name),
                        "_%s", yaml_config->metrics[i].queries[j].columns[k].name);
            }

            if (config->number_of_metric_names < NUMBER_OF_METRIC_NAMES)
            {
               strncpy(config->metric_names[config->number_of_metric_names], 
                     final_metric_name, 
                     MISC_LENGTH - 1);
               config->metric_names[config->number_of_metric_names][MISC_LENGTH - 1] = '\0';
               config->number_of_metric_names++;
            }
            else
            {
               pgexporter_log_warn("Maximum metric names reached, skipping: %s", final_metric_name);
            }
         }
      }
      ext->number_of_metrics++;
   }

   return 0;
}
