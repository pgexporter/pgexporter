/*
 * Copyright (C) 2025 The pgexporter community
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
#include <http.h>
#include <json.h>
#include <management.h>
#include <network.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <tssuite.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void validate_metrics_response(char* response_body, const char* metric_pattern);

// Test HTTP metrics endpoint
START_TEST(test_http_metrics)
{
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   int ret;

   config = (struct configuration*)shmem;

   ret = pgexporter_http_create("localhost", config->metrics, false, &connection);
   ck_assert_msg(ret == 0, "Failed to connect to HTTP endpoint localhost:%d", config->metrics);

   ret = pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/metrics", &request);
   ck_assert_msg(ret == 0, "Failed to create HTTP request");

   ret = pgexporter_http_invoke(connection, request, &response);
   ck_assert_msg(ret == 0, "Failed to execute HTTP GET /metrics");

   ck_assert_msg(response->payload.data != NULL, "HTTP response body is NULL");

   response_body = strdup((char*)response->payload.data);
   ck_assert_msg(response_body != NULL, "Failed to duplicate response body");
   ck_assert_msg(strlen(response_body) > 0, "Response body is empty");

   validate_metrics_response(response_body, "pgexporter_state 1");

   free(response_body);
   pgexporter_http_response_destroy(response);
   pgexporter_http_request_destroy(request);
   pgexporter_http_destroy(connection);
}
END_TEST

// Test HTTP bridge endpoint
START_TEST(test_http_bridge_endpoint)
{
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   int ret;

   config = (struct configuration*)shmem;

   ck_assert_msg(config->bridge > 0, "Bridge port not configured");

   ret = pgexporter_http_create("localhost", config->bridge, false, &connection);
   ck_assert_msg(ret == 0, "Failed to connect to bridge endpoint localhost:%d", config->bridge);

   ret = pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/metrics", &request);
   ck_assert_msg(ret == 0, "Failed to create HTTP request");

   ret = pgexporter_http_invoke(connection, request, &response);
   ck_assert_msg(ret == 0, "Failed to execute HTTP GET /metrics");

   ck_assert_msg(response->payload.data != NULL, "HTTP response body is NULL");

   response_body = strdup((char*)response->payload.data);
   ck_assert_msg(response_body != NULL, "Failed to duplicate response body");
   ck_assert_msg(strlen(response_body) > 0, "Response body is empty");

   validate_metrics_response(response_body, "pgexporter_state{endpoint=");

   free(response_body);
   pgexporter_http_response_destroy(response);
   pgexporter_http_request_destroy(request);
   pgexporter_http_destroy(connection);
}
END_TEST

// Test extension detection
START_TEST(test_http_extension_detection)
{
   struct configuration* config;
   bool found_pg_stat_statements = false;

   config = (struct configuration*)shmem;

   ck_assert_msg(config->number_of_servers > 0, "No servers configured");

   for (int i = 0; i < config->servers[0].number_of_extensions; i++)
   {
      if (strcmp(config->servers[0].extensions[i].name, "pg_stat_statements") == 0)
      {
         found_pg_stat_statements = true;
         break;
      }
   }

   ck_assert_msg(found_pg_stat_statements, "pg_stat_statements extension not found");
}
END_TEST

// Test CLI shutdown command (must be last test)
START_TEST(test_http_shutdown)
{
   int socket = -1;
   int ret;

   socket = pgexporter_tsclient_get_connection();
   ck_assert_msg(pgexporter_socket_isvalid(socket), "Failed to get connection to pgexporter");

   ret = pgexporter_management_request_shutdown(NULL, socket, MANAGEMENT_COMPRESSION_NONE,
                                                MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON);
   ck_assert_msg(ret == 0, "Failed to send shutdown request");

   ret = pgexporter_tsclient_check_outcome(socket);
   ck_assert_msg(ret == 0, "Shutdown command returned unsuccessful outcome");

   pgexporter_disconnect(socket);
}
END_TEST

Suite*
pgexporter_test_http_suite()
{
   Suite* s;
   TCase* tc_http;

   s = suite_create("pgexporter_test_http");

   tc_http = tcase_create("HTTP");

   tcase_set_timeout(tc_http, 60);
   tcase_add_checked_fixture(tc_http, pgexporter_test_setup, pgexporter_test_teardown);
   tcase_add_test(tc_http, test_http_metrics);
   tcase_add_test(tc_http, test_http_bridge_endpoint);
   tcase_add_test(tc_http, test_http_extension_detection);
   tcase_add_test(tc_http, test_http_shutdown);
   suite_add_tcase(s, tc_http);

   return s;
}

static void
validate_metrics_response(char* response_body, const char* metric_pattern)
{
   char* line = NULL;
   char* saveptr = NULL;
   bool found_core_metric = false;
   bool found_version_metric = false;
   int postgresql_version = 0;
   char* body_copy = strdup(response_body);

   ck_assert_msg(body_copy != NULL, "Failed to copy response body for parsing");

   line = strtok_r(body_copy, "\n", &saveptr);
   while (line != NULL)
   {
      if (pgexporter_starts_with(line, (char*)metric_pattern))
      {
         found_core_metric = true;
      }

      if (pgexporter_starts_with(line, "pgexporter_postgresql_version"))
      {
         char* version_start = strstr(line, "version=\"");
         if (version_start != NULL)
         {
            version_start += 9;
            char* version_end = strchr(version_start, '"');
            if (version_end != NULL)
            {
               *version_end = '\0';
               postgresql_version = atoi(version_start);
               found_version_metric = true;
            }
         }
      }

      line = strtok_r(NULL, "\n", &saveptr);
   }

   free(body_copy);

   ck_assert_msg(found_core_metric, "Failed to find core metric matching pattern: %s", metric_pattern);
   ck_assert_msg(found_version_metric, "Failed to find PostgreSQL version metric");
   ck_assert_msg(postgresql_version == 17, "Expected PostgreSQL version 17, got %d", postgresql_version);
}
