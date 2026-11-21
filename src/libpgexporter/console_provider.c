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
#include <console_provider.h>
#include <logging.h>

/* system */
#include <stdlib.h>
#include <string.h>

struct console_category*
pgexporter_console_find_or_create_category(struct console_page* console, const char* name)
{
   struct console_category* new_categories = NULL;
   struct console_category* new_cat = NULL;

   if (console == NULL || name == NULL)
   {
      return NULL;
   }

   for (int i = 0; i < console->category_count; i++)
   {
      if (console->categories[i].name != NULL && strcmp(console->categories[i].name, name) == 0)
      {
         return &console->categories[i];
      }
   }

   new_categories = (struct console_category*)realloc(console->categories,
                                                      (console->category_count + 1) * sizeof(struct console_category));
   if (new_categories == NULL)
   {
      pgexporter_log_error("Failed to allocate console category");
      return NULL;
   }

   console->categories = new_categories;
   new_cat = &console->categories[console->category_count];
   memset(new_cat, 0, sizeof(struct console_category));
   new_cat->name = strdup(name);
   if (new_cat->name == NULL)
   {
      pgexporter_log_error("Failed to allocate console category name");
      return NULL;
   }

   console->category_count++;
   return new_cat;
}

int
pgexporter_console_category_add_metric(struct console_category* category, struct console_metric* metric)
{
   struct console_metric* new_metrics = NULL;

   if (category == NULL || metric == NULL)
   {
      return 1;
   }

   new_metrics = (struct console_metric*)realloc(category->metrics,
                                                 (category->metric_count + 1) * sizeof(struct console_metric));
   if (new_metrics == NULL)
   {
      pgexporter_log_error("Failed to allocate console metric array");
      return 1;
   }

   category->metrics = new_metrics;
   memcpy(&category->metrics[category->metric_count], metric, sizeof(struct console_metric));
   category->metric_count++;

   return 0;
}

void
pgexporter_console_metric_free(struct console_metric* metric)
{
   if (metric == NULL)
   {
      return;
   }

   free(metric->name);
   free(metric->type);
   free(metric->help);
   free(metric->server);

   if (metric->labels != NULL)
   {
      for (int i = 0; i < metric->label_count; i++)
      {
         free(metric->labels[i].key);
         free(metric->labels[i].value);
      }
      free(metric->labels);
   }

   memset(metric, 0, sizeof(struct console_metric));
}
