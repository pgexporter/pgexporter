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
#include <cache.h>
#include <configuration.h>
#include <memory.h>
#include <shmem.h>
#include <tssuite.h>
#include <utils.h>

#include <check.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void* saved_shmem = NULL;

static void
test_cache_setup(void)
{
   size_t size = sizeof(struct configuration);

   saved_shmem = shmem;

   pgexporter_create_shared_memory(size, HUGEPAGE_OFF, &shmem);
   pgexporter_init_configuration(shmem);

   pgexporter_memory_init();
}

static void
test_cache_teardown(void)
{
   size_t size = sizeof(struct configuration);

   pgexporter_memory_destroy();
   pgexporter_destroy_shared_memory(shmem, size);
   shmem = saved_shmem;
}

// Test cache initialization
START_TEST(test_cache_init)
{
   size_t cache_size = 1024;
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   ck_assert_int_eq(pgexporter_cache_init(cache_size, &total_size, &cache_shmem), 0);
   ck_assert_ptr_nonnull(cache_shmem);
   ck_assert_int_eq(total_size, cache_size + sizeof(struct prometheus_cache));

   cache = (struct prometheus_cache*)cache_shmem;
   ck_assert_int_eq(cache->size, cache_size);
   ck_assert_int_eq(cache->valid_until, 0);
   ck_assert_int_eq(cache->data[0], '\0');

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

// Test cache validity checks
START_TEST(test_cache_is_valid)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   pgexporter_cache_init(64, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL cache
   ck_assert(!pgexporter_cache_is_valid(NULL));

   // Empty cache
   ck_assert(!pgexporter_cache_is_valid(cache));

   // Data appended but not finalized
   pgexporter_cache_append(cache, "data");
   ck_assert(!pgexporter_cache_is_valid(cache));

   // Finalized cache should be valid
   pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(60));
   ck_assert(pgexporter_cache_is_valid(cache));

   // Expired cache
   cache->valid_until = time(NULL) - 10;
   ck_assert(!pgexporter_cache_is_valid(cache));

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

// Test cache invalidation
START_TEST(test_cache_invalidate)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   pgexporter_cache_init(64, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL should not crash
   pgexporter_cache_invalidate(NULL);

   pgexporter_cache_append(cache, "some data");
   pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(60));
   ck_assert(pgexporter_cache_is_valid(cache));

   pgexporter_cache_invalidate(cache);
   ck_assert_int_eq(cache->valid_until, 0);
   ck_assert_int_eq(cache->data[0], '\0');
   ck_assert(!pgexporter_cache_is_valid(cache));

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

// Test cache append operations
START_TEST(test_cache_append)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   pgexporter_cache_init(32, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL guards
   ck_assert(!pgexporter_cache_append(NULL, "data"));
   ck_assert(!pgexporter_cache_append(cache, NULL));

   // Single append
   ck_assert(pgexporter_cache_append(cache, "hello"));
   ck_assert_str_eq(cache->data, "hello");

   // Multiple appends
   ck_assert(pgexporter_cache_append(cache, "world"));
   ck_assert_str_eq(cache->data, "helloworld");
   ck_assert_int_eq(cache->data[10], '\0');

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

// Test cache append overflow
START_TEST(test_cache_append_overflow)
{
   size_t cache_size = 8;
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   pgexporter_cache_init(cache_size, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // Fill exactly
   ck_assert(pgexporter_cache_append(cache, "1234567"));

   // Even 1 more byte should fail
   ck_assert(!pgexporter_cache_append(cache, "X"));

   // Cache should be invalidated after overflow
   ck_assert_int_eq(cache->data[0], '\0');
   ck_assert_int_eq(cache->valid_until, 0);

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

// Test cache finalize
START_TEST(test_cache_finalize)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;
   time_t before;

   pgexporter_cache_init(64, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL should not crash
   ck_assert(!pgexporter_cache_finalize(NULL, PGEXPORTER_TIME_SEC(60)));

   before = time(NULL);
   ck_assert(pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(120)));
   ck_assert(cache->valid_until >= before + 120);

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

// Test full cache lifecycle: init, append, finalize, invalidate, reuse
START_TEST(test_cache_lifecycle)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   ck_assert_int_eq(pgexporter_cache_init(256, &total_size, &cache_shmem), 0);
   cache = (struct prometheus_cache*)cache_shmem;

   ck_assert(!pgexporter_cache_is_valid(cache));

   ck_assert(pgexporter_cache_append(cache, "metric1 42\n"));
   ck_assert(pgexporter_cache_append(cache, "metric2 99\n"));
   ck_assert_str_eq(cache->data, "metric1 42\nmetric2 99\n");
   ck_assert(!pgexporter_cache_is_valid(cache));

   ck_assert(pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(60)));
   ck_assert(pgexporter_cache_is_valid(cache));

   pgexporter_cache_invalidate(cache);
   ck_assert(!pgexporter_cache_is_valid(cache));

   // Reuse after invalidation
   ck_assert(pgexporter_cache_append(cache, "new data"));
   ck_assert(pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(30)));
   ck_assert(pgexporter_cache_is_valid(cache));
   ck_assert_str_eq(cache->data, "new data");

   pgexporter_destroy_shared_memory(cache_shmem, total_size);
}
END_TEST

Suite*
pgexporter_test_cache_suite(void)
{
   Suite* s;
   TCase* tc_cache;

   s = suite_create("pgexporter_test_cache");

   tc_cache = tcase_create("Cache");

   tcase_add_checked_fixture(tc_cache, test_cache_setup, test_cache_teardown);

   tcase_add_test(tc_cache, test_cache_init);
   tcase_add_test(tc_cache, test_cache_is_valid);
   tcase_add_test(tc_cache, test_cache_invalidate);
   tcase_add_test(tc_cache, test_cache_append);
   tcase_add_test(tc_cache, test_cache_append_overflow);
   tcase_add_test(tc_cache, test_cache_finalize);
   tcase_add_test(tc_cache, test_cache_lifecycle);
   suite_add_tcase(s, tc_cache);

   return s;
}
