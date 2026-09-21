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

/*
 * Exercises pgexporter_history_http() end-to-end (path routing, query-param
 * parsing, SQLite lookup, JSON encoding) without a live daemon: each test
 * binds an ephemeral loopback TCP listener, forks a child that accepts the
 * one connection and calls pgexporter_history_http(NULL, fd) directly on it,
 * while the parent drives the request with the same HTTP client library
 * test_http.c uses (pgexporter_http_create/pgexporter_http_invoke). This
 * mirrors how main.c invokes the same function from a forked per-connection
 * worker, just without main.c's own accept loop around it, and lets us reuse
 * the client's request building/response parsing instead of hand-rolling it.
 */

#include <pgexporter.h>
#include <history.h>
#include <http.h>
#include <memory.h>
#include <shmem.h>
#include <utils.h>

#include <mctf.h>
#include <tscommon.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char db_path[MAX_PATH];

static void
unlink_db(const char* path)
{
   char sibling[MAX_PATH];

   if (path == NULL || path[0] == '\0')
   {
      return;
   }

   unlink(path);

   pgexporter_snprintf(sibling, MAX_PATH, "%s-wal", path);
   unlink(sibling);
   pgexporter_snprintf(sibling, MAX_PATH, "%s-shm", path);
   unlink(sibling);
   pgexporter_snprintf(sibling, MAX_PATH, "%s-journal", path);
   unlink(sibling);
}

static void
make_record(struct history_record* r, time_t ts, const char* server,
            const char* metric, const char* labels, double value)
{
   memset(r, 0, sizeof(*r));
   r->ts = ts;
   if (server)
   {
      pgexporter_snprintf(r->server, MISC_LENGTH, "%s", server);
   }
   if (metric)
   {
      pgexporter_snprintf(r->metric, PROMETHEUS_LENGTH, "%s", metric);
   }
   r->labels = (char*)labels;
   r->value = value;
}

/* Bind an ephemeral loopback TCP listener and report the port the kernel picked. */
static int
create_loopback_listener(int* out_port)
{
   int fd;
   struct sockaddr_in addr;
   socklen_t addr_len = sizeof(addr);

   fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0)
   {
      return -1;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   addr.sin_port = 0;

   if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0)
   {
      close(fd);
      return -1;
   }

   if (getsockname(fd, (struct sockaddr*)&addr, &addr_len) != 0)
   {
      close(fd);
      return -1;
   }

   *out_port = ntohs(addr.sin_port);
   return fd;
}

/* Fork a child that accepts the single incoming connection and serves it via
 * pgexporter_history_http(NULL, fd), while the parent issues `path` as a GET
 * through the real HTTP client and returns the response (caller destroys via
 * pgexporter_http_response_destroy). Returns PGEXPORTER_HTTP_STATUS_OK/ERROR. */
static int
do_http_get(const char* path, struct http_response** out_response)
{
   int listen_fd = -1;
   int port = 0;
   pid_t pid;
   int ret = PGEXPORTER_HTTP_STATUS_ERROR;
   struct http* connection = NULL;
   struct http_request* request = NULL;

   listen_fd = create_loopback_listener(&port);
   if (listen_fd < 0)
   {
      return PGEXPORTER_HTTP_STATUS_ERROR;
   }

   pid = fork();
   if (pid < 0)
   {
      close(listen_fd);
      return PGEXPORTER_HTTP_STATUS_ERROR;
   }

   if (pid == 0)
   {
      int client_fd = accept(listen_fd, NULL, NULL);

      close(listen_fd);
      if (client_fd < 0)
      {
         _exit(1);
      }
      pgexporter_history_http(NULL, client_fd);
      _exit(1);
   }

   close(listen_fd);

   if (pgexporter_http_create("127.0.0.1", port, false, NULL, NULL, NULL, &connection) != PGEXPORTER_HTTP_STATUS_OK)
   {
      waitpid(pid, NULL, 0);
      return PGEXPORTER_HTTP_STATUS_ERROR;
   }

   if (pgexporter_http_request_create(PGEXPORTER_HTTP_GET, (char*)path, &request) != PGEXPORTER_HTTP_STATUS_OK)
   {
      pgexporter_http_destroy(connection);
      waitpid(pid, NULL, 0);
      return PGEXPORTER_HTTP_STATUS_ERROR;
   }

   ret = pgexporter_http_invoke(connection, request, out_response);

   pgexporter_http_request_destroy(request);
   pgexporter_http_destroy(connection);
   waitpid(pid, NULL, 0);

   return ret;
}

MCTF_TEST_SETUP(history_http)
{
   struct configuration* config;

   pgexporter_test_config_save();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;
   config->history_backend = HISTORY_BACKEND_SQLITE;

   pgexporter_snprintf(db_path, MAX_PATH, "/tmp/pgexporter-test/history-http-%d.db", (int)getpid());
   unlink_db(db_path);
   pgexporter_snprintf(config->history_path, MAX_PATH, "%s", db_path);

   pgexporter_history_create();
}

MCTF_TEST_TEARDOWN(history_http)
{
   pgexporter_history_shutdown();
   unlink_db(db_path);
   db_path[0] = '\0';
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}

