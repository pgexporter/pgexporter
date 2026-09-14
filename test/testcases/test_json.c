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
#include <json.h>
#include <value.h>
#include <mctf.h>

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Basic item: put / get / contains_key                                */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_item_put_get)
{
   struct json* obj = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_json_create(&obj), 0, cleanup, "json_create failed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object is NULL");

   MCTF_ASSERT_INT_EQ(pgexporter_json_put(obj, "key1", (uintptr_t)"value1", ValueString), 0, cleanup,
                      "put key1 failed");
   MCTF_ASSERT_INT_EQ(pgexporter_json_put(obj, "key2", (uintptr_t)99, ValueInt32), 0, cleanup,
                      "put key2 failed");

   MCTF_ASSERT_STR_EQ((char*)pgexporter_json_get(obj, "key1"), "value1", cleanup, "get key1 mismatch");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(obj, "key2"), 99, cleanup, "get key2 mismatch");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(obj, "missing"), 0, cleanup, "missing key should return 0");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

MCTF_TEST(test_json_contains_key)
{
   struct json* obj = NULL;

   pgexporter_json_create(&obj);
   pgexporter_json_put(obj, "present", (uintptr_t)1, ValueInt32);

   MCTF_ASSERT(pgexporter_json_contains_key(obj, "present"), cleanup, "should contain 'present'");
   MCTF_ASSERT(!pgexporter_json_contains_key(obj, "absent"), cleanup, "should not contain 'absent'");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* put overwrites existing key                                         */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_put_overwrites)
{
   struct json* obj = NULL;

   pgexporter_json_create(&obj);
   pgexporter_json_put(obj, "k", (uintptr_t)1, ValueInt32);
   pgexporter_json_put(obj, "k", (uintptr_t)2, ValueInt32);

   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(obj, "k"), 2, cleanup, "overwrite should yield 2");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* remove                                                              */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_remove)
{
   struct json* obj = NULL;

   pgexporter_json_create(&obj);
   pgexporter_json_put(obj, "a", (uintptr_t)10, ValueInt32);
   pgexporter_json_put(obj, "b", (uintptr_t)20, ValueInt32);

   MCTF_ASSERT_INT_EQ(pgexporter_json_remove(obj, "a"), 0, cleanup, "remove 'a' should succeed");
   MCTF_ASSERT(!pgexporter_json_contains_key(obj, "a"), cleanup, "'a' should be gone");
   MCTF_ASSERT(pgexporter_json_contains_key(obj, "b"), cleanup, "'b' should still exist");

   /* removing a missing key is a no-op */
   MCTF_ASSERT_INT_EQ(pgexporter_json_remove(obj, "a"), 0, cleanup, "remove missing key should be 0");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* array: append / array_length / iterator                            */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_array_append_and_length)
{
   struct json* arr = NULL;

   pgexporter_json_create(&arr);

   MCTF_ASSERT_INT_EQ(pgexporter_json_append(arr, (uintptr_t)1, ValueInt32), 0, cleanup, "append 1 failed");
   MCTF_ASSERT_INT_EQ(pgexporter_json_append(arr, (uintptr_t)2, ValueInt32), 0, cleanup, "append 2 failed");
   MCTF_ASSERT_INT_EQ(pgexporter_json_append(arr, (uintptr_t)3, ValueInt32), 0, cleanup, "append 3 failed");

   MCTF_ASSERT_INT_EQ((int)pgexporter_json_array_length(arr), 3, cleanup, "array length should be 3");

cleanup:
   pgexporter_json_destroy(arr);
   MCTF_FINISH();
}

MCTF_TEST(test_json_array_iterator)
{
   struct json* arr = NULL;
   struct json_iterator* iter = NULL;
   int cnt = 0;

   pgexporter_json_create(&arr);
   pgexporter_json_append(arr, (uintptr_t)10, ValueInt32);
   pgexporter_json_append(arr, (uintptr_t)20, ValueInt32);
   pgexporter_json_append(arr, (uintptr_t)30, ValueInt32);

   MCTF_ASSERT_INT_EQ(pgexporter_json_iterator_create(arr, &iter), 0, cleanup, "iterator create failed");
   MCTF_ASSERT(pgexporter_json_iterator_has_next(iter), cleanup, "iterator should have next");

   while (pgexporter_json_iterator_next(iter))
   {
      cnt++;
   }
   MCTF_ASSERT_INT_EQ(cnt, 3, cleanup, "iterator should visit 3 elements");

cleanup:
   pgexporter_json_iterator_destroy(iter);
   pgexporter_json_destroy(arr);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* clear                                                               */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_clear)
{
   struct json* obj = NULL;

   pgexporter_json_create(&obj);
   pgexporter_json_put(obj, "x", (uintptr_t)1, ValueInt32);
   pgexporter_json_put(obj, "y", (uintptr_t)2, ValueInt32);

   MCTF_ASSERT_INT_EQ(pgexporter_json_clear(obj), 0, cleanup, "clear should succeed");
   MCTF_ASSERT(!pgexporter_json_contains_key(obj, "x"), cleanup, "'x' should be gone after clear");
   MCTF_ASSERT(!pgexporter_json_contains_key(obj, "y"), cleanup, "'y' should be gone after clear");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* parse_string round-trip                                             */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_parse_string_item)
{
   struct json* obj = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_json_parse_string("{\"name\":\"pgexporter\",\"port\":5432}", &obj), 0,
                      cleanup, "parse_string failed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "parsed object is NULL");
   MCTF_ASSERT_STR_EQ((char*)pgexporter_json_get(obj, "name"), "pgexporter", cleanup, "name mismatch");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(obj, "port"), 5432, cleanup, "port mismatch");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

MCTF_TEST(test_json_parse_string_array)
{
   struct json* arr = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_json_parse_string("[1,2,3]", &arr), 0, cleanup, "parse array failed");
   MCTF_ASSERT_PTR_NONNULL(arr, cleanup, "parsed array is NULL");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_array_length(arr), 3, cleanup, "array length should be 3");

cleanup:
   pgexporter_json_destroy(arr);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_json_parse_string_invalid)
{
   struct json* obj = NULL;
   int ret = pgexporter_json_parse_string("{not valid json", &obj);

   MCTF_ASSERT(ret != 0, cleanup, "parsing invalid JSON should fail");
   MCTF_ASSERT_PTR_NULL(obj, cleanup, "obj should be NULL on parse failure");

cleanup:
   pgexporter_json_destroy(obj);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* clone independence                                                  */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_json_clone_independence)
{
   struct json* orig = NULL;
   struct json* copy = NULL;

   pgexporter_json_create(&orig);
   pgexporter_json_put(orig, "val", (uintptr_t)7, ValueInt32);

   MCTF_ASSERT_INT_EQ(pgexporter_json_clone(orig, &copy), 0, cleanup, "clone failed");
   MCTF_ASSERT_PTR_NONNULL(copy, cleanup, "clone is NULL");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(copy, "val"), 7, cleanup, "clone val mismatch");

   /* mutate original, clone must be unaffected */
   pgexporter_json_put(orig, "val", (uintptr_t)99, ValueInt32);
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(copy, "val"), 7, cleanup,
                      "clone should be independent of original");

cleanup:
   pgexporter_json_destroy(orig);
   pgexporter_json_destroy(copy);
   MCTF_FINISH();
}
