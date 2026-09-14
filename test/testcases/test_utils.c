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

/* ------------------------------------------------------------------ */
/* pgexporter_snprintf                                                 */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_snprintf_basic)
{
   char buf[64];
   int ret;

   ret = pgexporter_snprintf(buf, sizeof(buf), "hello %s, port %d", "world", 5432);
   MCTF_ASSERT_STR_EQ(buf, "hello world, port 5432", cleanup, "snprintf basic mismatch");
   MCTF_ASSERT(ret > 0, cleanup, "snprintf should return positive length");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_snprintf_truncation)
{
   char buf[8];

   pgexporter_snprintf(buf, sizeof(buf), "toolongstring");
   /* Buffer must be NUL-terminated and not overflow */
   MCTF_ASSERT_INT_EQ((int)buf[sizeof(buf) - 1], 0, cleanup, "last byte must be NUL");
   MCTF_ASSERT((int)strlen(buf) < (int)sizeof(buf), cleanup, "truncated string must fit in buffer");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_snprintf_percent_escape)
{
   char buf[16];

   pgexporter_snprintf(buf, sizeof(buf), "100%%");
   MCTF_ASSERT_STR_EQ(buf, "100%", cleanup, "%%%% should produce a single %%");

cleanup:
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_starts_with / pgexporter_ends_with                      */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_starts_with)
{
   MCTF_ASSERT(pgexporter_starts_with("pgexporter_metric", "pgexporter"), cleanup,
               "should start with 'pgexporter'");
   MCTF_ASSERT(!pgexporter_starts_with("metric_pgexporter", "pgexporter"), cleanup,
               "should not start with 'pgexporter'");
   MCTF_ASSERT(pgexporter_starts_with("abc", ""), cleanup, "every string starts with empty prefix");
   MCTF_ASSERT(pgexporter_starts_with("abc", "abc"), cleanup, "string starts with itself");
   MCTF_ASSERT(!pgexporter_starts_with("ab", "abc"), cleanup, "shorter string cannot start with longer prefix");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_ends_with)
{
   MCTF_ASSERT(pgexporter_ends_with("file.conf", ".conf"), cleanup, "should end with '.conf'");
   MCTF_ASSERT(!pgexporter_ends_with("file.conf", ".yaml"), cleanup, "should not end with '.yaml'");
   MCTF_ASSERT(pgexporter_ends_with("abc", ""), cleanup, "every string ends with empty suffix");
   MCTF_ASSERT(pgexporter_ends_with("abc", "abc"), cleanup, "string ends with itself");
   MCTF_ASSERT(!pgexporter_ends_with("ab", "abc"), cleanup, "shorter string cannot end with longer suffix");

cleanup:
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_append_bool / pgexporter_append_ulong                   */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_append_bool)
{
   char* s = NULL;

   /* pgexporter_append_bool() renders booleans numerically as "1"/"0". */
   s = pgexporter_append_bool(s, true);
   MCTF_ASSERT_STR_EQ(s, "1", cleanup, "expected '1'");
   free(s);
   s = NULL;

   s = pgexporter_append_bool(s, false);
   MCTF_ASSERT_STR_EQ(s, "0", cleanup, "expected '0'");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_ulong)
{
   char* s = NULL;

   s = pgexporter_append_ulong(s, 123456789UL);
   MCTF_ASSERT_STR_EQ(s, "123456789", cleanup, "expected '123456789'");

cleanup:
   free(s);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_vappend                                                  */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_vappend)
{
   char* s = NULL;

   s = pgexporter_vappend(s, 3, "foo", "bar", "baz");
   MCTF_ASSERT_STR_EQ(s, "foobarbaz", cleanup, "vappend of 3 strings mismatch");

cleanup:
   free(s);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_is_number                                                */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_is_number)
{
   MCTF_ASSERT(pgexporter_is_number("42", 10), cleanup, "'42' is a base-10 number");
   MCTF_ASSERT(pgexporter_is_number("0", 10), cleanup, "'0' is a base-10 number");
   MCTF_ASSERT(!pgexporter_is_number("12abc", 10), cleanup, "'12abc' is not a number");
   MCTF_ASSERT(!pgexporter_is_number("", 10), cleanup, "empty string is not a number");
   MCTF_ASSERT(pgexporter_is_number("ff", 16), cleanup, "'ff' is a base-16 number");
   MCTF_ASSERT(pgexporter_is_number("FF", 16), cleanup, "'FF' is a base-16 number");
   MCTF_ASSERT(!pgexporter_is_number("gg", 16), cleanup, "'gg' is not a base-16 number");

cleanup:
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_version_as_number / pgexporter_version_ge               */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_version_as_number)
{
   MCTF_ASSERT_INT_EQ((int)pgexporter_version_as_number(1, 5, 0), 10500, cleanup,
                      "1.5.0 should be 10500");
   MCTF_ASSERT_INT_EQ((int)pgexporter_version_as_number(0, 0, 1), 1, cleanup,
                      "0.0.1 should be 1");
   MCTF_ASSERT_INT_EQ((int)pgexporter_version_as_number(2, 0, 0), 20000, cleanup,
                      "2.0.0 should be 20000");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_version_ge)
{
   unsigned int major, minor, patch;

   /* Get current version components from the compiled-in number */
   unsigned int cur = pgexporter_version_number();
   major = cur / 10000;
   minor = (cur % 10000) / 100;
   patch = cur % 100;

   MCTF_ASSERT(pgexporter_version_ge(major, minor, patch), cleanup,
               "current version should be >= itself");
   MCTF_ASSERT(pgexporter_version_ge(0, 0, 0), cleanup,
               "any version should be >= 0.0.0");
   MCTF_ASSERT(!pgexporter_version_ge(major + 1, 0, 0), cleanup,
               "current version should not be >= next major");

cleanup:
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_base64 round-trip                                        */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_base64_roundtrip)
{
   const char* original = "pgexporter-base64-test";
   char* encoded = NULL;
   size_t encoded_len = 0;
   void* decoded = NULL;
   size_t decoded_len = 0;

   MCTF_ASSERT_INT_EQ(
      pgexporter_base64_encode((void*)original, strlen(original), &encoded, &encoded_len),
      0, cleanup, "base64 encode failed");
   MCTF_ASSERT_PTR_NONNULL(encoded, cleanup, "encoded is NULL");
   MCTF_ASSERT(encoded_len > 0, cleanup, "encoded length should be > 0");

   MCTF_ASSERT_INT_EQ(
      pgexporter_base64_decode(encoded, encoded_len, &decoded, &decoded_len),
      0, cleanup, "base64 decode failed");
   MCTF_ASSERT_PTR_NONNULL(decoded, cleanup, "decoded is NULL");
   MCTF_ASSERT_INT_EQ((int)decoded_len, (int)strlen(original), cleanup, "decoded length mismatch");
   MCTF_ASSERT_INT_EQ(memcmp(decoded, original, decoded_len), 0, cleanup, "decoded content mismatch");

cleanup:
   free(encoded);
   free(decoded);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_is_valid_metric_name                                     */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_is_valid_metric_name)
{
   MCTF_ASSERT(pgexporter_is_valid_metric_name("pg_up"), cleanup, "pg_up should be valid");
   MCTF_ASSERT(pgexporter_is_valid_metric_name("pgexporter_connections_total"), cleanup,
               "snake_case should be valid");
   /*
    * pgexporter_is_valid_metric_name() validates the character set only; it does
    * not constrain the first character. A leading digit is therefore accepted,
    * even though Prometheus itself requires [a-zA-Z_:][a-zA-Z0-9_:]*. Asserted
    * here to pin the current behaviour rather than the desired one.
    */
   MCTF_ASSERT(pgexporter_is_valid_metric_name("1starts_with_digit"), cleanup,
               "leading digit is currently accepted (charset-only validation)");
   MCTF_ASSERT(!pgexporter_is_valid_metric_name("has-hyphen"), cleanup,
               "hyphen should be invalid");
   MCTF_ASSERT(!pgexporter_is_valid_metric_name(""), cleanup, "empty name should be invalid");

cleanup:
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* binary read/write round-trips                                       */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_utils_readwrite_int32)
{
   char buf[4];

   pgexporter_write_int32(buf, 0x12345678);
   MCTF_ASSERT_INT_EQ((int)pgexporter_read_int32(buf), (int)0x12345678, cleanup,
                      "int32 round-trip mismatch");

   pgexporter_write_int32(buf, -1);
   MCTF_ASSERT_INT_EQ((int)pgexporter_read_int32(buf), -1, cleanup, "int32 -1 round-trip mismatch");

   pgexporter_write_int32(buf, 0);
   MCTF_ASSERT_INT_EQ((int)pgexporter_read_int32(buf), 0, cleanup, "int32 0 round-trip mismatch");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_readwrite_int64)
{
   char buf[8];
   int64_t val = 0x0102030405060708LL;

   pgexporter_write_int64(buf, val);
   MCTF_ASSERT(pgexporter_read_int64(buf) == val, cleanup, "int64 round-trip mismatch");

   pgexporter_write_int64(buf, -1LL);
   MCTF_ASSERT(pgexporter_read_int64(buf) == -1LL, cleanup, "int64 -1 round-trip mismatch");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_readwrite_byte)
{
   char buf[1];

   pgexporter_write_byte(buf, 0x42);
   MCTF_ASSERT_INT_EQ((int)pgexporter_read_byte(buf), (int)(signed char)0x42, cleanup,
                      "byte round-trip mismatch");

   pgexporter_write_byte(buf, -1);
   MCTF_ASSERT_INT_EQ((int)pgexporter_read_byte(buf), -1, cleanup, "byte -1 round-trip mismatch");

cleanup:
   MCTF_FINISH();
}
