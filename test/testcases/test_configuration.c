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
#include <shmem.h>
#include <tscommon.h>
#include <tssuite.h>
#include <utils.h>

// Test conf set with various time units
START_TEST(test_configuration_accept_time)
{
   // Zero / disabled
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "0", 0);

   // Seconds (response in seconds)
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10s", 10);

   // Minutes (response in seconds)
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 120);

   // Hours (response in seconds)
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h", 3600);

   // Days (response in seconds)
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1d", 86400);

   // Weeks (response in seconds)
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1w", 7 * 24 * 3600);

   // Milliseconds (response in milliseconds)
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "5ms", 5);

   // Uppercase suffix
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "50MS", 50);
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "1S", 1000);
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "2M", 120000);
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1H", 3600);
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1D", 86400);
}
END_TEST

// Test conf set rejects invalid time values
START_TEST(test_configuration_reject_invalid_time)
{
   // Invalid suffix
   pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10x");

   // Negative value
   pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "-1s");

   // Mixed units
   pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h5ms");
   pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h 5ms");

   // Space between number and unit
   pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10 s");

   // Non-numeric
   pgexporter_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "abc");
}
END_TEST

// Test conf get returns correct values after conf set
START_TEST(test_configuration_get_returns_set_values)
{
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "45s", 45);
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 120);
   pgexporter_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, "500ms", 500);

   pgexporter_test_assert_conf_get_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, 45);
   pgexporter_test_assert_conf_get_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, 120);
   pgexporter_test_assert_conf_get_ok(CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, 500);
}
END_TEST

// Test pgexporter_time_format produces correct strings
START_TEST(test_configuration_time_format_output)
{
   pgexporter_time_t t;
   char* str = NULL;
   int ret;

   // Milliseconds
   t = PGEXPORTER_TIME_MS(500);
   ret = pgexporter_time_format(t, FORMAT_TIME_MS, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "500ms");
   free(str);

   // Seconds
   t = PGEXPORTER_TIME_SEC(10);
   ret = pgexporter_time_format(t, FORMAT_TIME_S, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "10s");
   free(str);

   // Minutes
   t = PGEXPORTER_TIME_MIN(5);
   ret = pgexporter_time_format(t, FORMAT_TIME_MIN, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "5m");
   free(str);

   // Hours
   t = PGEXPORTER_TIME_HOUR(2);
   ret = pgexporter_time_format(t, FORMAT_TIME_HOUR, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "2h");
   free(str);

   // Days
   t = PGEXPORTER_TIME_DAY(1);
   ret = pgexporter_time_format(t, FORMAT_TIME_DAY, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "1d");
   free(str);

   // Timestamp (epoch 0)
   t = PGEXPORTER_TIME_MS(0);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "1970-01-01T00:00:00.000Z");
   free(str);

   // Timestamp (1000ms = 1 second)
   t = PGEXPORTER_TIME_MS(1000);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "1970-01-01T00:00:01.000Z");
   free(str);

   // Timestamp with millisecond precision
   t = PGEXPORTER_TIME_MS(1500);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "1970-01-01T00:00:01.500Z");
   free(str);

   // Timestamp for year 2000
   t = PGEXPORTER_TIME_MS(946684800000LL);
   ret = pgexporter_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   ck_assert_int_eq(ret, 0);
   ck_assert_str_eq(str, "2000-01-01T00:00:00.000Z");
   free(str);

   // NULL output should return error
   ret = pgexporter_time_format(t, FORMAT_TIME_MS, NULL);
   ck_assert_int_eq(ret, 1);
}
END_TEST

Suite*
pgexporter_test_configuration_suite()
{
   Suite* s;
   TCase* tc_configuration;

   s = suite_create("pgexporter_test_configuration");

   tc_configuration = tcase_create("Configuration");

   tcase_set_timeout(tc_configuration, 60);
   tcase_add_checked_fixture(tc_configuration, pgexporter_test_setup, pgexporter_test_teardown);

   tcase_add_test(tc_configuration, test_configuration_accept_time);
   tcase_add_test(tc_configuration, test_configuration_reject_invalid_time);
   tcase_add_test(tc_configuration, test_configuration_get_returns_set_values);
   tcase_add_test(tc_configuration, test_configuration_time_format_output);

   suite_add_tcase(s, tc_configuration);

   return s;
}
