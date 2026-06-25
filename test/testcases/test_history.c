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
#include <history.h>
#include <prometheus.h>
#include <art.h>
#include <memory.h>
#include <shmem.h>
#include <utils.h>

#include <mctf.h>
#include <tscommon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Path of the SQLite database used by the test currently running. Set in
 * setup, unlinked in teardown so every test starts from an empty database.
 */
static char db_path[MAX_PATH];

/* Build a history_record compactly. */
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
   /* labels is now a char* in history_record; input records borrow the caller's literal */
   r->labels = (char*)labels;
   r->value = value;
}

/* Remove the db file and its WAL/SHM siblings, ignoring missing files. Intentionally used to prevent cross-test contamination. */
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

MCTF_TEST_SETUP(history)
{
   struct configuration* config;

   pgexporter_test_config_save();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;
   config->history_backend = HISTORY_BACKEND_SQLITE;

   pgexporter_snprintf(db_path, MAX_PATH, "/tmp/pgexporter-test/history-%d.db", (int)getpid());
   unlink_db(db_path);
   pgexporter_snprintf(config->history_path, MAX_PATH, "%s", db_path);
}

MCTF_TEST_TEARDOWN(history)
{
   pgexporter_history_shutdown();
   unlink_db(db_path);
   db_path[0] = '\0';
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}

MCTF_TEST(test_history_init_idempotent)
{
   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "first init should succeed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "second init should be a no-op success");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_init_empty_path)
{
   struct configuration* config = (struct configuration*)shmem;

   config->history_path[0] = '\0';

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 1, cleanup, "init with empty path should fail");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_history_init_unknown_backend)
{
   struct configuration* config = (struct configuration*)shmem;

   config->history_backend = 9999;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 1, cleanup, "unknown backend should fail");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_write_and_query_roundtrip)
{
   struct history_record in[2];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   make_record(&in[0], now, "srv1", "pg_up", "server=\"srv1\"", 1.0);
   make_record(&in[1], now + 1, "srv1", "pg_up", "server=\"srv1\"", 0.0);

   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 2), 0, cleanup, "write_batch failed");

   /* On success query_range allocates the result array via malloc and stores it in `out`;
    * the caller owns it and must free() it */
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

MCTF_TEST(test_history_write_empty_batch)
{
   int count = -1;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(NULL, 0), 0, cleanup, "empty batch should succeed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("anything", 0, time(NULL) + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 0, cleanup, "expected 0 rows after empty batch, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_query_metric_filter)
{
   struct history_record in[3];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   make_record(&in[0], now, "s", "metric_a", "", 1.0);
   make_record(&in[1], now, "s", "metric_b", "", 2.0);
   make_record(&in[2], now, "s", "metric_a", "", 3.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("metric_a", now - 1, now + 1, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 metric_a rows, got %d", count);
   MCTF_ASSERT_STR_EQ(out[0].metric, "metric_a", cleanup, "filtered metric mismatch");
   MCTF_ASSERT_STR_EQ(out[1].metric, "metric_a", cleanup, "filtered metric mismatch");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_query_time_window_inclusive)
{
   struct history_record in[3];
   struct history_record* out = NULL;
   int count = 0;
   time_t base = 1000000;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   make_record(&in[0], base, "s", "m", "", 1.0);       /* on lower bound  */
   make_record(&in[1], base + 50, "s", "m", "", 2.0);  /* inside          */
   make_record(&in[2], base + 100, "s", "m", "", 3.0); /* on upper bound  */
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", base, base + 100, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 3, cleanup, "inclusive window should return 3, got %d", count);
   pgexporter_history_records_free(out, count);
   out = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", base + 1, base + 99, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "narrow window should return 1, got %d", count);
   MCTF_ASSERT(out[0].ts == base + 50, cleanup, "wrong row returned for narrow window");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_query_orders_by_ts_asc)
{
   struct history_record in[4];
   struct history_record* out = NULL;
   int count = 0;
   time_t base = 2000000;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   make_record(&in[0], base + 30, "s", "m", "", 0.0);
   make_record(&in[1], base + 10, "s", "m", "", 0.0);
   make_record(&in[2], base + 40, "s", "m", "", 0.0);
   make_record(&in[3], base + 20, "s", "m", "", 0.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 4), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", base, base + 100, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 4, cleanup, "expected 4 rows, got %d", count);
   MCTF_ASSERT(out[0].ts == base + 10, cleanup, "order[0] wrong");
   MCTF_ASSERT(out[1].ts == base + 20, cleanup, "order[1] wrong");
   MCTF_ASSERT(out[2].ts == base + 30, cleanup, "order[2] wrong");
   MCTF_ASSERT(out[3].ts == base + 40, cleanup, "order[3] wrong");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_query_count_only)
{
   struct history_record in[5];
   int count = -1;
   time_t now = time(NULL);
   int i;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   for (i = 0; i < 5; i++)
   {
      make_record(&in[i], now + i, "s", "m", "", (double)i);
   }
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 5), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", now, now + 10, NULL, &count), 0,
                      cleanup, "count-only query failed");
   MCTF_ASSERT_INT_EQ(count, 5, cleanup, "count-only should report 5, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_query_capacity_growth)
{
   const int n = 250;
   struct history_record* in = NULL;
   struct history_record* out = NULL;
   int count = 0;
   time_t base = 3000000;
   int i;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   in = malloc(sizeof(struct history_record) * n);
   MCTF_ASSERT_PTR_NONNULL(in, cleanup, "alloc failed");
   for (i = 0; i < n; i++)
   {
      make_record(&in[i], base + i, "s", "m", "", (double)i);
   }
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, n), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", base, base + n, &out, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, n, cleanup, "expected %d rows, got %d", n, count);
   MCTF_ASSERT(out[0].ts == base, cleanup, "first row wrong");
   MCTF_ASSERT(out[150].ts == base + 150, cleanup, "mid row wrong");
   MCTF_ASSERT(out[n - 1].ts == base + n - 1, cleanup, "last row wrong");

cleanup:
   if (in)
   {
      free(in);
   }
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

/* Tests the documented contract that uninitialized calls return error cleanly. */
MCTF_TEST(test_history_ops_null_guards)
{
   struct history_record rec;
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   make_record(&rec, now, "s", "m", "", 1.0);

   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(&rec, 1), 1, cleanup, "write should fail without init");
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now, &out, &count), 1, cleanup,
                      "query should fail without init");
   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 1, cleanup, "prune should fail without init");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 1, cleanup, "shutdown should fail without init");