MCTF_TEST(test_history_http_valid_query_returns_200_json)
{
   struct history_record in[2];
   struct http_response* response = NULL;
   char path[256];
   char* body = NULL;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   make_record(&in[0], now, "srv1", "pg_up", "server=\"srv1\"", 1.0);
   make_record(&in[1], now, "srv1", "pg_up", "server=\"srv1\"", 1.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write_batch failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   pgexporter_snprintf(path, sizeof(path), "/history/pg_up?timestamp=%ld&duration=10", (long)now);

   MCTF_ASSERT_INT_EQ(do_http_get(path, &response), PGEXPORTER_HTTP_STATUS_OK, cleanup,
                      "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 200, cleanup, "expected 200, got %d", response->status_code);

   body = (char*)response->payload.data;
   MCTF_ASSERT_PTR_NONNULL(body, cleanup, "expected a response body");
   MCTF_ASSERT_PTR_NONNULL(strstr(body, "\"metric\": \"pg_up\""), cleanup,
                           "expected metric field in body: %s", body);
   MCTF_ASSERT_PTR_NONNULL(strstr(body, "\"server\": \"srv1\""), cleanup,
                           "expected server field in body: %s", body);
   MCTF_ASSERT_PTR_NONNULL(strchr(body, '['), cleanup, "expected a JSON array body");

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}

MCTF_TEST(test_history_http_no_matching_rows_returns_empty_array)
{
   struct http_response* response = NULL;
   char* body = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   MCTF_ASSERT_INT_EQ(do_http_get("/history/does_not_exist", &response), PGEXPORTER_HTTP_STATUS_OK, cleanup,
                      "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 200, cleanup, "expected 200, got %d", response->status_code);

   body = (char*)response->payload.data;
   MCTF_ASSERT_PTR_NONNULL(body, cleanup, "expected a response body");
   MCTF_ASSERT_PTR_NONNULL(strstr(body, "[]"), cleanup, "expected an empty JSON array, got: %s", body);
   MCTF_ASSERT(strstr(body, "\"metric\"") == NULL, cleanup, "expected no records for an unknown metric");

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_http_missing_metric_returns_400)
{
   struct http_response* response = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   MCTF_ASSERT_INT_EQ(do_http_get("/history/", &response), PGEXPORTER_HTTP_STATUS_OK, cleanup,
                      "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 400, cleanup, "expected 400, got %d", response->status_code);

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}

MCTF_TEST(test_history_http_unknown_path_returns_404)
{
   struct http_response* response = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   MCTF_ASSERT_INT_EQ(do_http_get("/not-history", &response), PGEXPORTER_HTTP_STATUS_OK, cleanup,
                      "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 404, cleanup, "expected 404, got %d", response->status_code);

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_http_non_numeric_timestamp_returns_400)
{
   struct http_response* response = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   MCTF_ASSERT_INT_EQ(do_http_get("/history/pg_up?timestamp=not-a-number", &response), PGEXPORTER_HTTP_STATUS_OK,
                      cleanup, "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 400, cleanup, "expected 400, got %d", response->status_code);

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_http_non_numeric_duration_returns_400)
{
   struct http_response* response = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   MCTF_ASSERT_INT_EQ(do_http_get("/history/pg_up?duration=not-a-number", &response), PGEXPORTER_HTTP_STATUS_OK,
                      cleanup, "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 400, cleanup, "expected 400, got %d", response->status_code);

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}

MCTF_TEST(test_history_http_zero_duration_zero_width_window)
{
   struct history_record in[2];
   struct http_response* response = NULL;
   char path[256];
   char* body = NULL;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   /* One row exactly at `now`, one a second before it: with duration=0 the
    * query window collapses to [now, now], so only the exact-match row should
    * come back. */
   make_record(&in[0], now, "s", "zero_window_metric", "", 1.0);
   make_record(&in[1], now - 1, "s", "zero_window_metric", "", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write_batch failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown before request failed");

   pgexporter_snprintf(path, sizeof(path), "/history/zero_window_metric?timestamp=%ld&duration=0", (long)now);

   MCTF_ASSERT_INT_EQ(do_http_get(path, &response), PGEXPORTER_HTTP_STATUS_OK, cleanup,
                      "HTTP request failed at the transport layer");
   MCTF_ASSERT_PTR_NONNULL(response, cleanup, "no response object returned");
   MCTF_ASSERT_INT_EQ(response->status_code, 200, cleanup, "expected 200, got %d", response->status_code);

   body = (char*)response->payload.data;
   MCTF_ASSERT_PTR_NONNULL(body, cleanup, "expected a response body");
   MCTF_ASSERT_PTR_NONNULL(strstr(body, "\"value\": 1.000000"), cleanup,
                           "expected only the exact-timestamp row in a zero-width window: %s", body);
   MCTF_ASSERT(strstr(body, "\"value\": 2.000000") == NULL, cleanup,
               "row outside a zero-width window should not be returned: %s", body);

cleanup:
   pgexporter_http_response_destroy(response);
   MCTF_FINISH();
}
