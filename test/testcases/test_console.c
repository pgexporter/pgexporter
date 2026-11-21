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

#include <pgexporter.h>
#include <console_provider.h>
#include <mctf.h>

#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_console_realtime_provider_lifecycle)
{
   const struct console_data_provider* provider = NULL;
   void* ctx = NULL;

   provider = pgexporter_console_realtime_provider();
   MCTF_ASSERT_PTR_NONNULL(provider, cleanup, "realtime provider should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(provider->name, cleanup, "provider name should not be NULL");
   MCTF_ASSERT_STR_EQ(provider->name, "realtime", cleanup, "provider name should be 'realtime'");
   MCTF_ASSERT_PTR_NONNULL(provider->init, cleanup, "provider init should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(provider->populate_page, cleanup, "provider populate_page should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(provider->destroy, cleanup, "provider destroy should not be NULL");

   MCTF_ASSERT_INT_EQ(provider->init(&ctx), 0, cleanup, "provider init should succeed");
   MCTF_ASSERT_PTR_NONNULL(ctx, cleanup, "provider context should be allocated");

cleanup:
   if (provider != NULL && provider->destroy != NULL)
   {
      provider->destroy(ctx);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_console_find_or_create_category)
{
   struct console_page page;
   struct console_category* cat1 = NULL;
   struct console_category* cat2 = NULL;
   struct console_category* cat1_again = NULL;

   memset(&page, 0, sizeof(struct console_page));

   MCTF_ASSERT_PTR_NULL(pgexporter_console_find_or_create_category(NULL, "database"), cleanup,
                        "NULL page should return NULL");
   MCTF_ASSERT_PTR_NULL(pgexporter_console_find_or_create_category(&page, NULL), cleanup,
                        "NULL name should return NULL");

   cat1 = pgexporter_console_find_or_create_category(&page, "database");
   MCTF_ASSERT_PTR_NONNULL(cat1, cleanup, "first category creation should succeed");
   MCTF_ASSERT_INT_EQ(page.category_count, 1, cleanup, "category_count should be 1");
   MCTF_ASSERT_STR_EQ(cat1->name, "database", cleanup, "category name should match");

   cat2 = pgexporter_console_find_or_create_category(&page, "replication");
   MCTF_ASSERT_PTR_NONNULL(cat2, cleanup, "second category creation should succeed");
   MCTF_ASSERT_INT_EQ(page.category_count, 2, cleanup, "category_count should be 2");

   cat1_again = pgexporter_console_find_or_create_category(&page, "database");
   MCTF_ASSERT_PTR_NONNULL(cat1_again, cleanup, "existing category lookup should succeed");
   MCTF_ASSERT_INT_EQ(page.category_count, 2, cleanup, "category_count should remain 2");
   MCTF_ASSERT_STR_EQ(cat1_again->name, "database", cleanup, "looked-up category name should match");

cleanup:
   if (page.categories != NULL)
   {
      for (int i = 0; i < page.category_count; i++)
      {
         free(page.categories[i].name);
         free(page.categories[i].metrics);
      }
      free(page.categories);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_console_category_add_and_free_metric)
{
   struct console_page page;
   struct console_category* cat = NULL;
   struct console_metric metric;

   memset(&page, 0, sizeof(struct console_page));
   memset(&metric, 0, sizeof(struct console_metric));

   cat = pgexporter_console_find_or_create_category(&page, "pg_stat_database");
   MCTF_ASSERT_PTR_NONNULL(cat, cleanup, "category creation should succeed");

   metric.name = strdup("xact_commit");
   metric.type = strdup("counter");
   metric.help = strdup("Number of transactions committed");
   metric.value = 1234.0;
   metric.server = strdup("primary");
   metric.label_count = 1;
   metric.labels = (struct console_label*)calloc(1, sizeof(struct console_label));
   metric.labels[0].key = strdup("database");
   metric.labels[0].value = strdup("postgres");

   MCTF_ASSERT_INT_EQ(pgexporter_console_category_add_metric(cat, &metric), 0, cleanup,
                      "adding metric to category should succeed");
   MCTF_ASSERT_INT_EQ(cat->metric_count, 1, cleanup, "metric_count should be 1");
   MCTF_ASSERT_STR_EQ(cat->metrics[0].name, "xact_commit", cleanup, "metric name should match");
   MCTF_ASSERT_STR_EQ(cat->metrics[0].server, "primary", cleanup, "metric server should match");
   MCTF_ASSERT_INT_EQ(cat->metrics[0].label_count, 1, cleanup, "label_count should be 1");
   MCTF_ASSERT_STR_EQ(cat->metrics[0].labels[0].key, "database", cleanup, "label key should match");
   MCTF_ASSERT_STR_EQ(cat->metrics[0].labels[0].value, "postgres", cleanup, "label value should match");

cleanup:
   if (page.categories != NULL)
   {
      for (int i = 0; i < page.category_count; i++)
      {
         free(page.categories[i].name);
         for (int j = 0; j < page.categories[i].metric_count; j++)
         {
            pgexporter_console_metric_free(&page.categories[i].metrics[j]);
         }
         free(page.categories[i].metrics);
      }
      free(page.categories);
   }
   pgexporter_console_metric_free(NULL);
   MCTF_FINISH();
}
