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

#include <pgexporter.h>
#include <art.h>
#include <deque.h>
#include <http.h>
#include <json.h>
#include <logging.h>
#include <prometheus_client.h>
#include <utils.h>
#include <value.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int parse_body_to_bridge(int endpoint, time_t timestamp, char* body, struct prometheus_bridge* bridge);
static int metric_find_create(struct prometheus_bridge* bridge, char* name, struct prometheus_metric** metric);
static int metric_set_name(struct prometheus_metric* metric, char* name);
static int metric_set_help(struct prometheus_metric* metric, char* help);
static int metric_set_type(struct prometheus_metric* metric, char* type);
static bool attributes_contains(struct deque* attributes, struct prometheus_attribute* attribute);
static int attributes_find_create(struct deque* definitions, struct deque* input, struct prometheus_attributes** attributes, bool* new);
static int add_attribute(struct deque* attributes, char* key, char* value);
static int add_value(struct deque* values, time_t timestamp, char* value);
static int add_line(struct prometheus_metric* metric, char* line, int endpoint, time_t timestamp);

static void  prometheus_metric_destroy_cb(uintptr_t data);
static char* deque_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void  prometheus_attributes_destroy_cb(uintptr_t data);
static char* prometheus_attributes_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void  prometheus_value_destroy_cb(uintptr_t data);
static char* prometheus_value_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void  prometheus_attribute_destroy_cb(uintptr_t data);
static char* prometheus_attribute_string_cb(uintptr_t data, int32_t format, char* tag, int indent);

int
pgexporter_prometheus_client_create_bridge(struct prometheus_bridge** bridge)
{
   struct prometheus_bridge* b = NULL;

   *bridge = NULL;

   b = (struct prometheus_bridge*)malloc(sizeof(struct prometheus_bridge));

   if (b == NULL)
   {
      pgexporter_log_error("Failed to allocate bridge");
      goto error;
   }

   memset(b, 0, sizeof(struct prometheus_bridge));

   if (pgexporter_art_create(&b->metrics))
   {
      pgexporter_log_error("Failed to create ART");
      goto error;
   }

   *bridge = b;

   return 0;

error:

   return 1;
}

int
pgexporter_prometheus_client_destroy_bridge(struct prometheus_bridge* bridge)
{
   if (bridge != NULL)
   {
      pgexporter_art_destroy(bridge->metrics);
   }

   free(bridge);

   return 0;
}

int
pgexporter_prometheus_client_get(int endpoint, struct prometheus_bridge* bridge)
{
   time_t timestamp;
   struct http* http = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   pgexporter_log_debug("Endpoint http://%s:%d/metrics", config->endpoints[endpoint].host, config->endpoints[endpoint].port);

   if (pgexporter_http_connect(config->endpoints[endpoint].host, config->endpoints[endpoint].port, false, &http))
   {
      pgexporter_log_error("Failed to connect to HTTP endpoint %d (%s:%d)",
                           endpoint,
                           config->endpoints[endpoint].host,
                           config->endpoints[endpoint].port);
      goto error;
   }

   http->endpoint = endpoint;

   if (pgexporter_http_get(http, config->endpoints[endpoint].host, "/metrics"))
   {
      pgexporter_log_error("Failed to execute HTTP/GET interaction with http://%s:%d/metrics",
                           config->endpoints[endpoint].host,
                           config->endpoints[endpoint].port);
      goto error;
   }

   timestamp = time(NULL);

   if (parse_body_to_bridge(endpoint, timestamp, http->body, bridge))
   {
      goto error;
   }

   if (pgexporter_http_disconnect(http))
   {
      pgexporter_log_error("Failed to disconnect HTTP");
      goto error;
   }

   pgexporter_http_destroy(http);

   return 0;

error:

   if (http != NULL)
   {
      pgexporter_http_disconnect(http);
      pgexporter_http_destroy(http);
   }

   return 1;
}

static void
prometheus_metric_destroy_cb(uintptr_t data)
{
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (m != NULL)
   {
      free(m->name);
      free(m->help);
      free(m->type);

      pgexporter_deque_destroy(m->definitions);
   }

   free(m);
}

static char*
deque_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   struct deque* d = NULL;

   d = (struct deque*)data;

   return pgexporter_deque_to_string(d, format, tag, indent);
}

