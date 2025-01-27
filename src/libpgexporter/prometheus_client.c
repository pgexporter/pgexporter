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

#include "json.h"
#include <pgexporter.h>
#include <art.h>
#include <deque.h>
#include <http.h>
#include <logging.h>
#include <prometheus_client.h>
#include <utils.h>
#include <value.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int metric_find_create(struct prometheus_bridge* bridge, char* name, struct prometheus_metric** metric);
static int metric_set_name(struct prometheus_metric* metric, char* name);
static int metric_set_help(struct prometheus_metric* metric, char* help);
static int metric_set_type(struct prometheus_metric* metric, char* type);
static int add_definition(struct prometheus_metric* metric, struct deque** attr, struct deque** val);
static int add_attribute(struct deque** attributes, char* key, char* value);
static int add_value(struct deque** values, char* value);
static int add_line(struct prometheus_metric* metric, char* line, char* endpoint);
static int parse_body_to_bridge(char* endpoint, char* body, struct prometheus_bridge* bridge);
static int parse_metric_line(struct prometheus_metric* metric, struct deque** attrs, struct deque** vals, char* line, char* endpoint);

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
   char* url = NULL;
   struct http* http = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   pgexporter_log_debug("Endpoint http://%s:%d/metrics", config->endpoints[endpoint].host, config->endpoints[endpoint].port);

   if (pgexporter_http_create(endpoint, &http))
   {
      pgexporter_log_error("Failed to create HTTP interaction for endpoint %d", endpoint);
      goto error;
   }

   if (pgexporter_http_get(http))
   {
      pgexporter_log_error("Failed to execute HTTP/GET interaction with http://%s:%d/metrics",
                           config->endpoints[endpoint].host,
                           config->endpoints[endpoint].port);
      goto error;
   }

   pgexporter_log_info("Ready to merge: %d", http->endpoint);

   url = pgexporter_append(url, config->endpoints[endpoint].host);
   url = pgexporter_append_char(url, ':');
   url = pgexporter_append_int(url, config->endpoints[endpoint].port);

   if (parse_body_to_bridge(url, http->body, bridge))
   {
      // TODO: What do we do with the bridge?
      goto error;
   }

   free(url);

   pgexporter_http_destroy(http);

   return 0;

error:

   free(url);

   pgexporter_http_destroy(http);

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
prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = "";
   struct json* j = NULL;
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (pgexporter_json_create(&j))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_json_put(j, "Name", (uintptr_t)m->name, ValueString);
      pgexporter_json_put(j, "Help", (uintptr_t)m->help, ValueString);
      pgexporter_json_put(j, "Type", (uintptr_t)m->type, ValueString);
      pgexporter_json_put(j, "Definitions", (uintptr_t)m->definitions, ValueRef);

      s = pgexporter_json_to_string(j, format, tag, indent);
   }

   pgexporter_json_destroy(j);

   return s;

error:

   pgexporter_json_destroy(j);

   free(s);

   return "Error";
}

static int
metric_find_create(struct prometheus_bridge* bridge, char* name,
                   struct prometheus_metric** metric)
{
   struct prometheus_metric* m = NULL;
   struct value_config* vc = NULL;

   vc = (struct value_config*)malloc(sizeof(struct value_config));
   if (vc == NULL)
   {
      goto error;
   }
   memset(vc, 0, sizeof(struct value_config));
   vc->destroy_data = &prometheus_metric_destroy_cb;
   vc->to_string = &prometheus_metric_string_cb;

   *metric = NULL;

   m = (struct prometheus_metric*)pgexporter_art_search(bridge->metrics, (unsigned char*)name, strlen(name));

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

      if (pgexporter_art_insert_with_config(bridge->metrics, (unsigned char*)name, strlen(name),
                                            (uintptr_t)m, vc))
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

static int
parse_metric_line(struct prometheus_metric* metric, struct deque** attrs,
                  struct deque** vals, char* line, char* endpoint)
{
   /* Metric lines are the ones that actually carry metrics, and not help or type. */

   char key[MISC_LENGTH] = {0};
   char value[MISC_LENGTH] = {0};
   char* token = NULL;
   char* saveptr = NULL;
   char* line_cpy = NULL;

   if (line == NULL)
   {
      goto error;
   }

   line_cpy = strdup(line); /* strtok modifies the string. */
   if (line_cpy == NULL)
   {
      goto error;
   }

