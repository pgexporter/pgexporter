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
#include <configuration.h>
#include <json.h>
#include <management.h>
#include <network.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <mctf.h>
#include <stdlib.h>

MCTF_TEST(test_configuration_time_format_output)
{
   pgexporter_time_t t;
   char* str = NULL;
   int ret;

   t = PGEXPORTER_TIME_MS(500);
   ret = pgexporter_time_format(t, FORMAT_TIME_MS, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for milliseconds");
   MCTF_ASSERT_STR_EQ(str, "500ms", cleanup, "time_format string mismatch for 500ms");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_SEC(10);
   ret = pgexporter_time_format(t, FORMAT_TIME_S, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for seconds");
   MCTF_ASSERT_STR_EQ(str, "10s", cleanup, "time_format string mismatch for 10s");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_MIN(5);
   ret = pgexporter_time_format(t, FORMAT_TIME_MIN, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for minutes");
   MCTF_ASSERT_STR_EQ(str, "5m", cleanup, "time_format string mismatch for 5m");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_HOUR(2);
   ret = pgexporter_time_format(t, FORMAT_TIME_HOUR, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for hours");
   MCTF_ASSERT_STR_EQ(str, "2h", cleanup, "time_format string mismatch for 2h");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_DAY(1);
   ret = pgexporter_time_format(t, FORMAT_TIME_DAY, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for days");
   MCTF_ASSERT_STR_EQ(str, "1d", cleanup, "time_format string mismatch for 1d");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_MS(0);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for timestamp epoch 0");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:00.000Z", cleanup, "time_format string mismatch for epoch 0");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_MS(1000);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for timestamp 1000ms");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:01.000Z", cleanup, "time_format string mismatch for 1000ms");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_MS(1500);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for timestamp 1500ms");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:01.500Z", cleanup, "time_format string mismatch for 1500ms");
   free(str);
   str = NULL;

   t = PGEXPORTER_TIME_MS(946684800000LL);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format failed for year 2000");
   MCTF_ASSERT_STR_EQ(str, "2000-01-01T00:00:00.000Z", cleanup, "time_format string mismatch for year 2000");
   free(str);
   str = NULL;

   ret = pgexporter_time_format(t, FORMAT_TIME_MS, NULL);
   MCTF_ASSERT_INT_EQ(ret, 1, cleanup, "expected error for NULL output");

cleanup:
   if (str != NULL)
   {
      free(str);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_time)
{
   pgexporter_test_setup();

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "0", 0) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=0");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10s", 10) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=10s");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 120) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=2m");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h", 3600) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=1h");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1d", 86400) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=1d");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1w", 7 * 24 * 3600) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=1w");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "5ms", 5) == 0,
               cleanup, "conf set failed for metrics_query_timeout=5ms");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "50MS", 50) == 0,
               cleanup, "conf set failed for metrics_query_timeout=50MS");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "1S", 1000) == 0,
               cleanup, "conf set failed for metrics_query_timeout=1S");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "2M", 120000) == 0,
               cleanup, "conf set failed for metrics_query_timeout=2M");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1H", 3600) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=1H");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1D", 86400) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=1D");

cleanup:
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_configuration_reject_invalid_time)
{
   pgexporter_test_setup();

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10x") == 0,
               cleanup, "expected conf set to fail for invalid suffix 10x");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "-1s") == 0,
               cleanup, "expected conf set to fail for negative value -1s");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h5ms") == 0,
               cleanup, "expected conf set to fail for mixed units 1h5ms");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h 5ms") == 0,
               cleanup, "expected conf set to fail for mixed units 1h 5ms");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10 s") == 0,
               cleanup, "expected conf set to fail for space between number and unit");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "abc") == 0,
               cleanup, "expected conf set to fail for non-numeric value abc");

cleanup:
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_get_returns_set_values)
{
   pgexporter_test_setup();

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "45s", 45) == 0,
               cleanup, "conf set failed for blocking_timeout=45s");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 120) == 0,
               cleanup, "conf set failed for metrics_cache_max_age=2m");

   MCTF_ASSERT(pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "500ms", 500) == 0,
               cleanup, "conf set failed for metrics_query_timeout=500ms");

   MCTF_ASSERT(pgexporter_test_assert_conf_get_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, 45) == 0,
               cleanup, "conf get failed for blocking_timeout");

   MCTF_ASSERT(pgexporter_test_assert_conf_get_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, 120) == 0,
               cleanup, "conf get failed for metrics_cache_max_age");

   MCTF_ASSERT(pgexporter_test_assert_conf_get_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, 500) == 0,
               cleanup, "conf get failed for metrics_query_timeout");