cleanup:
   pgexporter_history_records_free(out, count);
   MCTF_FINISH();
}

MCTF_TEST(test_history_shutdown_resets_ops)
{
   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 0, cleanup, "shutdown should succeed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_shutdown(), 1, cleanup, "second shutdown should fail (ops reset)");

cleanup:
   MCTF_FINISH();
}

struct mock_prometheus_metric_value
{
   time_t timestamp;
   char* value;
   char* help;
   char* type;
   int sort_type;
};

MCTF_TEST(test_history_store_metrics_edge_cases)
{
   struct configuration* config = (struct configuration*)shmem;
   prometheus_metrics_container_t* container = NULL;
   struct art* t = NULL;
   struct history_record* out = NULL;
   int out_len = 0;

   /* Set up an in-memory test database */
   unlink_db("test_edge_cases.db");
   pgexporter_snprintf(config->history_path, MAX_PATH, "test_edge_cases.db");
   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "history_init failed");

   /* Create a dummy container */
   container = malloc(sizeof(prometheus_metrics_container_t));
   memset(container, 0, sizeof(prometheus_metrics_container_t));

   pgexporter_art_create(&t);
   container->general_metrics = t;

   struct mock_prometheus_metric_value m1;
   memset(&m1, 0, sizeof(m1));
   m1.value = "pgexporter_up 1\n";
   pgexporter_art_insert(t, "m1", (uintptr_t)&m1, ValueRef);

   struct mock_prometheus_metric_value m2;
   memset(&m2, 0, sizeof(m2));
   m2.value = "edge_case_no_labels 42\nedge_case_with_server{server=\"db1\"} 10.5\n";
   pgexporter_art_insert(t, "m2", (uintptr_t)&m2, ValueRef);

   struct mock_prometheus_metric_value m3;
   memset(&m3, 0, sizeof(m3));
   m3.value = "   weird_leading_space 1\nempty_line\n\n# comment\nmetric{broken} 2";
   pgexporter_art_insert(t, "m3", (uintptr_t)&m3, ValueRef);

   /* Run the parser on these edge cases */
   MCTF_ASSERT_INT_EQ(pgexporter_history_store_metrics(container), 0, cleanup, "store_metrics returned error");

   time_t now = time(NULL);
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("edge_case_no_labels", now - 10, now + 10, &out, &out_len), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(out_len, 1, cleanup, "Expected 1 record for edge_case_no_labels");
   MCTF_ASSERT(out[0].value == 42.0, cleanup, "Expected value 42");
   pgexporter_history_records_free(out, out_len);
   out = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("edge_case_with_server", now - 10, now + 10, &out, &out_len), 0, cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(out_len, 1, cleanup, "Expected 1 record for edge_case_with_server");
   MCTF_ASSERT_INT_EQ(strcmp(out[0].server, "db1"), 0, cleanup, "Expected server db1");
   MCTF_ASSERT(out[0].value == 10.5, cleanup, "Expected value 10.5");

