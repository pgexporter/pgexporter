/*
 * Copyright (C) 2023 Red Hat
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
#include <yaml_configuration.h>

/* system */
#include <yaml.h>
#include <errno.h>

static int pgexporter_read_yaml(prometheus_t* prometheus, char* filename, int* number_of_metrics);

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
} __attribute__ ((aligned (64))) yaml_metric_t;

// Config's Structure
typedef struct yaml_config
{
   yaml_metric_t* metrics;
   int n_metrics;
   char default_version;
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
static int semantics_yaml(prometheus_t* prometheus, yaml_config_t* yaml_config);

int
pgexporter_read_metrics_configuration(void* shmem)
{
   configuration_t* config;
   int idx_metrics = 0;
   int number_of_metrics = 0;
   int number_of_yaml_files = 0;
   char** yaml_files = NULL;
   char* yaml_path = NULL;

   config = (configuration_t*) shmem;
   idx_metrics = config->number_of_metrics;

   if (pgexporter_is_file(config->metrics_path))
   {
      number_of_metrics = 0;
      if (pgexporter_read_yaml(config->prometheus + idx_metrics, config->metrics_path, &number_of_metrics))
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

         if (pgexporter_read_yaml(config->prometheus + idx_metrics, yaml_path, &number_of_metrics))
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
pgexporter_read_internal_yaml_metrics(configuration_t* config, bool start)
{
   int number_of_metrics = 0;
   int ret;
   FILE* internal_yaml_ptr = fmemopen(INTERNAL_YAML, strlen(INTERNAL_YAML), "r");

   ret = pgexporter_read_yaml_from_file_pointer(config->prometheus, &number_of_metrics, internal_yaml_ptr);
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
pgexporter_read_yaml(prometheus_t* prometheus, char* filename, int* number_of_metrics)
{
   FILE* file;

   file = fopen(filename, "r");
   if (file == NULL)
   {
      pgexporter_log_error("pgexporter: fopen error %s", strerror(errno));
      return 1;
   }

   int ret = pgexporter_read_yaml_from_file_pointer(prometheus, number_of_metrics, file);

   fclose(file);

   return ret;
}

int
pgexporter_read_yaml_from_file_pointer(prometheus_t* prometheus, int* number_of_metrics, FILE* file)
{
   int ret = 0;
   yaml_config_t yaml_config;

   memset(&yaml_config, 0, sizeof(yaml_config_t));

   if (parse_yaml(file, &yaml_config))
   {
      ret = 1;
      goto end;
   }

   *number_of_metrics += yaml_config.n_metrics;

   if (semantics_yaml(prometheus, &yaml_config))
   {
      ret = 1;
      goto end;
   }

end:
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

            if (!strcmp(buf, "version"))
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
               if (parse_int(parser_ptr, event_ptr, state_ptr, (int*) &(*queries)[*n_queries].version))
               {
                  goto error;
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
         free_yaml_queries(&(*metrics)[i].queries, (*metrics)[i].n_queries);
      }
   }

   free(*metrics);
   *metrics = NULL;
}

static void
free_yaml_queries(yaml_query_t** queries, size_t n_queries)
{
   for (int i = 0 ; i < n_queries; i++)
   {
      if ((*queries)[i].query)
      {
         free((*queries)[i].query);
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

static int
semantics_yaml(prometheus_t* prometheus, yaml_config_t* yaml_config)
{

   for (int i = 0; i < yaml_config->n_metrics; i++)
   {

      memcpy(prometheus[i].tag, yaml_config->metrics[i].tag, MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].tag)));
      memcpy(prometheus[i].collector, yaml_config->metrics[i].collector, MIN(MAX_COLLECTOR_LENGTH - 1, strlen(yaml_config->metrics[i].collector)));

      // Sort Type
      if (!yaml_config->metrics[i].sort || !strcmp(yaml_config->metrics[i].sort, "name"))
      {
         prometheus[i].sort_type = SORT_NAME;
      }
      else if (!strcmp(yaml_config->metrics[i].sort, "data"))
      {
         prometheus[i].sort_type = SORT_DATA0;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected sort_type %s", yaml_config->metrics[i].sort);
         return 1;
      }

      // Server Query Type
      if (!yaml_config->metrics[i].server || !strcmp(yaml_config->metrics[i].server, "both"))
      {
         prometheus[i].server_query_type = SERVER_QUERY_BOTH;
      }
      else if (!strcmp(yaml_config->metrics[i].server, "primary"))
      {
         prometheus[i].server_query_type = SERVER_QUERY_PRIMARY;
      }
      else if (!strcmp(yaml_config->metrics[i].server, "replica"))
      {
         prometheus[i].server_query_type = SERVER_QUERY_REPLICA;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected server %s", yaml_config->metrics[i].server);
         return 1;
      }

      // Queries
      for (int j = 0; j < yaml_config->metrics[i].n_queries; j++)
      {

         query_alts_t* new_query = NULL;
         void* new_query_shmem = NULL;

         pgexporter_create_shared_memory(sizeof(query_alts_t), HUGEPAGE_OFF, &new_query_shmem);
         new_query = (query_alts_t*) new_query_shmem;

         new_query->n_columns = MIN(yaml_config->metrics[i].queries[j].n_columns, MAX_NUMBER_OF_COLUMNS);

         memcpy(new_query->query, yaml_config->metrics[i].queries[j].query, MIN(MAX_QUERY_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].query)));
         new_query->version = yaml_config->metrics[i].queries[j].version;

         // Columns
         for (int k = 0; k < new_query->n_columns; k++)
         {

            // Name
            if (yaml_config->metrics[i].queries[j].columns[k].name)
            {
               memcpy(new_query->columns[k].name, yaml_config->metrics[i].queries[j].columns[k].name, MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].columns[k].name)));
            }

            // Description
            if (yaml_config->metrics[i].queries[j].columns[k].description)
            {
               memcpy(new_query->columns[k].description, yaml_config->metrics[i].queries[j].columns[k].description, MIN(MISC_LENGTH - 1, strlen(yaml_config->metrics[i].queries[j].columns[k].description)));
            }

            // Type
            if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "label"))
            {
               new_query->columns[k].type = LABEL_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "counter"))
            {
               new_query->columns[k].type = COUNTER_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "gauge"))
            {
               new_query->columns[k].type = GAUGE_TYPE;
            }
            else if (!strcmp(yaml_config->metrics[i].queries[j].columns[k].type, "histogram"))
            {
               new_query->columns[k].type = HISTOGRAM_TYPE;
               new_query->is_histogram = true;
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
            new_query->version = yaml_config->default_version;
         }

         prometheus[i].root = pgexporter_insert_node_avl(prometheus[i].root, &new_query);
      }
   }

   return 0;
}