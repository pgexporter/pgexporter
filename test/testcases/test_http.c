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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int validate_metrics_response(char* response_body, const char* metric_pattern);

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

MCTF_TEST(test_http_metrics_no_empty_labels)
{
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   char* body_copy = NULL;
   char* line = NULL;
   char* saveptr = NULL;
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

   body_copy = strdup(response_body);
   line = strtok_r(body_copy, "\n", &saveptr);
   while (line != NULL)
   {
      if (line[0] != '#' && strstr(line, "pgexporter_") != NULL)
      {
         MCTF_ASSERT(strstr(line, "=\"\"") == NULL, cleanup,
                     "Empty label value found: %s", line);
      }
      line = strtok_r(NULL, "\n", &saveptr);
   }

   free(body_copy);
   body_copy = NULL;
   free(response_body);
   response_body = NULL;
   pgexporter_http_response_destroy(response);
   response = NULL;
   pgexporter_http_request_destroy(request);
   request = NULL;
   pgexporter_http_destroy(connection);
   connection = NULL;
cleanup:
   if (body_copy)
      free(body_copy);
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

MCTF_TEST(test_http_metrics_valid_values)
{
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   char* body_copy = NULL;
   char* line = NULL;
   char* saveptr = NULL;
   char* brace_close = NULL;
   char* value_str = NULL;
   char* endptr = NULL;
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

   body_copy = strdup(response_body);
   line = strtok_r(body_copy, "\n", &saveptr);
   while (line != NULL)
   {
      if (line[0] != '#' && strstr(line, "pgexporter_") != NULL)
      {
         brace_close = strrchr(line, '}');
         if (brace_close != NULL)
         {
            value_str = brace_close + 1;
            while (*value_str == ' ')
            {
               value_str++;
            }
            if (*value_str != '\0')
            {
               endptr = NULL;
               strtod(value_str, &endptr);
               MCTF_ASSERT(endptr != value_str && (*endptr == '\0' || *endptr == ' ' || *endptr == '\n'), cleanup,
                           "Non-numeric metric value: '%s'", value_str);
            }
         }
      }
      line = strtok_r(NULL, "\n", &saveptr);
   }

   free(body_copy);
   body_copy = NULL;
   free(response_body);
   response_body = NULL;
   pgexporter_http_response_destroy(response);
   response = NULL;
   pgexporter_http_request_destroy(request);
   request = NULL;
   pgexporter_http_destroy(connection);
   connection = NULL;
cleanup:
   if (body_copy)
      free(body_copy);
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

struct echo_server
{
   int socket_fd;
   int port;
   pthread_t thread;
   bool running;
   char* response;
   size_t response_len;
};

struct write_cb_context
{
   char data[64];
   size_t data_size;
};

static struct echo_server* test_server = NULL;

static size_t
collect_write_cb(void* buffer, size_t size, void* userdata)
{
   struct write_cb_context* context = (struct write_cb_context*)userdata;

   if (context == NULL || context->data_size + size > sizeof(context->data))
   {
      return 0;
   }

   memcpy(context->data + context->data_size, buffer, size);
   context->data_size += size;

   return size;
}

static void*
echo_server_thread(void* arg)
{
   struct echo_server* server = (struct echo_server*)arg;

   while (server->running)
   {
      fd_set read_fds;
      struct timeval timeout;

      FD_ZERO(&read_fds);
      FD_SET(server->socket_fd, &read_fds);

      timeout.tv_sec = 1;
      timeout.tv_usec = 0;

      int result = select(server->socket_fd + 1, &read_fds, NULL, NULL, &timeout);

      if (result <= 0)
      {
         continue;
      }

      if (FD_ISSET(server->socket_fd, &read_fds))
      {
         struct sockaddr_in client_addr;
         socklen_t client_len = sizeof(client_addr);
         int client_fd = accept(server->socket_fd, (struct sockaddr*)&client_addr, &client_len);

         if (client_fd < 0)
         {
            continue;
         }

         char buffer[4096];
         ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

         if (bytes_read > 0)
         {
            send(client_fd, server->response, server->response_len, 0);
         }

         close(client_fd);
      }
   }

   return NULL;
}

static int
start_echo_server(int port, char* response)
{
   struct sockaddr_in addr;

   test_server = malloc(sizeof(struct echo_server));
   if (test_server == NULL)
   {
      return 1;
   }

   test_server->port = port;
   test_server->running = false;
   test_server->response = response;
   test_server->response_len = strlen(response);

   test_server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (test_server->socket_fd < 0)
   {
      free(test_server);
      test_server = NULL;
      return 1;
   }

   int opt = 1;
   setsockopt(test_server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(port);

   if (bind(test_server->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
   {
      close(test_server->socket_fd);
      free(test_server);
      test_server = NULL;
      return 1;
   }

   if (listen(test_server->socket_fd, 5) < 0)
   {
      close(test_server->socket_fd);
      free(test_server);
      test_server = NULL;
      return 1;
   }

   test_server->running = true;

   if (pthread_create(&test_server->thread, NULL, echo_server_thread, test_server) != 0)
   {
      test_server->running = false;
      close(test_server->socket_fd);
      free(test_server);
      test_server = NULL;
      return 1;
   }

   usleep(100000);

   return 0;
}

static void
stop_echo_server(void)
{
   if (test_server == NULL)
   {
      return;
   }

   test_server->running = false;
   usleep(1100000);

   if (test_server->socket_fd >= 0)
   {
      close(test_server->socket_fd);
   }

   pthread_detach(test_server->thread);
   free(test_server);
   test_server = NULL;
}

MCTF_TEST(test_http_write_cb_content_length)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct write_cb_context write_context = {0};

   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "Hello, World!";

   MCTF_ASSERT(start_echo_server(9999, response_text) == 0, cleanup, "failed to start echo server");

   response = calloc(1, sizeof(struct http_response));
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "failed to allocate response");

   response->write_cb = collect_write_cb;
   response->write_userdata = &write_context;

   MCTF_ASSERT(pgexporter_http_create("localhost", 9999, false, &connection) == 0, cleanup, "failed to establish connection");
   MCTF_ASSERT(pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/test", &request) == 0, cleanup, "failed to create request");

   status = pgexporter_http_invoke(connection, request, &response);
   MCTF_ASSERT(status == PGEXPORTER_HTTP_STATUS_OK, cleanup, "HTTP request failed");
   MCTF_ASSERT(write_context.data_size == 13, cleanup, "write_cb should receive content-length body");
   MCTF_ASSERT(strcmp(write_context.data, "Hello, World!") == 0, cleanup, "write_cb content-length body mismatch");
   MCTF_ASSERT(response->payload.data_size == 0, cleanup, "write_cb should prevent buffering in payload");

cleanup:
   pgexporter_http_request_destroy(request);
   pgexporter_http_response_destroy(response);
   pgexporter_http_destroy(connection);
   stop_echo_server();
   MCTF_FINISH();
}

MCTF_TEST(test_http_write_cb_chunked)
{
   int status;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct write_cb_context write_context = {0};

   char* response_text =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\n"
      "Hello\r\n"
      "8\r\n"
      ", World!\r\n"
      "0\r\n"
      "\r\n";

   MCTF_ASSERT(start_echo_server(9999, response_text) == 0, cleanup, "failed to start echo server");

   response = calloc(1, sizeof(struct http_response));
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "failed to allocate response");

   response->write_cb = collect_write_cb;
   response->write_userdata = &write_context;

   MCTF_ASSERT(pgexporter_http_create("localhost", 9999, false, &connection) == 0, cleanup, "failed to establish connection");
   MCTF_ASSERT(pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/test", &request) == 0, cleanup, "failed to create request");

   status = pgexporter_http_invoke(connection, request, &response);
   MCTF_ASSERT(status == PGEXPORTER_HTTP_STATUS_OK, cleanup, "HTTP chunked request failed");
   MCTF_ASSERT(write_context.data_size == 13, cleanup, "write_cb should receive chunked body");
   MCTF_ASSERT(strcmp(write_context.data, "Hello, World!") == 0, cleanup, "write_cb chunked body mismatch");
   MCTF_ASSERT(response->payload.data_size == 0, cleanup, "write_cb should prevent buffering for chunked response");

cleanup:
   pgexporter_http_request_destroy(request);
   pgexporter_http_response_destroy(response);
   pgexporter_http_destroy(connection);
   stop_echo_server();
   MCTF_FINISH();
}

/* Must run last: shuts down the daemon. Defined last so it registers last and runs last. */
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
