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
#include <bridge_history.h>
#include <configuration.h>
#include <history.h>
#include <local_history.h>
#include <memory.h>
#include <prometheus_client.h>
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

static char local_path[MAX_PATH];
static char bridge_path[MAX_PATH];

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
   pgexporter_snprintf(r->server, MISC_LENGTH, "%s", server);
   pgexporter_snprintf(r->metric, PROMETHEUS_LENGTH, "%s", metric);
   r->labels = (char*)labels;
   r->value = value;
}

/* Bind a loopback listener and fork a child that answers one GET with body. */
static pid_t
serve_metrics_once(const char* body, int* out_port)
{
   int fd;
   struct sockaddr_in addr;
   socklen_t addr_len = sizeof(addr);
   pid_t pid;

   fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0)
   {
      return -1;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0 ||
       getsockname(fd, (struct sockaddr*)&addr, &addr_len) != 0)
   {
      close(fd);
      return -1;
   }

   *out_port = ntohs(addr.sin_port);

   pid = fork();
   if (pid != 0)
   {
      close(fd);
      return pid;
   }

   {
      char request[4096];
      char header[256];
      ssize_t n;
      size_t total = 0;
      int client_fd = accept(fd, NULL, NULL);

      close(fd);
      if (client_fd < 0)
      {
         _exit(1);
      }

      while (total < sizeof(request) - 1 && (n = read(client_fd, request + total, sizeof(request) - 1 - total)) > 0)
      {
         total += (size_t)n;
         request[total] = '\0';
         if (strstr(request, "\r\n\r\n") != NULL)
         {
            break;
         }
      }

      pgexporter_snprintf(header, sizeof(header),
                          "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                          strlen(body));
      if (write(client_fd, header, strlen(header)) < 0 || write(client_fd, body, strlen(body)) < 0)
      {
         _exit(1);
      }

      close(client_fd);
      _exit(0);
   }
}

/* Scrape the given bodies as endpoints 0..n-1 through the real bridge client. */
static int
scrape_endpoints(const char** bodies, int n, struct prometheus_bridge** bridge, int* ports)
{
   struct configuration* config = (struct configuration*)shmem;
   pid_t pids[NUMBER_OF_ENDPOINTS];
   int ret = 0;

   if (pgexporter_prometheus_client_create_bridge(bridge))
   {
      return 1;
   }

   for (int i = 0; i < n; i++)
   {
      pids[i] = serve_metrics_once(bodies[i], &ports[i]);
      if (pids[i] < 0)
      {
         return 1;
      }

      pgexporter_snprintf(config->endpoints[i].host, MISC_LENGTH, "127.0.0.1");
      config->endpoints[i].port = ports[i];
      config->endpoints[i].tls = false;
   }
   config->number_of_endpoints = n;

   for (int i = 0; i < n; i++)
   {
      if (pgexporter_prometheus_client_get(i, *bridge))
      {
         ret = 1;
      }
      waitpid(pids[i], NULL, 0);
   }

   return ret;
}

static bool
has_label(struct history_record* records, int count, const char* needle)
{
   for (int i = 0; i < count; i++)
   {
      if (records[i].labels != NULL && strstr(records[i].labels, needle) != NULL)
      {
         return true;
      }
   }

   return false;
}

MCTF_TEST_SETUP(bridge_history)
{
   struct configuration* config;

   pgexporter_test_config_save();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;
   config->history_backend = HISTORY_BACKEND_SQLITE;
   config->bridge_history_backend = HISTORY_BACKEND_SQLITE;

   pgexporter_snprintf(local_path, MAX_PATH, "/tmp/pgexporter-test/bridge-local-%d.db", (int)getpid());
   pgexporter_snprintf(bridge_path, MAX_PATH, "/tmp/pgexporter-test/bridge-history-%d.db", (int)getpid());
   unlink_db(local_path);
   unlink_db(bridge_path);
   pgexporter_snprintf(config->history_path, MAX_PATH, "%s", local_path);
   pgexporter_snprintf(config->bridge_history_path, MAX_PATH, "%s", bridge_path);

   pgexporter_history_create(pgexporter_local_history_store());
   pgexporter_history_create(pgexporter_bridge_history_store());
}

MCTF_TEST_TEARDOWN(bridge_history)
{
   pgexporter_history_shutdown();
   unlink_db(local_path);
   unlink_db(bridge_path);
   local_path[0] = '\0';
   bridge_path[0] = '\0';
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}