static char*
prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct value_config vc = {.destroy_data = NULL,
                             .to_string = &deque_string_cb};
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (pgexporter_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_art_insert(a, (char*)"Name", (uintptr_t)m->name, ValueString);
      pgexporter_art_insert(a, (char*)"Help", (uintptr_t)m->help, ValueString);
      pgexporter_art_insert(a, (char*)"Type", (uintptr_t)m->type, ValueString);
      pgexporter_art_insert_with_config(a, (char*)"Definitions", (uintptr_t)m->definitions, &vc);

      s = pgexporter_art_to_string(a, format, tag, indent);
   }

   pgexporter_art_destroy(a);

   return s;

error:

   pgexporter_art_destroy(a);

   return "Error";
}

static int
metric_find_create(struct prometheus_bridge* bridge, char* name,
                   struct prometheus_metric** metric)
{
   struct prometheus_metric* m = NULL;
   struct value_config vc = {.destroy_data = &prometheus_metric_destroy_cb,
                             .to_string = &prometheus_metric_string_cb};

   *metric = NULL;

   m = (struct prometheus_metric*)pgexporter_art_search(bridge->metrics, (char*)name);

   if (m == NULL)
   {
      struct deque* defs = NULL;

      m = (struct prometheus_metric*)malloc(sizeof(struct prometheus_metric));
      memset(m, 0, sizeof(struct prometheus_metric));

      if (pgexporter_deque_create(true, &defs))
      {
         goto error;
      }

      m->name = strdup(name);
      m->definitions = defs;

      if (pgexporter_art_insert_with_config(bridge->metrics, (char*)name,
                                            (uintptr_t)m, &vc))
      {
         goto error;
      }
   }

   *metric = m;

   return 0;

error:

   return 1;
}

