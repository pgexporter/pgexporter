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
#include <logging.h>
#include <prometheus_client.h>
#include <utils.h>
#include <value.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int prometheus_metric_find_create(struct prometheus_bridge* bridge, char* name, struct prometheus_metric** metric);
static int prometheus_metric_set_help(struct prometheus_metric* metric, char* help);
static int prometheus_metric_set_type(struct prometheus_metric* metric, char* type);

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
pgexporter_prometheus_client_get(char* url, struct deque** metrics)
{
   struct http* http = NULL;
   char* response = NULL;

   *metrics = NULL;

   if (pgexporter_http_create(url, &http))
   {
      pgexporter_log_error("Failed to create HTTP interaction");
      goto error;
   }

   if (pgexporter_http_get(http))
   {
      pgexporter_log_error("Failed to execute HTTP/GET interaction");
      goto error;
   }

   pgexporter_http_log(http);

   response = pgexporter_append(response, http->body);

   pgexporter_http_destroy(http);

   return 0;

error:

   pgexporter_http_destroy(http);

   return 1;
}

static int
prometheus_metric_find_create(struct prometheus_bridge* bridge, char* name, struct prometheus_metric** metric)
{
   struct prometheus_metric* m = NULL;

   *metric = NULL;

   m = (struct prometheus_metric*)pgexporter_art_search(bridge->metrics, (unsigned char*)name, strlen(name));

   if (m == NULL)
   {
      struct deque* defs = NULL;

      m = (struct prometheus_metric*)malloc(sizeof(struct prometheus_metric));
      memset(m, 0, sizeof(struct prometheus_metric));

      if (pgexporter_deque_create(false, &defs))
      {
         goto error;
      }

      m->name = strdup(name);
      m->definitions = defs;

      if (pgexporter_art_insert(bridge->metrics, (unsigned char*)name, strlen(name),
                                (uintptr_t)m, ValueRef))
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
prometheus_metric_set_help(struct prometheus_metric* metric, char* help)
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
prometheus_metric_set_type(struct prometheus_metric* metric, char* type)
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
