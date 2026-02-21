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

#include <mctf.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void* cache_test_shmem = NULL;

static void
test_cache_setup(void)
{
   size_t size = sizeof(struct configuration);

   pgexporter_create_shared_memory(size, HUGEPAGE_OFF, &cache_test_shmem);
   pgexporter_init_configuration(cache_test_shmem);

   pgexporter_memory_init();
}

static void
test_cache_teardown(void)
{
   size_t size = sizeof(struct configuration);

   pgexporter_memory_destroy();
   pgexporter_destroy_shared_memory(cache_test_shmem, size);
   cache_test_shmem = NULL;
}

// Test cache initialization
MCTF_TEST(test_cache_init)
{
   size_t cache_size = 1024;
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   test_cache_setup();

   MCTF_ASSERT_INT_EQ(pgexporter_cache_init(cache_size, &total_size, &cache_shmem), 0, cleanup, "cache_init failed");
   MCTF_ASSERT(cache_shmem != NULL, cleanup, "cache_shmem is NULL");
   MCTF_ASSERT_INT_EQ(total_size, cache_size + sizeof(struct prometheus_cache), cleanup, "total_size mismatch");

   cache = (struct prometheus_cache*)cache_shmem;
   MCTF_ASSERT_INT_EQ(cache->size, cache_size, cleanup, "cache size mismatch");
   MCTF_ASSERT_INT_EQ(cache->valid_until, 0, cleanup, "cache valid_until mismatch");
   MCTF_ASSERT_INT_EQ(cache->data[0], '\0', cleanup, "cache data[0] mismatch");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}

// Test cache validity checks
MCTF_TEST(test_cache_is_valid)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   test_cache_setup();

   pgexporter_cache_init(64, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL cache
   MCTF_ASSERT(!pgexporter_cache_is_valid(NULL), cleanup, "NULL cache should be invalid");

   // Empty cache
   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "Empty cache should be invalid");

   // Data appended but not finalized
   pgexporter_cache_append(cache, "data");
   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "Unfinalized cache should be invalid");

   // Finalized cache should be valid
   pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(60));
   MCTF_ASSERT(pgexporter_cache_is_valid(cache), cleanup, "Finalized cache should be valid");

   // Expired cache
   cache->valid_until = time(NULL) - 10;
   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "Expired cache should be invalid");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}

// Test cache invalidation
MCTF_TEST(test_cache_invalidate)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   test_cache_setup();

   pgexporter_cache_init(64, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL should not crash
   pgexporter_cache_invalidate(NULL);

   pgexporter_cache_append(cache, "some data");
   pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(60));
   MCTF_ASSERT(pgexporter_cache_is_valid(cache), cleanup, "cache should be valid");

   pgexporter_cache_invalidate(cache);
   MCTF_ASSERT_INT_EQ(cache->valid_until, 0, cleanup, "valid_until not cleared");
   MCTF_ASSERT_INT_EQ(cache->data[0], '\0', cleanup, "data not cleared");
   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "invalidated cache should be invalid");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}

// Test cache append operations
MCTF_TEST(test_cache_append)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   test_cache_setup();

   pgexporter_cache_init(32, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL guards
   MCTF_ASSERT(!pgexporter_cache_append(NULL, "data"), cleanup, "append to NULL cache should fail");
   MCTF_ASSERT(!pgexporter_cache_append(cache, NULL), cleanup, "append NULL data should fail");

   // Single append
   MCTF_ASSERT(pgexporter_cache_append(cache, "hello"), cleanup, "append failed");
   MCTF_ASSERT_STR_EQ(cache->data, "hello", cleanup, "data mismatch");

   // Multiple appends
   MCTF_ASSERT(pgexporter_cache_append(cache, "world"), cleanup, "second append failed");
   MCTF_ASSERT_STR_EQ(cache->data, "helloworld", cleanup, "data mismatch after second append");
   MCTF_ASSERT_INT_EQ(cache->data[10], '\0', cleanup, "missing null terminator");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}

// Test cache append overflow
MCTF_TEST(test_cache_append_overflow)
{
   size_t cache_size = 8;
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   test_cache_setup();

   pgexporter_cache_init(cache_size, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // Fill exactly
   MCTF_ASSERT(pgexporter_cache_append(cache, "1234567"), cleanup, "append failed");

   // Even 1 more byte should fail
   MCTF_ASSERT(!pgexporter_cache_append(cache, "X"), cleanup, "append should have failed on overflow");

   // Cache should be invalidated after overflow
   MCTF_ASSERT_INT_EQ(cache->data[0], '\0', cleanup, "data should be cleared on overflow");
   MCTF_ASSERT_INT_EQ(cache->valid_until, 0, cleanup, "valid_until should be cleared on overflow");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}

// Test cache finalize
MCTF_TEST(test_cache_finalize)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;
   time_t before;

   test_cache_setup();

   pgexporter_cache_init(64, &total_size, &cache_shmem);
   cache = (struct prometheus_cache*)cache_shmem;

   // NULL should not crash
   MCTF_ASSERT(!pgexporter_cache_finalize(NULL, PGEXPORTER_TIME_SEC(60)), cleanup, "finalize NULL should fail");

   before = time(NULL);
   MCTF_ASSERT(pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(120)), cleanup, "finalize failed");
   MCTF_ASSERT(cache->valid_until >= before + 120, cleanup, "valid_until mismatch");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}

// Test full cache lifecycle: init, append, finalize, invalidate, reuse
MCTF_TEST(test_cache_lifecycle)
{
   size_t total_size = 0;
   void* cache_shmem = NULL;
   struct prometheus_cache* cache = NULL;

   test_cache_setup();

   MCTF_ASSERT_INT_EQ(pgexporter_cache_init(256, &total_size, &cache_shmem), 0, cleanup, "cache_init failed");
   cache = (struct prometheus_cache*)cache_shmem;

   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "cache should be invalid initially");

   MCTF_ASSERT(pgexporter_cache_append(cache, "metric1 42\n"), cleanup, "append 1 failed");
   MCTF_ASSERT(pgexporter_cache_append(cache, "metric2 99\n"), cleanup, "append 2 failed");
   MCTF_ASSERT_STR_EQ(cache->data, "metric1 42\nmetric2 99\n", cleanup, "data mismatch");
   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "unfinalized cache should be invalid");

   MCTF_ASSERT(pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(60)), cleanup, "finalize failed");
   MCTF_ASSERT(pgexporter_cache_is_valid(cache), cleanup, "finalized cache should be valid");

   pgexporter_cache_invalidate(cache);
   MCTF_ASSERT(!pgexporter_cache_is_valid(cache), cleanup, "invalidated cache should be invalid");

   // Reuse after invalidation
   MCTF_ASSERT(pgexporter_cache_append(cache, "new data"), cleanup, "append after invalidation failed");
   MCTF_ASSERT(pgexporter_cache_finalize(cache, PGEXPORTER_TIME_SEC(30)), cleanup, "finalize after invalidation failed");
   MCTF_ASSERT(pgexporter_cache_is_valid(cache), cleanup, "cache should be valid again");
   MCTF_ASSERT_STR_EQ(cache->data, "new data", cleanup, "new data mismatch");

cleanup:
   if (cache_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(cache_shmem, total_size);
   }
   test_cache_teardown();
   MCTF_FINISH();
}