MCTF_TEST(test_bridge_history_store_keeps_endpoint)
{
   const char* bodies[2] = {
      "# HELP pg_database_size Size of the database\n"
      "# TYPE pg_database_size gauge\n"
      "pg_database_size{server=\"primary\",database=\"postgres\"} 8000000\n"
      "pg_database_size{server=\"replica\",database=\"postgres\"} 7900000\n"
      "\n"
      "# HELP pgexporter_state The state of pgexporter\n"
      "# TYPE pgexporter_state gauge\n"
      "pgexporter_state 1\n",
      "# HELP pg_database_size Size of the database\n"
      "# TYPE pg_database_size gauge\n"
      "pg_database_size{server=\"primary\",database=\"postgres\"} 5100000\n"
      "\n"
      "# HELP pgexporter_state The state of pgexporter\n"
      "# TYPE pgexporter_state gauge\n"
      "pgexporter_state 1\n"};
   struct prometheus_bridge* bridge = NULL;
   struct history_record* out = NULL;
   int count = 0;
   int ports[2] = {0};
   char needle[128];
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(scrape_endpoints(bodies, 2, &bridge, ports), 0, cleanup, "scrape failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_bridge_history_store()), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_bridge_history_store_metrics(bridge), 0, cleanup, "store failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pg_database_size", now - 10, now + 10, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "expected one row per endpoint and server, got %d", count);

   pgexporter_snprintf(needle, sizeof(needle), "endpoint=\"127.0.0.1:%d\",server=\"primary\"", ports[0]);
   MCTF_ASSERT(has_label(out, count, needle), cleanup, "missing first endpoint primary: %s", needle);
   pgexporter_snprintf(needle, sizeof(needle), "endpoint=\"127.0.0.1:%d\",server=\"primary\"", ports[1]);
   MCTF_ASSERT(has_label(out, count, needle), cleanup, "missing second endpoint primary: %s", needle);

   for (int i = 0; i < count; i++)
   {
      MCTF_ASSERT(out[i].server[0] != '\0', cleanup, "server column should come from the server label");
   }

   pgexporter_history_records_free(out, count);
   out = NULL;
   count = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pgexporter_state", now - 10, now + 10, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected one unlabeled series per endpoint, got %d", count);
   MCTF_ASSERT_STR_EQ(out[0].server, "", cleanup, "server should be empty without a server label");

cleanup:
   pgexporter_history_records_free(out, count);
   pgexporter_prometheus_client_destroy_bridge(bridge);
   MCTF_FINISH();
}

