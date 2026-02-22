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
#include <http.h>
#include <json.h>
#include <management.h>
#include <network.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <mctf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int validate_metrics_response(char* response_body, const char* metric_pattern);

/* Must run last: shuts down the daemon. Defined first so it registers first and runs last. */
MCTF_TEST(test_http_shutdown)
{
   int socket = -1;
   int ret;

   socket = pgexporter_tsclient_get_connection();
   MCTF_ASSERT(pgexporter_socket_isvalid(socket), cleanup, "Failed to get connection to pgexporter");

   ret = pgexporter_management_request_shutdown(NULL, socket, MANAGEMENT_COMPRESSION_NONE,
                                                MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to send shutdown request");

   ret = pgexporter_tsclient_check_outcome(socket);
   MCTF_ASSERT(ret == 0, cleanup, "Shutdown command returned unsuccessful outcome");

   pgexporter_disconnect(socket);
   socket = -1;
cleanup:
   if (socket >= 0)
   {
      pgexporter_disconnect(socket);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_http_metrics)
{
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   int ret;

   pgexporter_test_setup();

   config = (struct configuration*)shmem;

   ret = pgexporter_http_create("localhost", config->metrics, false, &connection);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to connect to HTTP endpoint localhost:%d", config->metrics);

   ret = pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/metrics", &request);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to create HTTP request");

   ret = pgexporter_http_invoke(connection, request, &response);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to execute HTTP GET /metrics");

   MCTF_ASSERT_PTR_NONNULL(response->payload.data, cleanup, "HTTP response body is NULL");

   response_body = strdup((char*)response->payload.data);
   MCTF_ASSERT_PTR_NONNULL(response_body, cleanup, "Failed to duplicate response body");
   MCTF_ASSERT(strlen(response_body) > 0, cleanup, "Response body is empty");

   ret = validate_metrics_response(response_body, "pgexporter_state 1");
   MCTF_ASSERT(ret == 0, cleanup, "HTTP metrics response validation failed");

   free(response_body);
   response_body = NULL;
   pgexporter_http_response_destroy(response);
   response = NULL;
   pgexporter_http_request_destroy(request);
   request = NULL;
   pgexporter_http_destroy(connection);
   connection = NULL;
cleanup:
   if (response_body)
      free(response_body);
   if (response)
      pgexporter_http_response_destroy(response);
   if (request)
      pgexporter_http_request_destroy(request);
   if (connection)
      pgexporter_http_destroy(connection);
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_http_bridge_endpoint)
{
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   int ret;

   pgexporter_test_setup();

   config = (struct configuration*)shmem;

   MCTF_ASSERT(config->bridge > 0, cleanup, "Bridge port not configured");

   ret = pgexporter_http_create("localhost", config->bridge, false, &connection);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to connect to bridge endpoint localhost:%d", config->bridge);

   ret = pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/metrics", &request);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to create HTTP request");

   ret = pgexporter_http_invoke(connection, request, &response);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to execute HTTP GET /metrics");

   MCTF_ASSERT_PTR_NONNULL(response->payload.data, cleanup, "HTTP response body is NULL");

   response_body = strdup((char*)response->payload.data);
   MCTF_ASSERT_PTR_NONNULL(response_body, cleanup, "Failed to duplicate response body");
   MCTF_ASSERT(strlen(response_body) > 0, cleanup, "Response body is empty");

   ret = validate_metrics_response(response_body, "pgexporter_state{endpoint=");
   MCTF_ASSERT(ret == 0, cleanup, "HTTP bridge metrics response validation failed");

   free(response_body);
   response_body = NULL;
   pgexporter_http_response_destroy(response);
   response = NULL;
   pgexporter_http_request_destroy(request);
   request = NULL;
   pgexporter_http_destroy(connection);
   connection = NULL;
cleanup:
   if (response_body)
      free(response_body);
   if (response)
      pgexporter_http_response_destroy(response);
   if (request)
      pgexporter_http_request_destroy(request);
   if (connection)
      pgexporter_http_destroy(connection);
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_http_extension_detection)
{
   struct configuration* config;
   bool found_pg_stat_statements = false;

   pgexporter_test_setup();

   config = (struct configuration*)shmem;

   MCTF_ASSERT(config->number_of_servers > 0, cleanup, "No servers configured");

   for (int i = 0; i < config->servers[0].number_of_extensions; i++)
   {
      if (strcmp(config->servers[0].extensions[i].name, "pg_stat_statements") == 0)
      {
         found_pg_stat_statements = true;
         break;
      }
   }

   if (!found_pg_stat_statements)
   {
      MCTF_SKIP("pg_stat_statements extension not found (not installed or not yet discovered)");
   }
cleanup:
   pgexporter_test_teardown();
   MCTF_FINISH();
}

static int
validate_metrics_response(char* response_body, const char* metric_pattern)
{
   char* line = NULL;
   char* saveptr = NULL;
   bool found_core_metric = false;
   bool found_version_metric = false;
   int postgresql_version = 0;
   char* body_copy = strdup(response_body);

   if (body_copy == NULL)
   {
      if (mctf_errmsg)
         free(mctf_errmsg);
      mctf_errmsg = strdup("Failed to copy response body for parsing");
      return -1;
   }

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

   if (!found_core_metric)
   {
      if (mctf_errmsg)
         free(mctf_errmsg);
      mctf_errmsg = malloc(128);
      if (mctf_errmsg)
         snprintf(mctf_errmsg, 128, "Failed to find core metric matching pattern: %s", metric_pattern);
      return -1;
   }
   if (!found_version_metric)
   {
      if (mctf_errmsg)
         free(mctf_errmsg);
      mctf_errmsg = strdup("Failed to find PostgreSQL version metric");
      return -1;
   }
   if (postgresql_version != 17)
   {
      if (mctf_errmsg)
         free(mctf_errmsg);
      mctf_errmsg = malloc(64);
      if (mctf_errmsg)
         snprintf(mctf_errmsg, 64, "Expected PostgreSQL version 17, got %d", postgresql_version);
      return -1;
   }
   return 0;
}
