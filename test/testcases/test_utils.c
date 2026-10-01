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
#include <mctf.h>
#include <utils.h>

#include <configuration.h>
#include <limits.h>
#include <memory.h>
#include <message.h>
#include <shmem.h>
#include <stdlib.h>
#include <string.h>

MCTF_MODULE_SETUP(utils)
{
   pgexporter_memory_init();
}

MCTF_MODULE_TEARDOWN(utils)
{
   pgexporter_memory_destroy();
}

MCTF_TEST(test_utils_append_basic)
{
   char* s = NULL;

   s = pgexporter_append(s, "foo");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append to NULL should allocate a new buffer");
   MCTF_ASSERT_STR_EQ(s, "foo", cleanup, "expected 'foo'");

   s = pgexporter_append(s, "bar");
   MCTF_ASSERT_STR_EQ(s, "foobar", cleanup, "expected 'foobar' after second append");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_int_and_char)
{
   char* s = NULL;

   s = pgexporter_append(s, "n=");
   s = pgexporter_append_int(s, 42);
   s = pgexporter_append_char(s, '!');
   MCTF_ASSERT_STR_EQ(s, "n=42!", cleanup, "expected 'n=42!'");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_int_negative)
{
   char* s = NULL;

   s = pgexporter_append_int(s, -7);
   MCTF_ASSERT_STR_EQ(s, "-7", cleanup, "expected '-7'");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_compare_string)
{
   MCTF_ASSERT(pgexporter_compare_string(NULL, NULL), cleanup,
               "two NULLs should compare equal");
   MCTF_ASSERT(!pgexporter_compare_string("a", NULL), cleanup,
               "non-NULL vs NULL should differ");
   MCTF_ASSERT(!pgexporter_compare_string(NULL, "a"), cleanup,
               "NULL vs non-NULL should differ");
   MCTF_ASSERT(pgexporter_compare_string("same", "same"), cleanup,
               "identical strings should compare equal");
   MCTF_ASSERT(!pgexporter_compare_string("a", "b"), cleanup,
               "different strings should not compare equal");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_numbers)
{
   char* s = NULL;

   /* The largest value of each type is the corner case: it needs every digit
      the buffer can hold, so a size argument that is one short truncates it
      silently rather than overflowing. */
   s = pgexporter_append_int(NULL, INT_MIN);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_int returned NULL");
   MCTF_ASSERT_STR_EQ(s, "-2147483648", cleanup, "append_int truncated INT_MIN");
   free(s);
   s = NULL;

   s = pgexporter_append_int(NULL, INT_MAX);
   MCTF_ASSERT_STR_EQ(s, "2147483647", cleanup, "append_int wrong for INT_MAX");
   free(s);
   s = NULL;

   s = pgexporter_append_ulong(NULL, ULONG_MAX);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_ulong returned NULL");
   MCTF_ASSERT_STR_EQ(s, "18446744073709551615", cleanup, "append_ulong truncated ULONG_MAX");
   free(s);
   s = NULL;

   s = pgexporter_append_ullong(NULL, ULLONG_MAX);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_ullong returned NULL");
   MCTF_ASSERT_STR_EQ(s, "18446744073709551615", cleanup, "append_ullong truncated ULLONG_MAX");
   free(s);
   s = NULL;

   /* Appending onto an existing string must concatenate, not replace */
   s = pgexporter_append(NULL, "n=");
   s = pgexporter_append_ulong(s, ULONG_MAX);
   MCTF_ASSERT_STR_EQ(s, "n=18446744073709551615", cleanup, "append_ulong did not concatenate");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_double)
{
   char* s = NULL;

   /* %lf writes the whole integer part, so a large double needs far more
      room than a small fixed buffer: 1e19 alone is 20 digits before the
      six decimals. */
   s = pgexporter_append_double(NULL, 1e19);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_double returned NULL");
   MCTF_ASSERT_STR_EQ(s, "10000000000000000000.000000", cleanup, "append_double truncated 1e19");
   free(s);
   s = NULL;

   s = pgexporter_append_double(NULL, 0.5);
   MCTF_ASSERT_STR_EQ(s, "0.500000", cleanup, "append_double wrong for 0.5");
   free(s);
   s = NULL;

   s = pgexporter_append_double_precision(NULL, 1e19, 2);
   MCTF_ASSERT_STR_EQ(s, "10000000000000000000.00", cleanup, "append_double_precision truncated 1e19");
   free(s);
   s = NULL;

   s = pgexporter_append_double_precision(NULL, 3.14159, 3);
   MCTF_ASSERT_STR_EQ(s, "3.142", cleanup, "append_double_precision wrong for 3.14159");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_copy_string)
{
   char* dest = NULL;

   MCTF_ASSERT(pgexporter_copy_string("hello", &dest) == 0, cleanup, "copy_string basic failed");
   MCTF_ASSERT_PTR_NONNULL(dest, cleanup, "dest should not be NULL");
   MCTF_ASSERT_STR_EQ(dest, "hello", cleanup, "copy_string content mismatch");
   free(dest);
   dest = NULL;

   MCTF_ASSERT(pgexporter_copy_string("", &dest) == 0, cleanup, "copy_string empty failed");
   MCTF_ASSERT_PTR_NONNULL(dest, cleanup, "dest should not be NULL for empty string");
   MCTF_ASSERT_STR_EQ(dest, "", cleanup, "copy_string empty content mismatch");
   free(dest);
   dest = NULL;

   MCTF_ASSERT(pgexporter_copy_string(NULL, &dest) != 0, cleanup, "copy_string NULL should fail");
   MCTF_ASSERT_PTR_NULL(dest, cleanup, "dest should remain NULL for NULL input");

cleanup:
   if (dest != NULL)
   {
      free(dest);
      dest = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_utils_extract_username_database_normal)
{
   unsigned char* buf = NULL;
   size_t total = 4 + 4 + 5 + 6 + 9 + 5 + 1;
   struct message msg;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   int rc = 1;

   buf = malloc(total);
   MCTF_ASSERT_PTR_NONNULL(buf, cleanup, "malloc failed for startup message");
   memset(buf, 0, total);
   buf[0] = (total >> 24) & 0xFF;
   buf[1] = (total >> 16) & 0xFF;
   buf[2] = (total >> 8) & 0xFF;
   buf[3] = total & 0xFF;
   buf[4] = 0x00;
   buf[5] = 0x03;
   buf[6] = 0x00;
   buf[7] = 0x00;
   memcpy(buf + 8, "user", 5);
   memcpy(buf + 8 + 5, "alice", 6);
   memcpy(buf + 8 + 5 + 6, "database", 9);
   memcpy(buf + 8 + 5 + 6 + 9, "mydb", 5);

   memset(&msg, 0, sizeof(msg));
   msg.kind = 0;
   msg.length = (ssize_t)total;
   msg.data = buf;

   rc = pgexporter_extract_username_database(&msg, &username, &database, &appname);
   MCTF_ASSERT_INT_EQ(rc, 0, cleanup, "extract normal should return 0");
   MCTF_ASSERT_PTR_NONNULL(username, cleanup, "username should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(database, cleanup, "database should not be NULL");
   MCTF_ASSERT_STR_EQ(username, "alice", cleanup, "username should be alice");
   MCTF_ASSERT_STR_EQ(database, "mydb", cleanup, "database should be mydb");

cleanup:
   free(username);
   free(database);
   free(appname);
   free(buf);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_extract_username_database_defaults)
{
   unsigned char* buf = NULL;
   const char* name = "alice";
   size_t namelen = strlen(name);
   size_t total = 4 + 4 + 5 + namelen + 1 + 1;
   struct message msg;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   int rc = 1;

   buf = malloc(total);
   MCTF_ASSERT_PTR_NONNULL(buf, cleanup, "malloc failed for startup message");
   memset(buf, 0, total);
   buf[0] = (total >> 24) & 0xFF;
   buf[1] = (total >> 16) & 0xFF;
   buf[2] = (total >> 8) & 0xFF;
   buf[3] = total & 0xFF;
   buf[4] = 0x00;
   buf[5] = 0x03;
   buf[6] = 0x00;
   buf[7] = 0x00;
   memcpy(buf + 8, "user", 5);
   memcpy(buf + 8 + 5, name, namelen + 1);

   memset(&msg, 0, sizeof(msg));
   msg.kind = 0;
   msg.length = (ssize_t)total;
   msg.data = buf;

   rc = pgexporter_extract_username_database(&msg, &username, &database, &appname);
   MCTF_ASSERT_INT_EQ(rc, 0, cleanup, "extract defaults should return 0");
   MCTF_ASSERT_PTR_NONNULL(username, cleanup, "username should not be NULL");
   MCTF_ASSERT_PTR_NONNULL(database, cleanup, "database should default to non-NULL");
   MCTF_ASSERT_STR_EQ(database, username, cleanup, "database should equal username content");
   MCTF_ASSERT(username != database, cleanup, "database must be a copy, not aliased to username");

cleanup:
   free(username);
   free(database);
   free(appname);
   free(buf);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_utils_extract_username_database_both_missing)
{
   unsigned char* buf = NULL;
   size_t total = 4 + 4 + 1;
   struct message msg;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   int rc = 0;

   buf = malloc(total);
   MCTF_ASSERT_PTR_NONNULL(buf, cleanup, "malloc failed for startup message");
   memset(buf, 0, total);
   buf[0] = (total >> 24) & 0xFF;
   buf[1] = (total >> 16) & 0xFF;
   buf[2] = (total >> 8) & 0xFF;
   buf[3] = total & 0xFF;
   buf[4] = 0x00;
   buf[5] = 0x03;
   buf[6] = 0x00;
   buf[7] = 0x00;

   memset(&msg, 0, sizeof(msg));
   msg.kind = 0;
   msg.length = (ssize_t)total;
   msg.data = buf;

   rc = pgexporter_extract_username_database(&msg, &username, &database, &appname);
   MCTF_ASSERT(rc != 0, cleanup, "extract with both missing should fail");
   MCTF_ASSERT_PTR_NULL(username, cleanup, "username should be NULL on failure");
   MCTF_ASSERT_PTR_NULL(database, cleanup, "database should be NULL on failure");

cleanup:
   free(username);
   free(database);
   free(appname);
   free(buf);
   MCTF_FINISH();
}
