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

static int count_history_tables(void);
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

MCTF_TEST_SETUP(history_postgresql)
{
   struct configuration* config;

   pgexporter_test_config_save();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   config->history = 1;
   config->history_backend = HISTORY_BACKEND_POSTGRESQL;
   config->history_postgresql_port = config->servers[0].port;
   config->history_postgresql_tls = SERVER_TLS_OFF;
   pgexporter_snprintf(config->history_postgresql_host, MISC_LENGTH, "%s", config->servers[0].host);
   pgexporter_snprintf(config->history_postgresql_database, MISC_LENGTH, "%s", "pgexporter_history");
   pgexporter_snprintf(config->history_postgresql_user, MAX_USERNAME_LENGTH, "%s", config->servers[0].username);
   pgexporter_snprintf(config->history_postgresql_password_file, MAX_PATH, "%s/conf/pgexporter_history.conf", TEST_BASE_DIR);
}

MCTF_TEST_TEARDOWN(history_postgresql)
{
   pgexporter_history_shutdown();
   truncate_history();
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}

MCTF_TEST(test_history_postgresql_credential)
{
   struct configuration* config = (struct configuration*)shmem;

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_STR_EQ(config->history_user.username, config->history_postgresql_user, cleanup, "Wrong history user %s", config->history_user.username);
   MCTF_ASSERT(strlen(config->history_user.password) > 0, cleanup, "History password is empty");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_credential_no_file)
{
   struct configuration* config = (struct configuration*)shmem;

   config->history_postgresql_password_file[0] = '\0';

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "A missing password file should be allowed");
   MCTF_ASSERT_STR_EQ(config->history_user.username, config->history_postgresql_user, cleanup, "Wrong history user %s", config->history_user.username);
   MCTF_ASSERT_STR_EQ(config->history_user.password, "", cleanup, "Password should be empty without a password file");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_credential_wrong_user)
{
   struct configuration* config = (struct configuration*)shmem;

   pgexporter_snprintf(config->history_postgresql_user, MAX_USERNAME_LENGTH, "%s", "someone_else");

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 4, cleanup, "A file for another user should be rejected");
   MCTF_ASSERT_STR_EQ(config->history_user.username, "", cleanup, "Credential should be cleared on error");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_credential_missing_file)
{
   struct configuration* config = (struct configuration*)shmem;

   pgexporter_snprintf(config->history_postgresql_password_file, MAX_PATH, "%s/conf/does_not_exist.conf", TEST_BASE_DIR);

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 1, cleanup, "A missing file should be reported");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_create)
{
   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "Failed to create the history store");
   MCTF_ASSERT_INT_EQ(count_history_tables(), 2, cleanup, "Expected the series and sample tables");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_create_again)
{
   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "First create failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "Create on an existing schema failed");
   MCTF_ASSERT_INT_EQ(count_history_tables(), 2, cleanup, "Expected the series and sample tables");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_init)
{
   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 0, cleanup, "Failed to create the history store");

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "First init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "Init while connected should succeed");

   pgexporter_history_shutdown();

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "Init after shutdown failed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_postgresql_init_no_password)
{
   struct configuration* config = (struct configuration*)shmem;

   config->history_postgresql_password_file[0] = '\0';

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 1, cleanup, "Login without a password should fail");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_postgresql_create_missing_database)
{
   struct configuration* config = (struct configuration*)shmem;

   pgexporter_snprintf(config->history_postgresql_database, MISC_LENGTH, "%s", "pgexporter_missing");

   MCTF_ASSERT_INT_EQ(pgexporter_read_history_user_configuration(shmem), 0, cleanup, "Failed to read history credential");
   MCTF_ASSERT_INT_EQ(pgexporter_history_create(), 1, cleanup, "Create against a missing database should fail");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_write_and_query_roundtrip)
{
   struct history_record in[2];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   make_record(&in[0], now, "srv1", "pg_up", "server=\"srv1\"", 1.0);
   make_record(&in[1], now + 1, "srv1", "pg_up", "server=\"srv1\"", 0.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write_batch failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pg_up", now, now + 10, &out, &count), 0,
                      cleanup, "query_range failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 rows, got %d", count);
   MCTF_ASSERT_PTR_NONNULL(out, cleanup, "out should be allocated");
   MCTF_ASSERT(out[0].ts == now, cleanup, "row0 ts mismatch");
   MCTF_ASSERT_STR_EQ(out[0].server, "srv1", cleanup, "row0 server mismatch");
   MCTF_ASSERT_STR_EQ(out[0].metric, "pg_up", cleanup, "row0 metric mismatch");
   MCTF_ASSERT_STR_EQ(out[0].labels, "server=\"srv1\"", cleanup, "row0 labels mismatch");
   MCTF_ASSERT(out[0].value == 1.0, cleanup, "row0 value mismatch");
   MCTF_ASSERT(out[1].value == 0.0, cleanup, "row1 value mismatch");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_write_empty_batch)
{
   int count = -1;

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(NULL, 0), 0, cleanup, "empty batch should succeed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("anything", 0, time(NULL) + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "expected 0 rows after empty batch, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_query_metric_filter)
{
   struct history_record in[3];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   make_record(&in[0], now, "s", "metric_a", "", 1.0);
   make_record(&in[1], now, "s", "metric_b", "", 2.0);
   make_record(&in[2], now + 1, "s", "metric_a", "", 3.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("metric_a", now - 1, now + 2, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 metric_a rows, got %d", count);
   MCTF_ASSERT_STR_EQ(out[0].metric, "metric_a", cleanup, "filtered metric mismatch");
   MCTF_ASSERT_STR_EQ(out[1].metric, "metric_a", cleanup, "filtered metric mismatch");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_query_time_window_inclusive)
{
   struct history_record in[3];
   struct history_record* out = NULL;
   int count = 0;
   time_t base = 1000000;

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   make_record(&in[0], base, "s", "m", "", 1.0);
   make_record(&in[1], base + 50, "s", "m", "", 2.0);
   make_record(&in[2], base + 100, "s", "m", "", 3.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", base, base + 100, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "inclusive window should return 3, got %d", count);
   pgexporter_history_records_free(out, count);
   out = NULL;
   count = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", base + 1, base + 99, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "narrow window should return 1, got %d", count);
   MCTF_ASSERT(out[0].ts == base + 50, cleanup, "wrong row returned for narrow window");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_sample_duplicate_last_wins)
{
   struct history_record in[2];
   struct history_record* out = NULL;
   int count = 0;
   time_t base = 4400000;

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   make_record(&in[0], base, "primary", "pg_up", "server=\"primary\"", 1.0);
   make_record(&in[1], base, "primary", "pg_up", "server=\"primary\"", 9.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.sample"), 1, cleanup,
                      "the same series at the same instant is one row");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pg_up", base, base, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "expected 1 row, got %d", count);
   MCTF_ASSERT(out[0].value == 9.0, cleanup, "the later write should win, got %f", out[0].value);

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_series_survives_label_reorder)
{
   struct history_record in[2];
   struct history_record* out = NULL;
   int count = 0;
   time_t base = 4100000;

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   make_record(&in[0], base, "primary", "pg_x", "server=\"primary\",database=\"pgbench\"", 1.0);
   make_record(&in[1], base + 60, "primary", "pg_x", "database=\"pgbench\",server=\"primary\"", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 1, cleanup,
                      "reordered labels must resolve to the same series, not a second one");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("pg_x", base, base + 60, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "both samples should still be readable, got %d", count);

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_series_written_once)
{
   struct history_record in[1];
   time_t base = 4000000;
   int i;

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   for (i = 0; i < 10; i++)
   {
      make_record(&in[0], base + (i * 60), "primary", "pg_up", "server=\"primary\"", (double)i);
      MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 1), 0, cleanup, "write %d failed", i);
   }

   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 1, cleanup,
                      "ten snapshots of one series should store one series row");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.sample"), 10, cleanup,
                      "ten snapshots should store ten samples");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_prune_deletes_old_keeps_new)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[4];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_SEC(3600);

   make_record(&in[0], now - 7200, "s", "m", "", 1.0);
   make_record(&in[1], now - 3700, "s", "m", "", 2.0);
   make_record(&in[2], now - 60, "s", "m", "", 3.0);
   make_record(&in[3], now, "s", "m", "", 4.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 4), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 rows after prune, got %d", count);
   MCTF_ASSERT(out[0].ts == now - 60, cleanup, "kept row0 ts wrong");
   MCTF_ASSERT(out[1].ts == now, cleanup, "kept row1 ts wrong");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_prune_disabled_keeps_all)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[3];
   int count = -1;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_DISABLED;

   make_record(&in[0], now - 100000, "s", "m", "", 1.0);
   make_record(&in[1], now - 50000, "s", "m", "", 2.0);
   make_record(&in[2], now, "s", "m", "", 3.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup,
                      "prune with disabled retention should succeed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "disabled retention should keep all 3, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_prune_sweeps_orphan_series)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[3];
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_SEC(3600);

   make_record(&in[0], now - 7200, "primary", "stale_series", "server=\"primary\"", 1.0);
   make_record(&in[1], now - 7200, "primary", "live_series", "server=\"primary\"", 2.0);
   make_record(&in[2], now - 60, "primary", "live_series", "server=\"primary\"", 3.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 2, cleanup,
                      "expected 2 series before prune");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");

   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.sample"), 1, cleanup,
                      "only the sample inside the retention window should remain");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 1, cleanup,
                      "a series with no samples left is no longer a series");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series WHERE metric = 'live_series'"), 1, cleanup,
                      "the series that still has samples must be kept");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_postgresql_prune_keeps_series_reusable)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[1];
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(prepare_store(), 0, cleanup, "Failed to prepare the history store");

   config->history_retention = PGEXPORTER_TIME_SEC(3600);

   make_record(&in[0], now - 7200, "primary", "pg_up", "server=\"primary\"", 1.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 1), 0, cleanup, "write failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 0, cleanup,
                      "series should have been swept");

   make_record(&in[0], now, "primary", "pg_up", "server=\"primary\"", 2.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 1), 0, cleanup, "write after sweep failed");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.series"), 1, cleanup,
                      "series should have been recreated");
   MCTF_ASSERT_INT_EQ((int)history_scalar("SELECT COUNT(*) FROM pgexporter.sample WHERE series_id NOT IN (SELECT series_id FROM pgexporter.series)"),
                      0, cleanup, "the recreated sample must point at the new series");

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

static int
count_history_tables(void)
{
   struct configuration* config;
   SSL* ssl = NULL;
   int fd = -1;
   struct query* query = NULL;
   int count = -1;

   config = (struct configuration*)shmem;

   if (pgexporter_authenticate_host("test", config->history_postgresql_host, config->history_postgresql_port,
                                    config->history_postgresql_tls, config->history_postgresql_tls_cert_file,
                                    config->history_postgresql_tls_key_file, config->history_postgresql_tls_ca_file,
                                    config->history_postgresql_database, config->history_user.username,
                                    config->history_user.password, &ssl, &fd) != AUTH_SUCCESS)
   {
      return -1;
   }

   if (!pgexporter_query_execute_params(ssl, fd, "SELECT count(*) FROM pg_tables WHERE schemaname = 'pgexporter'", 0, NULL, "test", &query) &&
       query->tuples != NULL && query->tuples->data[0] != NULL)
   {
      count = atoi(query->tuples->data[0]);
   }

   pgexporter_free_query(query);
   pgexporter_write_terminate(ssl, fd);
   pgexporter_close_ssl(ssl);
   pgexporter_disconnect(fd);

   return count;
}
