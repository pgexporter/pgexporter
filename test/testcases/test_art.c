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
 *
 */

#include <pgexporter.h>
#include <art.h>
#include <value.h>

#include <mctf.h>
#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_art_prefix_search)
{
   struct art* t = NULL;
   char** matches = NULL;
   int count = 0;

   pgexporter_art_create(&t);
   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "ART creation failed");

   MCTF_ASSERT(!pgexporter_art_insert(t, "apple", 1, ValueInt32), cleanup, "Insert apple failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "applet", 2, ValueInt32), cleanup, "Insert applet failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "apply", 3, ValueInt32), cleanup, "Insert apply failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "ball", 4, ValueInt32), cleanup, "Insert ball failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "bat", 5, ValueInt32), cleanup, "Insert bat failed");

   count = pgexporter_art_prefix_search(t, "app", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "Prefix search 'app' should return 3 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "apple", cleanup, "First match mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "applet", cleanup, "Second match mismatch");
   MCTF_ASSERT_STR_EQ(matches[2], "apply", cleanup, "Third match mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "cat", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "Prefix search 'cat' should return 0 matches");
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "apple", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "Prefix search 'apple' should return 2 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "apple", cleanup, "First match mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "applet", cleanup, "Second match mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 5, cleanup, "Empty prefix should return all matches");
   MCTF_ASSERT_STR_EQ(matches[0], "apple", cleanup, "Match 0 mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "applet", cleanup, "Match 1 mismatch");
   MCTF_ASSERT_STR_EQ(matches[2], "apply", cleanup, "Match 2 mismatch");
   MCTF_ASSERT_STR_EQ(matches[3], "ball", cleanup, "Match 3 mismatch");
   MCTF_ASSERT_STR_EQ(matches[4], "bat", cleanup, "Match 4 mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "app", &matches, 2);
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "Max matches should limit results to 2");
   MCTF_ASSERT_STR_EQ(matches[0], "apple", cleanup, "Match 0 mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "applet", cleanup, "Match 1 mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "applet", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "Prefix search 'applet' should return exactly 1 match");
   MCTF_ASSERT_STR_EQ(matches[0], "applet", cleanup, "Exact match mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   MCTF_ASSERT(!pgexporter_art_delete(t, "applet"), cleanup, "Delete applet failed");
   count = pgexporter_art_prefix_search(t, "app", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "Prefix search 'app' after delete should return 2 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "apple", cleanup, "Match 0 mismatch after delete");
   MCTF_ASSERT_STR_EQ(matches[1], "apply", cleanup, "Match 1 mismatch after delete");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(NULL, "app", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, -1, cleanup, "Prefix search with NULL tree should return -1");

   count = pgexporter_art_prefix_search(t, "app", NULL, 10);
   MCTF_ASSERT_INT_EQ(count, -1, cleanup, "Prefix search with NULL matches pointer should return -1");

   count = pgexporter_art_prefix_search(t, "app", &matches, 0);
   MCTF_ASSERT_INT_EQ(count, -1, cleanup, "Prefix search with 0 max_matches should return -1");

cleanup:
   if (matches)
   {
      for (int i = 0; i < count; i++)
      {
         free(matches[i]);
      }
      free(matches);
   }
   pgexporter_art_destroy(t);
   MCTF_FINISH();
}

MCTF_TEST(test_art_prefix_search_nested)
{
   struct art* t = NULL;
   char** matches = NULL;
   int count = 0;

   pgexporter_art_create(&t);
   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "ART creation failed");

   MCTF_ASSERT(!pgexporter_art_insert(t, "api.foo.bar", 1, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "api.foo.baz", 2, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "api.foe.fum", 3, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "abc.123.456", 4, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "api.foo", 5, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "api", 6, ValueInt32), cleanup, "Insert failed");

   count = pgexporter_art_prefix_search(t, "api", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 5, cleanup, "Prefix 'api' should return 5 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "api", cleanup, "Match 0 mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "api.foe.fum", cleanup, "Match 1 mismatch");
   MCTF_ASSERT_STR_EQ(matches[2], "api.foo", cleanup, "Match 2 mismatch");
   MCTF_ASSERT_STR_EQ(matches[3], "api.foo.bar", cleanup, "Match 3 mismatch");
   MCTF_ASSERT_STR_EQ(matches[4], "api.foo.baz", cleanup, "Match 4 mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "a", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 6, cleanup, "Prefix 'a' should return 6 matches");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "b", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "Prefix 'b' should return 0 matches");
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "api.", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 4, cleanup, "Prefix 'api.' should return 4 matches");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "api.foo.ba", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "Prefix 'api.foo.ba' should return 2 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "api.foo.bar", cleanup, "Match 0 mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "api.foo.baz", cleanup, "Match 1 mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

   count = pgexporter_art_prefix_search(t, "api.end", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "Prefix 'api.end' should return 0 matches");
   free(matches);
   matches = NULL;

cleanup:
   if (matches)
   {
      for (int i = 0; i < count; i++)
      {
         free(matches[i]);
      }
      free(matches);
   }
   pgexporter_art_destroy(t);
   MCTF_FINISH();
}

MCTF_TEST(test_art_prefix_search_long_prefix)
{
   struct art* t = NULL;
   char** matches = NULL;
   int count = 0;

   pgexporter_art_create(&t);
   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "ART creation failed");

   MCTF_ASSERT(!pgexporter_art_insert(t, "this:key:has:a:long:prefix:3", 3, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "this:key:has:a:long:common:prefix:2", 2, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "this:key:has:a:long:common:prefix:1", 1, ValueInt32), cleanup, "Insert failed");

   count = pgexporter_art_prefix_search(t, "this:key:has", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "Prefix 'this:key:has' should return 3 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "this:key:has:a:long:common:prefix:1", cleanup, "Match 0 mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "this:key:has:a:long:common:prefix:2", cleanup, "Match 1 mismatch");
   MCTF_ASSERT_STR_EQ(matches[2], "this:key:has:a:long:prefix:3", cleanup, "Match 2 mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

cleanup:
   if (matches)
   {
      for (int i = 0; i < count; i++)
      {
         free(matches[i]);
      }
      free(matches);
   }
   pgexporter_art_destroy(t);
   MCTF_FINISH();
}

MCTF_TEST(test_art_prefix_search_max_prefix_len)
{
   struct art* t = NULL;
   char** matches = NULL;
   int count = 0;

   pgexporter_art_create(&t);
   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "ART creation failed");

   MCTF_ASSERT(!pgexporter_art_insert(t, "foobarbaz1-test1-foo", 1, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "foobarbaz1-test1-bar", 2, ValueInt32), cleanup, "Insert failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "foobarbaz1-test2-foo", 3, ValueInt32), cleanup, "Insert failed");

   count = pgexporter_art_prefix_search(t, "foobarbaz1-test1", &matches, 10);
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "Prefix 'foobarbaz1-test1' should return 2 matches");
   MCTF_ASSERT_STR_EQ(matches[0], "foobarbaz1-test1-bar", cleanup, "Match 0 mismatch");
   MCTF_ASSERT_STR_EQ(matches[1], "foobarbaz1-test1-foo", cleanup, "Match 1 mismatch");
   for (int i = 0; i < count; i++)
   {
      free(matches[i]);
   }
   free(matches);
   matches = NULL;

cleanup:
   if (matches)
   {
      for (int i = 0; i < count; i++)
      {
         free(matches[i]);
      }
      free(matches);
   }
   pgexporter_art_destroy(t);
   MCTF_FINISH();
}

MCTF_TEST(test_art_iterator_remove_collapse)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   int cnt = 0;

   pgexporter_art_create(&t);
   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "ART creation failed");

   MCTF_ASSERT(!pgexporter_art_insert(t, "aa", 1, ValueInt32), cleanup, "Insert aa failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "ab", 2, ValueInt32), cleanup, "Insert ab failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "ba", 3, ValueInt32), cleanup, "Insert ba failed");
   MCTF_ASSERT(!pgexporter_art_insert(t, "bb", 4, ValueInt32), cleanup, "Insert bb failed");

   MCTF_ASSERT(!pgexporter_art_iterator_create(t, &iter), cleanup, "Iterator creation failed");

   while (pgexporter_art_iterator_next(iter))
   {
      cnt++;
      if (strcmp(iter->key, "aa") == 0 || strcmp(iter->key, "ab") == 0)
      {
         pgexporter_art_iterator_remove(iter);
      }
   }

   MCTF_ASSERT_INT_EQ(cnt, 4, cleanup, "visited count mismatch");
   MCTF_ASSERT_INT_EQ(t->size, 2, cleanup, "size should be 2");
   MCTF_ASSERT(!pgexporter_art_contains_key(t, "aa"), cleanup, "aa should be removed");
   MCTF_ASSERT(!pgexporter_art_contains_key(t, "ab"), cleanup, "ab should be removed");
   MCTF_ASSERT(pgexporter_art_contains_key(t, "ba"), cleanup, "ba should remain");
   MCTF_ASSERT(pgexporter_art_contains_key(t, "bb"), cleanup, "bb should remain");

cleanup:
   pgexporter_art_iterator_destroy(iter);
   pgexporter_art_destroy(t);
   MCTF_FINISH();
}
