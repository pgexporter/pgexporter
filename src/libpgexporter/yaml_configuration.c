/*
 * Copyright (C) 2022 Red Hat
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
#include <logging.h>
#include <utils.h>

/* system */
#include <yaml.h>
#include <errno.h>

static int pgexporter_read_yaml(struct prometheus* prometheus, char* filename, int* number_of_metrics);

static int handle_key_value(struct prometheus* metrics, char* key, char* value);
static int handle_column_key_value(struct prometheus* metrics, int idx_columns, char* key, char* value);

static int get_yaml_files(char* base, int* number_of_yaml_files, char*** files);
static bool is_yaml_file(char* filename);

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

   if (pgexporter_is_file(config->metrics_path))
   {
      number_of_metrics = 0;
      if (pgexporter_read_yaml(config->prometheus, config->metrics_path, &number_of_metrics))
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
         yaml_path = pgexporter_append(yaml_path, config->metrics_path);
         yaml_path = pgexporter_append(yaml_path, "/");
         yaml_path = pgexporter_append(yaml_path, yaml_files[i]);
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
pgexporter_read_yaml(struct prometheus* prometheus, char* filename, int* number_of_metrics)
{
   FILE* file;
   yaml_parser_t parser;
   yaml_token_t token;
   char* key = NULL;
   char* value = NULL;
   int status = START_STATUS;
   int idx_metrics = 0;
   int idx_columns = 0;
   int in_columns = 0;

   file = fopen(filename, "r");
   if (file == NULL)
   {
      pgexporter_log_error("pgexporter: fopen error %s", strerror(errno));
      return 1;
   }
   if (!yaml_parser_initialize(&parser))
   {
      pgexporter_log_error("pgexporter: yaml_parser_initialize error");
      return 1;
   }

   yaml_parser_set_input_file(&parser, file);
   do
   {
      yaml_parser_scan(&parser, &token);
      switch (token.type)
      {
         case YAML_STREAM_START_TOKEN:
            break;
         case YAML_STREAM_END_TOKEN:
            break;
         case YAML_BLOCK_SEQUENCE_START_TOKEN:
            if (in_columns)
            {
               status = COLUMN_SEQUENCE_STATUS;
            }
            else
            {
               status = SEQUENCE_STAUS;
            }
            break;
         case YAML_BLOCK_ENTRY_TOKEN:
            if (status == SEQUENCE_STAUS)
            {
               status = BLOCK_STAUS;
            }
            else if (status == COLUMN_SEQUENCE_STATUS)
            {
               status = COLUMN_BLOCK_STATUS;
            }
            else
            {
               pgexporter_log_error("pgexporter: unexpected status for YAML_BLOCK_ENTRY_TOKEN");
               goto error;
            }
            break;
         case YAML_BLOCK_MAPPING_START_TOKEN:
            if (status == BLOCK_STAUS)
            {
               status = BLOCK_MAPPING_STATUS;
            }
            else if (status == COLUMN_BLOCK_STATUS)
            {
               status = COLUMN_MAPPING_STATUS;
            }
            else
            {
               goto error;
            }
            break;
         case YAML_KEY_TOKEN:
            free(key);
            key = NULL;
            free(value);
            value = NULL;
            status = KEY_STATUS;
            break;
         case YAML_VALUE_TOKEN:
            if (status != COLUMN_SEQUENCE_STATUS)
            {
               status = VALUE_STATUS;
            }
            break;
         case YAML_SCALAR_TOKEN:
            if (status == KEY_STATUS)
            {
               free(key);
               key = NULL;
               key = strdup((char*)token.data.scalar.value);
               if (!strcmp(key, "columns"))
               {
                  idx_columns = 0;
                  in_columns = 1;
               }
            }
            else if (status == VALUE_STATUS)
            {
               free(value);
               value = NULL;
               value = strdup((char*)token.data.scalar.value);
               if (in_columns)
               {
                  if (handle_column_key_value(&prometheus[idx_metrics], idx_columns, key, value))
                  {
                     goto error;
                  }
               }
               else
               {
                  if (handle_key_value(&prometheus[idx_metrics], key, value))
                  {
                     goto error;
                  }
               }
            }
            else
            {
               /* unexpected status */
               pgexporter_log_error("pgexporter: unexpected status for YAML_SCALAR_TOKEN");
               goto error;
            }
            break;
         case YAML_BLOCK_END_TOKEN:
            if (status == VALUE_STATUS)
            {
               status = COLUMN_SEQUENCE_STATUS;
               idx_columns++;
            }
            else if (status == COLUMN_SEQUENCE_STATUS)
            {
               status = BLOCK_MAPPING_STATUS;
               prometheus[idx_metrics].number_of_columns = idx_columns;
               in_columns = 0;
               idx_metrics++;
            }
            else if (status == BLOCK_MAPPING_STATUS)
            {
               status = SEQUENCE_STAUS;
            }
            else if (status == SEQUENCE_STAUS)
            {
               status = END_STATUS;
            }
            else
            {
               pgexporter_log_error("pgexporter: unexpected status for YAML_BLOCK_END_TOKEN");
               goto error;
            }
            break;
         default:
            /* unexpected token type */
            pgexporter_log_error("pgexporter: unexpected token type");
            goto error;
      }
      if (token.type != YAML_STREAM_END_TOKEN)
      {
         yaml_token_delete(&token);
      }
   }
   while (token.type != YAML_STREAM_END_TOKEN);

   *number_of_metrics = idx_metrics;
   free(key);
   free(value);
   key = NULL;
   value = NULL;
   yaml_token_delete(&token);
   yaml_parser_delete(&parser);
   fclose(file);
   return 0;

error:
   free(key);
   free(value);
   key = NULL;
   value = NULL;
   yaml_token_delete(&token);
   yaml_parser_delete(&parser);
   fclose(file);
   return 1;
}

static int
handle_key_value(struct prometheus* metric, char* key, char* value)
{
   size_t slen = 0;

   if (key == NULL || value == NULL)
   {
      return 1;
   }
   if (!strcmp(key, "query"))
   {
      memset(metric->query, 0, MAX_QUERY_LENGTH);
      slen = strlen(value);
      if (slen > MAX_QUERY_LENGTH - 1)
      {
         slen = MAX_QUERY_LENGTH - 1;
      }
      memcpy(metric->query, value, slen);
   }
   else if (!strcmp(key, "tag"))
   {
      memset(metric->tag, 0, MISC_LENGTH);
      slen = strlen(value);
      if (slen > MISC_LENGTH - 1)
      {
         slen = MISC_LENGTH - 1;
      }
      memcpy(metric->tag, value, slen);
   }
   else if (!strcmp(key, "sort"))
   {
      if (!strcmp(value, "name"))
      {
         metric->sort_type = SORT_NAME;
      }
      else if (!strcmp(value, "data"))
      {
         metric->sort_type = SORT_DATA0;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected sort_type %s", value);
         return 1;
      }
   }
   else if (!strcmp(key, "server"))
   {
      if (!strcmp(value, "both"))
      {
         metric->server_query_type = SERVER_QUERY_BOTH;
      }
      else if (!strcmp(value, "primary"))
      {
         metric->server_query_type = SERVER_QUERY_PRIMARY;
      }
      else if (!strcmp(value, "replica"))
      {
         metric->server_query_type = SERVER_QUERY_REPLICA;
      }
      else
      {
         pgexporter_log_error("pgexporter: unexpected server %s", value);
         return 1;
      }
   }
   else
   {
      //unexpected key
      pgexporter_log_error("pgexporter: unexpected key %s", key);
      return 1;
   }
   return 0;
}

static int
handle_column_key_value(struct prometheus* metric, int idx_columns, char* key, char* value)
{
   size_t slen = 0;
   if (key == NULL || value == NULL)
   {
      return 1;
   }
   if (!strcmp(key, "type"))
   {
      if (!strcmp(value, "label"))
      {
         metric->columns[idx_columns].type = LABEL_TYPE;
      }
      else if (!strcmp(value, "gauge"))
      {
         metric->columns[idx_columns].type = GAUGE_TYPE;
      }
      else if (!strcmp(value, "counter"))
      {
         metric->columns[idx_columns].type = COUNTER_TYPE;
      }
      else if (!strcmp(value, "histogram"))
      {
         metric->columns[idx_columns].type = HISTOGRAM_TYPE;
      }
      else
      {
         //unexpected type
         pgexporter_log_error("pgexporter: unexpected type %s", value);
         return 1;
      }
   }
   else if (!strcmp(key, "name"))
   {
      slen = strlen(value);
      if (slen > MISC_LENGTH - 1)
      {
         slen = MISC_LENGTH - 1;
      }
      memset(metric->columns[idx_columns].name, 0, MISC_LENGTH);
      memcpy(metric->columns[idx_columns].name, value, slen);
   }
   else if (!strcmp(key, "description"))
   {
      memset(metric->columns[idx_columns].description, 0, MISC_LENGTH);
      slen = strlen(value);
      if (slen > MISC_LENGTH - 1)
      {
         slen = MISC_LENGTH - 1;
      }
      memcpy(metric->columns[idx_columns].description, value, slen);
   }
   else
   {
      //unexpected key
      pgexporter_log_error("pgexporter: unexpected column key %s", key);
      return 1;
   }
   return 0;
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
      if (pgexporter_ends_with(all_files[i], ".yaml"))
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
   if (pgexporter_ends_with(file, ".yaml"))
   {
      return true;
   }
   return false;
}