static int
metric_set_name(struct prometheus_metric* metric, char* name)
{
   if (metric->name != NULL)
   {
      /* Assumes information is already present. */
      return 0;
   }

   metric->name = strdup(name);
   if (metric->name == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static int
metric_set_help(struct prometheus_metric* metric, char* help)
{
   if (metric->help != NULL)
   {
      free(metric->help);
      metric->help = NULL;
   }

   metric->help = strdup(help);

   if (metric->help == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static int
metric_set_type(struct prometheus_metric* metric, char* type)
{
   if (metric->type != NULL)
   {
      free(metric->type);
      metric->type = NULL;
   }

   metric->type = strdup(type);

   if (metric->type == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static bool
attributes_contains(struct deque* attributes, struct prometheus_attribute* attribute)
{
   bool found = false;
   struct deque_iterator* attributes_iterator = NULL;

   if (!pgexporter_deque_empty(attributes))
   {
      if (pgexporter_deque_iterator_create(attributes, &attributes_iterator))
      {
         goto done;
      }

      while (!found && pgexporter_deque_iterator_next(attributes_iterator))
      {
         struct prometheus_attribute* a = (struct prometheus_attribute*)attributes_iterator->value->data;

         if (!strcmp(a->key, attribute->key) && !strcmp(a->value, attribute->value))
         {
            found = true;
         }
      }
   }

done:

   pgexporter_deque_iterator_destroy(attributes_iterator);

   return found;
}

static void
prometheus_attributes_destroy_cb(uintptr_t data)
{
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (m != NULL)
   {
      pgexporter_deque_destroy(m->attributes);
      pgexporter_deque_destroy(m->values);
   }

   free(m);
}

static char*
prometheus_attributes_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct value_config vc = {.destroy_data = NULL,
                             .to_string = &deque_string_cb};
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (pgexporter_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_art_insert_with_config(a, (char*)"Attributes", (uintptr_t)m->attributes, &vc);
      pgexporter_art_insert_with_config(a, (char*)"Values", (uintptr_t)m->values, &vc);

      s = pgexporter_art_to_string(a, format, tag, indent);
   }

   pgexporter_art_destroy(a);

   return s;

error:

   pgexporter_art_destroy(a);

   return "Error";
}

static int
attributes_find_create(struct deque* definitions, struct deque* input,
                       struct prometheus_attributes** attributes, bool* new)
{
   bool found = false;
   struct prometheus_attributes* m = NULL;
   struct deque_iterator* definition_iterator = NULL;
   struct deque_iterator* input_iterator = NULL;
   struct value_config vc = {.destroy_data = &prometheus_attributes_destroy_cb,
                             .to_string = &prometheus_attributes_string_cb};

   *attributes = NULL;
   *new = false;

   /* We have to search for an existing definition */
   if (!pgexporter_deque_empty(definitions))
   {
      if (pgexporter_deque_iterator_create(definitions, &definition_iterator))
      {
         goto error;
      }

      while (!found && pgexporter_deque_iterator_next(definition_iterator))
      {
         bool match = true;
         struct prometheus_attributes* a =
            (struct prometheus_attributes*)definition_iterator->value->data;

         if (pgexporter_deque_iterator_create(input, &input_iterator))
         {
            goto error;
         }

         while (match && pgexporter_deque_iterator_next(input_iterator))
         {
            struct prometheus_attribute* i = (struct prometheus_attribute*)input_iterator->value->data;

            if (!attributes_contains(a->attributes, i))
            {
               match = false;
            }
         }

         if (match)
         {
            *attributes = a;
            found = true;
         }

         pgexporter_deque_iterator_destroy(input_iterator);
         input_iterator = NULL;
      }
   }

   /* Ok, create a new one */
   if (!found)
   {
      m = (struct prometheus_attributes*)malloc(sizeof (struct prometheus_attributes));
      if (m == NULL)
      {
         goto error;
      }

      if (pgexporter_deque_create(false, &m->values))
      {
         goto error;
      }

      m->attributes = input;

      if (pgexporter_deque_add_with_config(definitions, NULL, (uintptr_t)m, &vc))
      {
         goto error;
      }

      *attributes = m;
      *new = true;
   }

   pgexporter_deque_iterator_destroy(definition_iterator);

   return 0;

error:

   return 1;
}

static void
prometheus_value_destroy_cb(uintptr_t data)
{
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (m != NULL)
   {
      free(m->value);
   }

   free(m);
}

static char*
prometheus_value_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (pgexporter_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_art_insert(a, (char*)"Timestamp", (uintptr_t)m->timestamp, ValueInt64);
      pgexporter_art_insert(a, (char*)"Value", (uintptr_t)m->value, ValueString);

      s = pgexporter_art_to_string(a, format, tag, indent);
   }

   pgexporter_art_destroy(a);

   return s;

error:

   pgexporter_art_destroy(a);

   return "Error";
}

static int
add_value(struct deque* values, time_t timestamp, char* value)
{
   struct value_config vc = {.destroy_data = &prometheus_value_destroy_cb,
                             .to_string = &prometheus_value_string_cb};
   struct prometheus_value* val = NULL;

   val = (struct prometheus_value*)malloc(sizeof(struct prometheus_value));
   if (val == NULL)
   {
      goto error;
   }

   val->timestamp = timestamp;
   val->value = strdup(value);

   if (val->value == NULL)
   {
      goto error;
   }

   if (pgexporter_deque_size(values) >= 100)
   {
      struct prometheus_value* v = NULL;

      v = (struct prometheus_value*)pgexporter_deque_poll(values, NULL);

      free(v->value);
      free(v);
   }

   pgexporter_deque_add_with_config(values, NULL, (uintptr_t)val, &vc);

   return 0;

error:

   if (val != NULL)
   {
      free(val->value);
   }
   free(val);

   return 1;
}

static void
prometheus_attribute_destroy_cb(uintptr_t data)
{
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (m != NULL)
   {
      free(m->key);
      free(m->value);
   }

   free(m);
}

static char*
prometheus_attribute_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (pgexporter_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_art_insert(a, (char*)"Key", (uintptr_t)m->key, ValueString);
      pgexporter_art_insert(a, (char*)"Value", (uintptr_t)m->value, ValueString);

      s = pgexporter_art_to_string(a, format, tag, indent);
   }

   pgexporter_art_destroy(a);

   return s;

error:

   pgexporter_art_destroy(a);

   return "Error";
}

static int
add_attribute(struct deque* attributes, char* key, char* value)
{
   struct prometheus_attribute* attr = NULL;
   struct value_config vc = {.destroy_data = &prometheus_attribute_destroy_cb,
                             .to_string = &prometheus_attribute_string_cb};

   attr = (struct prometheus_attribute*)malloc(sizeof(struct prometheus_attribute));
   if (attr == NULL)
   {
      goto error;
   }
   memset(attr, 0, sizeof(struct prometheus_attribute));

   attr->key = strdup(key);
   if (attr->key == NULL)
   {
      goto error;
   }

   attr->value = strdup(value);
   if (attr->value == NULL)
   {
      goto error;
   }

   pgexporter_deque_add_with_config(attributes, NULL, (uintptr_t) attr, &vc);

   return 0;

error:

   if (attr != NULL)
   {
      free(attr->key);
      free(attr->value);
   }
   free(attr);

   return 1;
}

static int
add_line(struct prometheus_metric* metric, char* line, int endpoint, time_t timestamp)
{
   char* e = NULL;
   char key[PROMETHEUS_LENGTH] = {0};
   char value[PROMETHEUS_LENGTH] = {0};
   char* token = NULL;
   char* saveptr = NULL;
   char* line_cpy = NULL;
   bool new = false;
   char* line_value = NULL;
   struct deque* line_attrs = NULL;
   struct prometheus_attributes* attributes = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   if (line == NULL)
   {
      goto error;
   }

   line_cpy = strdup(line);  /* strtok modifies the string. */
   if (line_cpy == NULL)
   {
      goto error;
   }

   if (pgexporter_deque_create(false, &line_attrs))
   {
      goto error;
   }

   e = pgexporter_append(e, config->endpoints[endpoint].host);
   e = pgexporter_append_char(e, ':');
   e = pgexporter_append_int(e, config->endpoints[endpoint].port);

   if (add_attribute(line_attrs, "endpoint", e))
   {
      goto error;
   }

   /* Lines of the form:
    *
    * type{key1="value1",key2="value2",...} value
    *
    * Tokenizing on a " " will give the value on second call.
    * The first token can be further tokenized on "{,}".
    */

   token = strtok_r(line_cpy, "{,} ", &saveptr);

   while (token != NULL)
   {
      if (token == line_cpy)
      {
         /* First token is the name of the metric. So just a sanity check. */
         if (strncmp(token, metric->name, strlen(metric->name)))
         {
            goto error;
         }
      }
      else if (saveptr == NULL || (saveptr != NULL && *saveptr == '\0'))
      {
         /* Final token. */
         line_value = strdup(token);
      }
      else if (strlen(token) > 0)
      {
         /* Assuming of the form key="value" */
         sscanf(token, "%127[^=]", key);
         sscanf(token + strlen(key) + 2, "%127[^\"]", value);

         if (strlen(key) == 0 || strlen(value) == 0)
         {
            goto error;
         }

         if (add_attribute(line_attrs, key, value))
         {
            goto error;
         }
      }

      token = strtok_r(NULL, "{,} ", &saveptr);
   }

   if (attributes_find_create(metric->definitions, line_attrs, &attributes, &new))
   {
      goto error;
   }

   pgexporter_log_info("Attributes: %p %p", attributes, attributes != NULL ? attributes->attributes : 0);

   if (add_value(attributes->values, timestamp, line_value))
   {
      goto error;
   }

   if (!new)
   {
      pgexporter_deque_destroy(line_attrs);
   }

   free(e);
   free(line_cpy);
   free(line_value);

   return 0;

error:

   pgexporter_deque_destroy(line_attrs);

   free(e);
   free(line_cpy);
   free(line_value);

   return 1;
}

static int
parse_body_to_bridge(int endpoint, time_t timestamp, char* body, struct prometheus_bridge* bridge)
{
   char* line = NULL;
   char* saveptr = NULL;
   char name[MISC_LENGTH] = {0};
   char help[MAX_PATH] = {0};
   char type[MISC_LENGTH] = {0};
   struct value_config vc = {.destroy_data = &prometheus_metric_destroy_cb,
                             .to_string = &prometheus_metric_string_cb};
   struct prometheus_metric* metric = NULL;

   line = strtok_r(body, "\n", &saveptr);  /* We ideally should not care if body is modified. */

   while (line != NULL)
   {
      if ((!strcmp(line, "") || !strcmp(line, "\r")) &&
          metric != NULL && metric->definitions->size > 0)  /* Basically empty strings, empty lines, or empty Windows lines. */
      {
         /* Previous metric is over. */
         pgexporter_art_insert_with_config(bridge->metrics, (char*) metric->name, (uintptr_t) metric, &vc);

         metric = NULL;
         continue;
      }
      else if (line[0] == '#')
      {
         if (!strncmp(&line[1], "HELP", 4))
         {
            sscanf(line + 6, "%127s %1021[^\n]", name, help);

            metric_find_create(bridge, name, &metric);

            metric_set_name(metric, name);
            metric_set_help(metric, help);
         }
         else if (!strncmp(&line[1], "TYPE", 4))
         {
            sscanf(line + 6, "%127s %127[^\n]", name, type);
            metric_set_type(metric, type);
         }
         else
         {
            goto error;
         }
      }
      else
      {
         add_line(metric, line, endpoint, timestamp);
      }

      line = strtok_r(NULL, "\n", &saveptr);
   }

   return 0;

error:
   pgexporter_art_destroy(bridge->metrics);
   bridge->metrics = NULL;

   return 1;
}