MCTF_TEST(test_bridge_history_escaped_label_round_trips)
{
   const char* bodies[1] = {
      "# HELP pg_query_calls Calls\n"
      "# TYPE pg_query_calls counter\n"
      "pg_query_calls{server=\"primary\",query=\"SELECT \\\"a\\\\b\\\"\"} 3\n"};
   struct prometheus_bridge* bridge = NULL;
   struct history_record* out = NULL;
   int count = 0;
   int ports[1] = {0};
   char value[64];
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(scrape_endpoints(bodies, 1, &bridge, ports), 0, cleanup, "scrape failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_bridge_history_store()), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_bridge_history_store_metrics(bridge), 0, cleanup, "store failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pg_query_calls", now - 10, now + 10, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "expected 1 row, got %d", count);
   MCTF_ASSERT(pgexporter_history_label_find(out[0].labels, "query", value, sizeof(value)), cleanup,
               "query label should parse: %s", out[0].labels);
   MCTF_ASSERT_STR_EQ(value, "SELECT \\\"a\\\\b\\\"", cleanup, "escapes should survive the bridge: %s", out[0].labels);
   MCTF_ASSERT(out[0].value == 3.0, cleanup, "value mismatch");

cleanup:
   pgexporter_history_records_free(out, count);
   pgexporter_prometheus_client_destroy_bridge(bridge);
   MCTF_FINISH();
}

MCTF_TEST(test_bridge_history_stores_are_isolated)
{
   struct history_record in[1];
   struct history_record* out = NULL;
   int count = -1;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_local_history_store()), 0, cleanup, "local init failed");
   make_record(&in[0], now, "primary", "local_only", "server=\"primary\"", 1.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 1), 0, cleanup, "local write failed");
   pgexporter_history_shutdown();

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_bridge_history_store()), 0, cleanup, "bridge init failed");
   make_record(&in[0], now, "primary", "bridge_only", "endpoint=\"a:1\",server=\"primary\"", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 1), 0, cleanup, "bridge write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("local_only", 0, now + 10, NULL, &count), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "bridge store should not see local rows, got %d", count);
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("bridge_only", 0, now + 10, NULL, &count), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "bridge store should see its own row, got %d", count);
   pgexporter_history_shutdown();

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_local_history_store()), 0, cleanup, "local reinit failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("bridge_only", 0, now + 10, NULL, &count), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "local store should not see bridge rows, got %d", count);
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("local_only", 0, now + 10, &out, &count), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "local store should see its own row, got %d", count);

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_bridge_history_prune_uses_own_retention)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[2];
   int count = -1;
   time_t now = time(NULL);

   config->history_retention = PGEXPORTER_TIME_DISABLED;
   config->bridge_history_retention = PGEXPORTER_TIME_SEC(3600);

   make_record(&in[0], now - 7200, "s", "m", "", 1.0);
   make_record(&in[1], now, "s", "m", "", 2.0);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_local_history_store()), 0, cleanup, "local init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "local write failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(pgexporter_local_history_store()), 0, cleanup, "local prune failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "local retention is disabled, expected 2 rows, got %d", count);
   pgexporter_history_shutdown();

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(pgexporter_bridge_history_store()), 0, cleanup, "bridge init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "bridge write failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(pgexporter_bridge_history_store()), 0, cleanup, "bridge prune failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "bridge retention is 1h, expected 1 row, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_bridge_history_postgresql_settings_are_separate)
{
   struct configuration* config = (struct configuration*)shmem;
   const struct history_store* local;
   const struct history_store* bridge;

   pgexporter_snprintf(config->history_postgresql_host, MISC_LENGTH, "%s", "local-db");
   pgexporter_snprintf(config->history_postgresql_database, MISC_LENGTH, "%s", "pgexporter_history");
   pgexporter_snprintf(config->history_user.username, MAX_USERNAME_LENGTH, "%s", "local_user");
   pgexporter_snprintf(config->bridge_history_postgresql_host, MISC_LENGTH, "%s", "bridge-db");
   pgexporter_snprintf(config->bridge_history_postgresql_database, MISC_LENGTH, "%s", "pgexporter_bridge_history");
   config->bridge_history_postgresql_port = 5433;
   pgexporter_snprintf(config->bridge_history_user.username, MAX_USERNAME_LENGTH, "%s", "bridge_user");

   local = pgexporter_local_history_store();
   bridge = pgexporter_bridge_history_store();
   MCTF_ASSERT_STR_EQ(local->pg_host, "local-db", cleanup, "local store should keep its own host");
   MCTF_ASSERT_STR_EQ(local->pg_user, "local_user", cleanup, "local store should keep its own user");
   MCTF_ASSERT_STR_EQ(bridge->pg_host, "bridge-db", cleanup, "bridge store should use bridge_history_postgresql_host");
   MCTF_ASSERT_STR_EQ(bridge->pg_database, "pgexporter_bridge_history", cleanup, "bridge store should use its own database");
   MCTF_ASSERT_INT_EQ(bridge->pg_port, 5433, cleanup, "bridge store should use its own port");
   MCTF_ASSERT_STR_EQ(bridge->pg_user, "bridge_user", cleanup, "bridge store should use bridge_history_user");

   config->bridge_history = 1;
   config->bridge_history_backend = HISTORY_BACKEND_POSTGRESQL;
   pgexporter_snprintf(config->bridge_history_postgresql_user, MAX_USERNAME_LENGTH, "%s", "bridge_user");
   config->bridge_history_postgresql_password_file[0] = '\0';
   MCTF_ASSERT_INT_EQ(pgexporter_read_bridge_history_user_configuration(shmem), 0, cleanup,
                      "a missing password file should be allowed");
   MCTF_ASSERT_STR_EQ(config->bridge_history_user.username, "bridge_user", cleanup, "user should be copied");
   MCTF_ASSERT_STR_EQ(config->bridge_history_user.password, "", cleanup, "password should be empty");

   pgexporter_snprintf(config->bridge_history_postgresql_password_file, MAX_PATH, "%s",
                       "/tmp/pgexporter-test/no-such-bridge-history.conf");
   MCTF_ASSERT_INT_EQ(pgexporter_read_bridge_history_user_configuration(shmem), 1, cleanup,
                      "a missing file should be reported");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_bridge_history_slots_are_independent)
{
   const struct history_store* local = pgexporter_local_history_store();
   const struct history_store* bridge = pgexporter_bridge_history_store();
   bool local_claimed = false;
   bool bridge_claimed = false;

   local_claimed = pgexporter_history_claim_slot(local, -1);
   MCTF_ASSERT(local_claimed, cleanup, "local slot should be free");

   bridge_claimed = pgexporter_history_claim_slot(bridge, -1);
   MCTF_ASSERT(bridge_claimed, cleanup, "a held local slot must not block the bridge slot");
   MCTF_ASSERT(!pgexporter_history_claim_slot(bridge, -1), cleanup, "the bridge slot is single-flight");

cleanup:
   if (bridge_claimed)
   {
      pgexporter_history_release_slot(bridge);
   }
   if (local_claimed)
   {
      pgexporter_history_release_slot(local);
   }
   MCTF_FINISH();
}