   if (add_attribute(attrs, "endpoint", endpoint))
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
      else if (*saveptr == '\0')
      {
         /* Final token. */

         if (add_value(vals, token))
         {
            /* TODO: Clear memory of items in deque. */
            goto error;
         }
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

         if (add_attribute(attrs, key, value))
         {
            /* TODO: Clear memory of items in deque. */
            goto error;
         }
      }

      token = strtok_r(NULL, "{,} ", &saveptr);
   }

   free(line_cpy);
   return 0;

error:
   free(line_cpy);

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
   char* s = "";
   struct json* j = NULL;
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (pgexporter_json_create(&j))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_json_put(j, "Timestamp", (uintptr_t)m->timestamp, ValueInt64);
      pgexporter_json_put(j, "Value", (uintptr_t)m->value, ValueString);

      s = pgexporter_json_to_string(j, format, tag, indent);
   }

   pgexporter_json_destroy(j);

   return s;

error:

   pgexporter_json_destroy(j);

   free(s);

   return "Error";
}

static int
add_value(struct deque** values, char* value)
{
   bool new = false;
   struct deque* vals = NULL;
   struct value_config* vc = NULL;
   struct prometheus_value* val = NULL;

   val = (struct prometheus_value*)malloc(sizeof(struct prometheus_value));
   if (val == NULL)
   {
      goto error;
   }

   vals = *values;

   vc = (struct value_config*)malloc(sizeof(struct value_config));
   if (vc == NULL)
   {
      goto error;
   }

   vc->destroy_data = &prometheus_value_destroy_cb;
   vc->to_string = &prometheus_value_string_cb;

   if (vals == NULL)
   {
      pgexporter_deque_create(true, &vals);

      if (vals == NULL)
      {
         goto error;
      }

      *values = vals;
      new = true;
   }

   val->timestamp = time(NULL); // TODO: Attach current timestamp.
   val->value = strdup(value);

   if (val->value == NULL)
   {
      if (new)
      {
         pgexporter_deque_destroy(vals);
         *values = NULL;
      }
      goto error;
   }

   pgexporter_deque_add_with_config(*values, NULL, (uintptr_t)val, vc);

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
   char* s = "";
   struct json* j = NULL;
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (pgexporter_json_create(&j))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_json_put(j, "Key", (uintptr_t)m->key, ValueString);
      pgexporter_json_put(j, "Value", (uintptr_t)m->value, ValueString);

      s = pgexporter_json_to_string(j, format, tag, indent);
   }

   pgexporter_json_destroy(j);

   return s;

error:

   pgexporter_json_destroy(j);

   free(s);

   return "Error";
}

static int
add_attribute(struct deque** attributes, char* key, char* value)
{
   struct deque* attrs = NULL;
   struct value_config* vc = NULL;
   struct prometheus_attribute* attr = NULL;

   attrs = *attributes;

   attr = (struct prometheus_attribute*)malloc(sizeof(struct prometheus_attribute));
   if (attr == NULL)
   {
      goto error;
   }
   memset(attr, 0, sizeof(struct prometheus_attribute));

   vc = (struct value_config*)malloc(sizeof(struct value_config));
   if (vc == NULL)
   {
      goto error;
   }
   memset(vc, 0, sizeof(struct value_config));

   vc->destroy_data = &prometheus_attribute_destroy_cb;
   vc->to_string = &prometheus_attribute_string_cb;

   if (attrs == NULL)
   {
      pgexporter_deque_create(true, &attrs);

      if (attrs == NULL)
      {
         goto error;
      }
   }

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

   pgexporter_deque_add_with_config(attrs, NULL, (uintptr_t) attr, vc);

   return 0;

error:

   pgexporter_deque_destroy(attrs);

   if (attr != NULL)
   {
      free(attr->key);
      free(attr->value);
   }
   free(attr);

   return 1;
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
   char* s = "";
   struct json* j = NULL;
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (pgexporter_json_create(&j))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgexporter_json_put(j, "Attributes", (uintptr_t)m->attributes, ValueRef);
      pgexporter_json_put(j, "Value", (uintptr_t)m->values, ValueRef);

      s = pgexporter_json_to_string(j, format, tag, indent);
   }

   pgexporter_json_destroy(j);

   return s;

error:

   pgexporter_json_destroy(j);

   free(s);

   return "Error";
}