cleanup:
   pgexporter_test_teardown();
   MCTF_FINISH();
}

static int
test_enum_to_str(char* where, int value)
{
   if (!where)
   {
      return 1;
   }

   switch (value)
   {
      case 0:
         snprintf(where, MISC_LENGTH, "%s", "off");
         break;
      case 1:
         snprintf(where, MISC_LENGTH, "%s", "foreground");
         break;
      case 2:
         snprintf(where, MISC_LENGTH, "%s", "background");
         break;
      default:
         return 1;
   }

   return 0;
}

static int
assert_server_tls_mode(char* key, char* value, int expected_mode, char* expected_string)
{
   int socket = -1;
   struct json* read = NULL;
   struct json* outcome = NULL;
   struct json* response = NULL;
   struct json* server = NULL;
   struct json* tls = NULL;

   socket = pgexporter_tsclient_get_connection();
   if (!pgexporter_socket_isvalid(socket))
   {
      goto fail;
   }

   if (pgexporter_management_request_conf_set(NULL, socket, key, value,
                                              MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE,
                                              MANAGEMENT_OUTPUT_FORMAT_JSON) != 0)
   {
      goto fail;
   }

   if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read) != 0)
   {
      goto fail;
   }

   outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!(bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      goto fail;
   }

   response = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);
   server = (struct json*)pgexporter_json_get(response, "primary");
   if (server == NULL)
   {
      goto fail;
   }

   tls = (struct json*)pgexporter_json_get(server, CONFIGURATION_ARGUMENT_TLS);
   if (tls == NULL)
   {
      goto fail;
   }

   if ((int)pgexporter_json_get(tls, "value") != expected_mode)
   {
      goto fail;
   }

   if (strcmp((char*)pgexporter_json_get(tls, "string_value"), expected_string))
   {
      goto fail;
   }

   pgexporter_json_destroy(read);
   pgexporter_disconnect(socket);
   socket = -1;
   read = NULL;

   socket = pgexporter_tsclient_get_connection();
   if (!pgexporter_socket_isvalid(socket))
   {
      goto fail;
   }

   if (pgexporter_management_request_conf_get(NULL, socket,
                                              MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE,
                                              MANAGEMENT_OUTPUT_FORMAT_JSON) != 0)
   {
      goto fail;
   }

   if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read) != 0)
   {
      goto fail;
   }

   outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!(bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      goto fail;
   }

   response = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);
   response = (struct json*)pgexporter_json_get(response, "server");
   if (response == NULL)
   {
      goto fail;
   }

   server = (struct json*)pgexporter_json_get(response, "primary");
   if (server == NULL)
   {
      goto fail;
   }

   tls = (struct json*)pgexporter_json_get(server, CONFIGURATION_ARGUMENT_TLS);
   if (tls == NULL)
   {
      goto fail;
   }

   if ((int)pgexporter_json_get(tls, "value") != expected_mode)
   {
      goto fail;
   }

   if (strcmp((char*)pgexporter_json_get(tls, "string_value"), expected_string))
   {
      goto fail;
   }

   pgexporter_json_destroy(read);
   pgexporter_disconnect(socket);
   return 0;

fail:
   if (read)
   {
      pgexporter_json_destroy(read);
   }
   if (socket >= 0)
   {
      pgexporter_disconnect(socket);
   }
   return -1;
}

MCTF_TEST(test_configuration_json_put_enum_value)
{
   struct json* res = NULL;
   struct json* nested = NULL;

   pgexporter_json_create(&res);

   pgexporter_json_put_enum_value(res, "validation", 1, test_enum_to_str);

   nested = (struct json*)pgexporter_json_get(res, "validation");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "nested object should exist");
   MCTF_ASSERT(pgexporter_json_contains_key(nested, "value"), cleanup, "should contain 'value' key");
   MCTF_ASSERT(pgexporter_json_contains_key(nested, "string_value"), cleanup, "should contain 'string_value' key");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(nested, "value"), 1, cleanup, "value should be 1");
   MCTF_ASSERT_STR_EQ((char*)pgexporter_json_get(nested, "string_value"), "foreground", cleanup, "string_value should be 'foreground'");

