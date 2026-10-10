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
#include <history.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <queries.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

#include <mctf.h>
#include <tscommon.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

static int prepare_store(void);
static int truncate_history(void);
static int64_t history_scalar(const char* sql);

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

#define SKIP_WITHOUT_TIMESCALEDB()                                                                \
   do                                                                                             \
   {                                                                                              \
      if (history_scalar("SELECT COUNT(*) FROM pg_extension WHERE extname = 'timescaledb'") != 1) \
      {                                                                                           \
         MCTF_SKIP("timescaledb extension is not installed");                                     \
      }                                                                                           \
   }                                                                                              \
   while (0)

MCTF_TEST_SETUP(history_timescaledb)
{
   struct configuration* config;

   pgexporter_test_config_save();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   config->history = 1;
   config->history_backend = HISTORY_BACKEND_TIMESCALEDB;
   config->history_postgresql_port = config->servers[0].port;
   config->history_postgresql_tls = SERVER_TLS_OFF;
   pgexporter_snprintf(config->history_postgresql_host, MISC_LENGTH, "%s", config->servers[0].host);
   pgexporter_snprintf(config->history_postgresql_database, MISC_LENGTH, "%s", "pgexporter_history");
   pgexporter_snprintf(config->history_postgresql_user, MAX_USERNAME_LENGTH, "%s", config->servers[0].username);
   pgexporter_snprintf(config->history_postgresql_password_file, MAX_PATH, "%s/conf/pgexporter_history.conf", TEST_BASE_DIR);
}

MCTF_TEST_TEARDOWN(history_timescaledb)
{
   pgexporter_history_shutdown();
   truncate_history();
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}

MCTF_TEST(test_history_timescaledb_credential)
{
   struct configuration* config = (struct configuration*)shmem;

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_STR_EQ(config->history_user.username, config->history_postgresql_user, cleanup, "Wrong history user %s", config->history_user.username);
   MCTF_ASSERT(strlen(config->history_user.password) > 0, cleanup, "History password is empty");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_timescaledb_create)
{
   SKIP_WITHOUT_TIMESCALEDB();

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "Failed to create the history store");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM timescaledb_information.hypertables WHERE hypertable_name = 'sample'"),
                      1, cleanup, "sample should be a hypertable");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_timescaledb_create_again)
{
   SKIP_WITHOUT_TIMESCALEDB();

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "First create failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "Create on an existing hypertable failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_timescaledb_write_and_query_roundtrip)
{
   struct history_record in[2];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   SKIP_WITHOUT_TIMESCALEDB();
   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   make_record(&in[0], now, "srv1", "pg_up", "server=\"srv1\"", 1.0);
   make_record(&in[1], now + 1, "srv1", "pg_up", "server=\"srv1\"", 0.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write_batch failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pg_up", now, now + 10, &out, &count), 0,
                      cleanup, "query_range failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 rows, got %d", count);
   MCTF_ASSERT(out[0].value == 1.0, cleanup, "row0 value mismatch");
   MCTF_ASSERT(out[1].value == 0.0, cleanup, "row1 value mismatch");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_timescaledb_prune_drops_old_chunk)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[2];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   SKIP_WITHOUT_TIMESCALEDB();
   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_SEC(86400);

   /* Ten days back is a finished chunk. drop_chunks keeps a chunk that still overlaps the cutoff. */
   make_record(&in[0], now - (10 * 86400), "s", "m", "", 1.0);
   make_record(&in[1], now, "s", "m", "", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "expected 1 row after prune, got %d", count);
   MCTF_ASSERT(out[0].ts == now, cleanup, "the sample in the open chunk should remain");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_timescaledb_prune_disabled_keeps_all)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[2];
   int count = -1;
   time_t now = time(NULL);

   SKIP_WITHOUT_TIMESCALEDB();
   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_DISABLED;

   make_record(&in[0], now - (10 * 86400), "s", "m", "", 1.0);
   make_record(&in[1], now, "s", "m", "", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune with disabled retention should succeed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "disabled retention should keep both rows, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_timescaledb_prune_sweeps_orphan_series)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[2];
   time_t now = time(NULL);

   SKIP_WITHOUT_TIMESCALEDB();
   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_SEC(86400);

   make_record(&in[0], now - (10 * 86400), "primary", "stale_series", "server=\"primary\"", 1.0);
   make_record(&in[1], now, "primary", "live_series", "server=\"primary\"", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");

   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 1, cleanup,
                      "a series with no samples left is no longer a series");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series WHERE metric = 'live_series'"), 1, cleanup,
                      "the series that still has samples must be kept");

cleanup:
   MCTF_FINISH();
}

static int
prepare_store(void)
{
   if (pgexporter_read_history_user_configuration(shmem) ||
       pgexporter_history_create() ||
       pgexporter_history_init())
   {
      return 1;
   }

   return truncate_history();
}

static int
history_connect(SSL** ssl, int* fd)
{
   struct configuration* config;

   config = (struct configuration*)shmem;
   *ssl = NULL;
   *fd = -1;

   if (config->history_user.username[0] == '\0' &&
       pgexporter_read_history_user_configuration(shmem))
   {
      return 1;
   }

   if (pgexporter_authenticate_host("test", config->history_postgresql_host, config->history_postgresql_port,
                                    config->history_postgresql_tls, config->history_postgresql_tls_cert_file,
                                    config->history_postgresql_tls_key_file, config->history_postgresql_tls_ca_file,
                                    config->history_postgresql_database, config->history_user.username,
                                    config->history_user.password, ssl, fd) != AUTH_SUCCESS)
   {
      return 1;
   }

   return 0;
}

static void
history_disconnect(SSL* ssl, int fd)
{
   if (fd != -1)
   {
      pgexporter_write_terminate(ssl, fd);
   }

   pgexporter_close_ssl(ssl);

   if (fd != -1)
   {
      pgexporter_disconnect(fd);
   }
}

static int
truncate_history(void)
{
   SSL* ssl = NULL;
   int fd = -1;
   char* sql = "DO $$ BEGIN "
               "IF EXISTS (SELECT 1 FROM pg_tables WHERE schemaname = 'pgexporter' AND tablename = 'sample') THEN "
               "TRUNCATE pgexporter.sample, pgexporter.series RESTART IDENTITY; "
               "END IF; "
               "END $$;";

   if (history_connect(&ssl, &fd))
   {
      return 1;
   }

   if (pgexporter_command_execute_params(ssl, fd, sql, 0, NULL))
   {
      history_disconnect(ssl, fd);
      return 1;
   }

   history_disconnect(ssl, fd);

   return 0;
}

static int64_t
history_scalar(const char* sql)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query* query = NULL;
   int64_t result = -1;

   if (history_connect(&ssl, &fd))
   {
      return -1;
   }

   if (!pgexporter_query_execute_params(ssl, fd, (char*)sql, 0, NULL, "test", &query) &&
       query != NULL && query->tuples != NULL && query->tuples->data[0] != NULL)
   {
      result = strtoll(query->tuples->data[0], NULL, 10);
   }

   pgexporter_free_query(query);
   history_disconnect(ssl, fd);

   return result;
}