static int
add_definition(struct prometheus_metric* metric, struct deque** attr, struct deque** val)
{
   struct deque* attrs = NULL;
   struct deque* vals = NULL;
   struct value_config* vc = NULL;
   struct prometheus_attributes* def = NULL;

   if (attr == NULL || val == NULL || metric == NULL)
   {
      pgexporter_log_error("Something is NULL");
      goto errout;
   }

   attrs = *attr;
   vals = *val;

   def = malloc(sizeof(*def));
   if (def == NULL)
   {
      goto errout;
   }

   vc = (struct value_config*)malloc(sizeof(struct value_config));
   if (vc == NULL)
   {
      goto errout;
   }
   memset(vc, 0, sizeof(struct value_config));

   vc->destroy_data = &prometheus_attributes_destroy_cb;
   vc->to_string = &prometheus_attributes_string_cb;

   if (metric->definitions == NULL)
   {
      pgexporter_deque_create(true, &metric->definitions);
   }

   def->attributes = attrs;
   def->values = vals;

   *attr = NULL;
   *val = NULL;

   if (pgexporter_deque_add_with_config(metric->definitions, NULL, (uintptr_t) def, vc))
   {
      goto errout_with_def;
   }

   return 0;

errout_with_def:
   pgexporter_deque_destroy(def->attributes);
   def->attributes = NULL;

   pgexporter_deque_destroy(def->values);
   def->values = NULL;

   free(def);
   def = NULL;

errout:
   return 1;
}

static int
add_line(struct prometheus_metric* metric, char* line, char* endpoint)
{
   struct deque* attrs = NULL;
   struct deque* vals = NULL;

   // attr and val are allocated here.
   if (parse_metric_line(metric, &attrs, &vals, line, endpoint))
   {
      goto error;
   }

   // attr and val have their ownership transferred.
   if (add_definition(metric, &attrs, &vals))
   {
      goto error;
   }

   return 0;

error:
   pgexporter_deque_destroy(attrs);
   pgexporter_deque_destroy(vals);

   return 1;
}

static int
parse_body_to_bridge(char* endpoint, char* body, struct prometheus_bridge* bridge)
{
   char* line = NULL;
   char* saveptr = NULL;
   char name[MISC_LENGTH] = {0};
   char help[MAX_PATH] = {0};
   char type[MISC_LENGTH] = {0};
   struct value_config* vc = NULL;
   struct prometheus_metric* metric = NULL;

   line = strtok_r(body, "\n", &saveptr); /* We ideally should not care if body is modified. */

   // TODO: What about metric ? Is it a missing metric = bridge->definitions ?

   while (line != NULL)
   {
      if ((!strcmp(line, "") || !strcmp(line, "\r")) &&
          metric != NULL && metric->definitions->size > 0) /* Basically empty strings, empty lines, or empty Windows lines. */
      {
         /* Previous metric is over. */
         vc = (struct value_config*)malloc(sizeof(struct value_config));
         if (vc == NULL)
         {
            goto error;
         }
         memset(vc, 0, sizeof(struct value_config));
         vc->destroy_data = &prometheus_metric_destroy_cb;
         vc->to_string = &prometheus_metric_string_cb;

         pgexporter_art_insert_with_config(bridge->metrics, (unsigned char*) metric->name, strlen(metric->name), (uintptr_t) metric, vc);

         metric = NULL;
         continue;
      }
      else if (line[0] == '#')
      {
         if (!strncmp(&line[1], "HELP", 4))
         {
            sscanf(line + 6, "%127s %1021[^\n]", name, help);

            // TODO: help is as expected here, but JSON prints a string that's not terminated with a "

            metric_find_create(bridge, name, &metric);

            metric_set_name(metric, name);
            metric_set_help(metric, help);
         }
         else if (!strncmp(&line[1], "TYPE", 4))
         {
            sscanf(line + 6, "%127s %127[^\n]", name, type);
            metric_set_type(metric, type);
            // assert(!strcmp(metric->name, name));
         }
         else
         {
            // TODO: Destroy all of `metric` during this as well.
            goto error;
         }
      }
      else
      {
         add_line(metric, line, endpoint);
      }

      line = strtok_r(NULL, "\n", &saveptr);
   }

   return 0;

error:
   pgexporter_art_destroy(bridge->metrics);
   bridge->metrics = NULL;

   return 1;
}