cleanup:
   pgexporter_json_destroy(res);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_server_tls_mode_roundtrip)
{
   pgexporter_test_setup();
   pgexporter_test_config_save();

   MCTF_ASSERT(assert_server_tls_mode("primary.tls", "off", SERVER_TLS_OFF, "off") == 0,
               cleanup, "server tls mode roundtrip failed for off");
   MCTF_ASSERT(assert_server_tls_mode("primary.tls", "try", SERVER_TLS_TRY, "try") == 0,
               cleanup, "server tls mode roundtrip failed for try");
   MCTF_ASSERT(assert_server_tls_mode("primary.tls", "on", SERVER_TLS_ON, "on") == 0,
               cleanup, "server tls mode roundtrip failed for on");

cleanup:
   /* pgexporter_test_config_restore only reverts the test's local shmem copy; the daemon's
    * in-memory config is unaffected by that call. Push the default back over the management
    * API so subsequent tests don't inherit tls_mode=on (which would fail auth without a CA). */
   (void)assert_server_tls_mode("primary.tls", "try", SERVER_TLS_TRY, "try");
   pgexporter_test_config_restore();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_configuration_server_tls_mode_reject_invalid)
{
   pgexporter_test_setup();

   MCTF_ASSERT(pgexporter_test_assert_conf_set_fail("primary.tls", "invalid") == 0,
               cleanup, "expected server tls mode to reject invalid value");

cleanup:
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_json_put_time_value)
{
   struct json* res = NULL;
   struct json* nested = NULL;

   pgexporter_json_create(&res);

   // Seconds
   pgexporter_json_put_time_value(res, "idle_timeout", PGEXPORTER_TIME_SEC(30), FORMAT_TIME_S);

   nested = (struct json*)pgexporter_json_get(res, "idle_timeout");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "seconds: nested object should exist");

   MCTF_ASSERT(pgexporter_json_contains_key(nested, "value"), cleanup, "seconds: should contain 'value' key");
   MCTF_ASSERT(pgexporter_json_contains_key(nested, "string_value"), cleanup, "seconds: should contain 'string_value' key");

   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(nested, "value"), 30, cleanup, "seconds: value should be 30");
   MCTF_ASSERT_STR_EQ((char*)pgexporter_json_get(nested, "string_value"), "30s", cleanup, "seconds: string_value should be '30s'");

   // Minutes
   pgexporter_json_put_time_value(res, "blocking_timeout", PGEXPORTER_TIME_MIN(5), FORMAT_TIME_MIN);

   nested = (struct json*)pgexporter_json_get(res, "blocking_timeout");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "minutes: nested object should exist");

   MCTF_ASSERT(pgexporter_json_contains_key(nested, "value"), cleanup, "minutes: should contain 'value' key");
   MCTF_ASSERT(pgexporter_json_contains_key(nested, "string_value"), cleanup, "minutes: should contain 'string_value' key");

   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(nested, "value"), 5, cleanup, "minutes: value should be 5");
   MCTF_ASSERT_STR_EQ((char*)pgexporter_json_get(nested, "string_value"), "5m", cleanup, "minutes: string_value should be '5m'");

cleanup:
   pgexporter_json_destroy(res);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_json_put_size_value)
{
   struct json* res = NULL;
   struct json* nested = NULL;

   pgexporter_json_create(&res);

   pgexporter_json_put_size_value(res, "buffer_size", 1048576);

   nested = (struct json*)pgexporter_json_get(res, "buffer_size");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "nested object should exist");
   MCTF_ASSERT(pgexporter_json_contains_key(nested, "value"), cleanup, "should contain 'value' key");
   MCTF_ASSERT(pgexporter_json_contains_key(nested, "string_value"), cleanup, "should contain 'string_value' key");
   MCTF_ASSERT_INT_EQ((int)pgexporter_json_get(nested, "value"), 1048576, cleanup, "value should be 1048576");
   MCTF_ASSERT_STR_EQ((char*)pgexporter_json_get(nested, "string_value"), "1 MB", cleanup, "string_value should be '1 MB'");

cleanup:
   pgexporter_json_destroy(res);
   MCTF_FINISH();
}