cleanup:
   pgexporter_history_records_free(out, out_len);
   if (container)
   {
      if (container->general_metrics)
         pgexporter_art_destroy(container->general_metrics);
      free(container);
   }
   pgexporter_history_shutdown();
   unlink_db("test_edge_cases.db");
   MCTF_FINISH();
}

MCTF_TEST(test_history_prune_deletes_old_keeps_new)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[4];
   struct history_record* out = NULL;
   int count = 0;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   config->history_retention = PGEXPORTER_TIME_SEC(3600); /* keep 1 hour */

   make_record(&in[0], now - 7200, "s", "m", "", 1.0); /* 2h old  -> pruned */
   make_record(&in[1], now - 3700, "s", "m", "", 2.0); /* >1h old -> pruned */
   make_record(&in[2], now - 60, "s", "m", "", 3.0);   /* recent  -> kept   */
   make_record(&in[3], now, "s", "m", "", 4.0);        /* now     -> kept   */
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

MCTF_TEST(test_history_prune_disabled_keeps_all)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[3];
   int count = -1;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   config->history_retention = PGEXPORTER_TIME_DISABLED; /* keep forever */

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

MCTF_TEST(test_history_prune_removes_all_when_all_old)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[3];
   int count = -1;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   config->history_retention = PGEXPORTER_TIME_SEC(60);

   make_record(&in[0], now - 3600, "s", "m", "", 1.0);
   make_record(&in[1], now - 1800, "s", "m", "", 2.0);
   make_record(&in[2], now - 600, "s", "m", "", 3.0);
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 0, cleanup,
                      "all records older than retention should be pruned, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_prune_empty_db_succeeds)
{
   struct configuration* config = (struct configuration*)shmem;

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   config->history_retention = PGEXPORTER_TIME_SEC(60);

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune on empty db should succeed");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_prune_idempotent)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[3];
   int count = -1;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   config->history_retention = PGEXPORTER_TIME_SEC(3600);

   make_record(&in[0], now - 7200, "s", "m", "", 1.0); /* pruned */
   make_record(&in[1], now - 30, "s", "m", "", 2.0);   /* kept   */
   make_record(&in[2], now, "s", "m", "", 3.0);        /* kept   */
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 3), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "first prune failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "expected 2 after first prune, got %d", count);

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "second prune failed");
   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("m", 0, now + 10, NULL, &count), 0,
                      cleanup, "query failed");
   MCTF_ASSERT_INT_EQ(count, 2, cleanup, "second prune should be a no-op, got %d", count);

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_history_prune_spans_all_metrics)
{
   struct configuration* config = (struct configuration*)shmem;
   struct history_record in[4];
   int count = -1;
   time_t now = time(NULL);

   MCTF_ASSERT_INT_EQ(pgexporter_history_init(), 0, cleanup, "init failed");

   config->history_retention = PGEXPORTER_TIME_SEC(3600);

   make_record(&in[0], now - 7200, "s", "metric_a", "", 1.0); /* pruned */
   make_record(&in[1], now - 30, "s", "metric_a", "", 2.0);   /* kept   */
   make_record(&in[2], now - 7200, "s", "metric_b", "", 3.0); /* pruned */
   make_record(&in[3], now - 30, "s", "metric_b", "", 4.0);   /* kept   */
   MCTF_ASSERT_INT_EQ(pgexporter_history_write_batch(in, 4), 0, cleanup, "write failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_prune(), 0, cleanup, "prune failed");

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("metric_a", 0, now + 10, NULL, &count), 0,
                      cleanup, "query metric_a failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "metric_a should have 1 row after prune, got %d", count);

   MCTF_ASSERT_INT_EQ(pgexporter_history_query_range("metric_b", 0, now + 10, NULL, &count), 0,
                      cleanup, "query metric_b failed");
   MCTF_ASSERT_INT_EQ(count, 1, cleanup, "metric_b should have 1 row after prune, got %d", count);

cleanup:
   MCTF_FINISH();
}
