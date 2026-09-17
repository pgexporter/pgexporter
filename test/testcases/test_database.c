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
#include <extension.h>
#include <message.h>
#include <queries.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

#include <mctf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int get_connection(SSL** ssl, int* fd);
static char* query_value(SSL* ssl, int fd, char* sql, int nparams, char** values);

MCTF_TEST(test_database_connection)
{
   struct configuration* config;
   int connected_servers = 0;

   pgexporter_test_setup();

   config = (struct configuration*)shmem;

   pgexporter_open_connections();

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (config->servers[i].fd != -1)
      {
         connected_servers++;
      }
   }

   MCTF_ASSERT(connected_servers > 0, cleanup, "No servers connected. Expected at least 1 connected server, got %d/%d",
               connected_servers, config->number_of_servers);

cleanup:
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_version_query)
{
   struct configuration* config;
   struct query* query = NULL;
   struct tuple* current = NULL;
   int ret;
   bool server_tested = false;

   pgexporter_test_setup();

   config = (struct configuration*)shmem;

   pgexporter_open_connections();

   for (int i = 0; i < config->number_of_servers && !server_tested; i++)
   {
      if (config->servers[i].fd != -1)
      {
         ret = pgexporter_query_version(i, &query);
         MCTF_ASSERT(ret == 0, cleanup, "Failed to execute version query on server %s", config->servers[i].name);
         MCTF_ASSERT_PTR_NONNULL(query, cleanup, "Version query returned NULL");

         current = query->tuples;
         MCTF_ASSERT_PTR_NONNULL(current, cleanup, "No version data returned from query");

         pgexporter_free_query(query);
         query = NULL;
         server_tested = true;
      }
   }

   MCTF_ASSERT(server_tested, cleanup, "No servers available for version query test");

cleanup:
   if (query != NULL)
   {
      pgexporter_free_query(query);
   }
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_extension_path)
{
   struct configuration* config;
   char* bin_path = NULL;
   char* program_path = NULL;
   char cwd[1024];
   int ret;
   char* cwd_result;

   pgexporter_test_setup();

   config = (struct configuration*)shmem;

   cwd_result = getcwd(cwd, sizeof(cwd));
   MCTF_ASSERT(cwd_result != NULL, cleanup, "Failed to get current directory");

   program_path = pgexporter_append(program_path, cwd);
   program_path = pgexporter_append(program_path, "/build/src/pgexporter");

   ret = pgexporter_setup_extensions_path(config, program_path, &bin_path);

   MCTF_ASSERT(ret == 0, cleanup, "Extension path setup failed");
   MCTF_ASSERT(bin_path != NULL && strlen(bin_path) > 0, cleanup, "Extension path is empty");

cleanup:
   if (program_path != NULL)
   {
      free(program_path);
      program_path = NULL;
   }
   if (bin_path != NULL)
   {
      free(bin_path);
      bin_path = NULL;
   }
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_message_bind)
{
   struct message* msg = NULL;
   char* values[] = {"a'b", NULL, ""};
   char* data = NULL;
   int offset;

   pgexporter_test_setup();

   MCTF_ASSERT_INT_EQ(pgexporter_create_bind_message("", "s1", 3, values, &msg), 0, cleanup, "Failed to create bind message");

   data = (char*)msg->data;

   MCTF_ASSERT_INT_EQ(pgexporter_read_byte(data), 'B', cleanup, "Wrong message type");
   MCTF_ASSERT_INT_EQ(pgexporter_read_int32(data + 1), msg->length - 1, cleanup, "Wrong message length");
   MCTF_ASSERT_STR_EQ(pgexporter_read_string(data + 5), "", cleanup, "Wrong portal name");
   MCTF_ASSERT_STR_EQ(pgexporter_read_string(data + 6), "s1", cleanup, "Wrong statement name");

   offset = 9;
   MCTF_ASSERT_INT_EQ(pgexporter_read_int16(data + offset), 0, cleanup, "Parameters should use the default text format");
   offset += 2;
   MCTF_ASSERT_INT_EQ(pgexporter_read_int16(data + offset), 3, cleanup, "Wrong number of parameters");
   offset += 2;

   MCTF_ASSERT_INT_EQ(pgexporter_read_int32(data + offset), 3, cleanup, "Wrong length for a'b");
   MCTF_ASSERT(!memcmp(data + offset + 4, "a'b", 3), cleanup, "Wrong value for a'b");
   offset += 4 + 3;
   MCTF_ASSERT_INT_EQ(pgexporter_read_int32(data + offset), -1, cleanup, "NULL should be sent with length -1");
   offset += 4;
   MCTF_ASSERT_INT_EQ(pgexporter_read_int32(data + offset), 0, cleanup, "Empty string should be sent with length 0");
   offset += 4;

   MCTF_ASSERT_INT_EQ(pgexporter_read_int16(data + offset), 0, cleanup, "Results should use the default text format");
   offset += 2;
   MCTF_ASSERT_INT_EQ(offset, msg->length, cleanup, "Unexpected data after the last field");

cleanup:
   pgexporter_free_message(msg);
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_message_invalid)
{
   struct message* msg = NULL;

   pgexporter_test_setup();

   MCTF_ASSERT_INT_EQ(pgexporter_create_describe_message('X', "", &msg), 1, cleanup, "Describe should only accept 'S' or 'P'");
   MCTF_ASSERT_INT_EQ(pgexporter_create_close_message('X', "", &msg), 1, cleanup, "Close should only accept 'S' or 'P'");
   MCTF_ASSERT_INT_EQ(pgexporter_create_parse_message("", "SELECT 1", 70000, &msg), 1, cleanup, "Parse should reject more than 65535 parameters");
   MCTF_ASSERT_INT_EQ(pgexporter_create_parse_message("", NULL, 0, &msg), 1, cleanup, "Parse should reject a NULL query");
   MCTF_ASSERT_INT_EQ(pgexporter_create_bind_message("", "", 1, NULL, &msg), 1, cleanup, "Bind should reject missing values");
   MCTF_ASSERT_PTR_NULL(msg, cleanup, "No message should be created for invalid input");

cleanup:
   pgexporter_free_message(msg);
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_query_params)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query* query = NULL;
   char* values[] = {"it's", "back\\slash", "$1"};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_query_execute_params(ssl, fd, "SELECT $1::text, $2::text, $3::text", 3, values, "test", &query), 0,
                      cleanup, "Failed to execute query");
   MCTF_ASSERT_PTR_NONNULL(query->tuples, cleanup, "Query returned no rows");
   MCTF_ASSERT_INT_EQ(query->number_of_columns, 3, cleanup, "Expected 3 columns, got %d", query->number_of_columns);
   MCTF_ASSERT_STR_EQ(query->tuples->data[0], "it's", cleanup, "Quote was not preserved");
   MCTF_ASSERT_STR_EQ(query->tuples->data[1], "back\\slash", cleanup, "Backslash was not preserved");
   MCTF_ASSERT_STR_EQ(query->tuples->data[2], "$1", cleanup, "Placeholder text was not preserved");

cleanup:
   pgexporter_free_query(query);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_query_params_null)
{
   SSL* ssl = NULL;
   int fd = -1;
   char* result = NULL;
   char* values[] = {NULL};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   result = query_value(ssl, fd, "SELECT $1::text IS NULL", 1, values);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Failed to execute query");
   MCTF_ASSERT_STR_EQ(result, "t", cleanup, "NULL parameter was not sent as NULL");

cleanup:
   free(result);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_query_params_double)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query* query = NULL;
   char number[64];
   char* values[] = {number, "NaN", "Infinity"};
   double value;

   pgexporter_test_setup();

   pgexporter_snprintf(number, sizeof(number), "%.17g", 0.1 + 0.2);

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_query_execute_params(ssl, fd, "SELECT $1::float8, $2::float8, $3::float8", 3, values, "test", &query), 0,
                      cleanup, "Failed to execute query");
   MCTF_ASSERT_PTR_NONNULL(query->tuples, cleanup, "Query returned no rows");

   value = strtod(query->tuples->data[0], NULL);
   MCTF_ASSERT(value == 0.1 + 0.2, cleanup, "Double changed in transit: %s", query->tuples->data[0]);

   value = strtod(query->tuples->data[1], NULL);
   MCTF_ASSERT(isnan(value), cleanup, "Expected NaN, got %s", query->tuples->data[1]);

   value = strtod(query->tuples->data[2], NULL);
   MCTF_ASSERT(isinf(value) && value > 0, cleanup, "Expected Infinity, got %s", query->tuples->data[2]);

cleanup:
   pgexporter_free_query(query);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_query_params_none)
{
   SSL* ssl = NULL;
   int fd = -1;
   char* result = NULL;

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   result = query_value(ssl, fd, "SELECT 42", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Failed to execute query without parameters");
   MCTF_ASSERT_STR_EQ(result, "42", cleanup, "Expected 42, got %s", result);

cleanup:
   free(result);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_query_params_no_rows)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query* query = NULL;
   char* values[] = {"1"};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_query_execute_params(ssl, fd, "SELECT $1::int AS id WHERE false", 1, values, "test", &query), 0,
                      cleanup, "An empty result should not be an error");
   MCTF_ASSERT_INT_EQ(query->number_of_columns, 1, cleanup, "Expected 1 column, got %d", query->number_of_columns);
   MCTF_ASSERT_STR_EQ(query->names[0], "id", cleanup, "Wrong column name %s", query->names[0]);
   MCTF_ASSERT_PTR_NULL(query->tuples, cleanup, "Expected no rows");

cleanup:
   pgexporter_free_query(query);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_database_query_params_error)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query* query = NULL;
   char* result = NULL;

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_query_execute_params(ssl, fd, "SELEC 1", 0, NULL, "test", &query), 1, cleanup, "Syntax error was not reported");
   MCTF_ASSERT_PTR_NULL(query, cleanup, "No query should be returned on error");

   result = query_value(ssl, fd, "SELECT 1", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Connection is not usable after an error");

cleanup:
   free(result);
   pgexporter_free_query(query);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_command_params)
{
   SSL* ssl = NULL;
   int fd = -1;
   char* result = NULL;
   char* alice[] = {"1", "alice"};
   char* bob[] = {"2", "o'brien"};
   char* id[] = {"2"};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "CREATE TEMP TABLE test_command (id int PRIMARY KEY, name text)", 0, NULL), 0,
                      cleanup, "Failed to create table");
   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "INSERT INTO test_command VALUES ($1, $2)", 2, alice), 0,
                      cleanup, "Failed to insert first row");
   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "INSERT INTO test_command VALUES ($1, $2)", 2, bob), 0,
                      cleanup, "Failed to insert second row");

   result = query_value(ssl, fd, "SELECT name FROM test_command WHERE id = $1", 1, id);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Inserted row not found");
   MCTF_ASSERT_STR_EQ(result, "o'brien", cleanup, "Expected o'brien, got %s", result);

cleanup:
   free(result);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_database_command_params_error)
{
   SSL* ssl = NULL;
   int fd = -1;
   char* result = NULL;
   char* values[] = {"1"};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "INSERT INTO missing_table VALUES ($1)", 1, values), 1,
                      cleanup, "Insert into a missing table was not reported");

   result = query_value(ssl, fd, "SELECT 1", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Connection is not usable after an error");

cleanup:
   free(result);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_pipeline)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query_pipeline* pipeline = NULL;
   char* result = NULL;
   char id[16];
   char name[32];
   char* values[] = {id, name};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "CREATE TEMP TABLE test_pipeline (id int PRIMARY KEY, name text)", 0, NULL), 0,
                      cleanup, "Failed to create table");

   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_create(&pipeline), 0, cleanup, "Failed to create pipeline");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_prepare(pipeline, "insert", "INSERT INTO test_pipeline VALUES ($1, $2)", 2), 0,
                      cleanup, "Failed to prepare insert");

   for (int i = 1; i <= 1000; i++)
   {
      pgexporter_snprintf(id, sizeof(id), "%d", i);
      pgexporter_snprintf(name, sizeof(name), "row %d", i);
      MCTF_ASSERT_INT_EQ(pgexporter_pipeline_execute(pipeline, "insert", 2, values), 0, cleanup, "Failed to queue row %d", i);
   }

   MCTF_ASSERT_INT_EQ(pipeline->count, 1000, cleanup, "Expected 1000 queued rows, got %d", pipeline->count);
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(ssl, fd, pipeline), 0, cleanup, "Failed to send pipeline");
   MCTF_ASSERT_INT_EQ(pipeline->count, 0, cleanup, "Pipeline should be empty after sync");

   result = query_value(ssl, fd, "SELECT count(*) FROM test_pipeline", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Failed to count rows");
   MCTF_ASSERT_STR_EQ(result, "1000", cleanup, "Expected 1000 rows, got %s", result);
   free(result);

   result = query_value(ssl, fd, "SELECT name FROM test_pipeline WHERE id = 500", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Row 500 not found");
   MCTF_ASSERT_STR_EQ(result, "row 500", cleanup, "Expected 'row 500', got %s", result);

cleanup:
   free(result);
   pgexporter_pipeline_destroy(pipeline);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_pipeline_reuse)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query_pipeline* pipeline = NULL;
   char* result = NULL;
   char* first[] = {"1"};
   char* second[] = {"2"};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "CREATE TEMP TABLE test_reuse (id int PRIMARY KEY)", 0, NULL), 0,
                      cleanup, "Failed to create table");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_create(&pipeline), 0, cleanup, "Failed to create pipeline");

   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_prepare(pipeline, "insert", "INSERT INTO test_reuse VALUES ($1)", 1), 0, cleanup, "Failed to prepare insert");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_execute(pipeline, "insert", 1, first), 0, cleanup, "Failed to queue first row");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(ssl, fd, pipeline), 0, cleanup, "Failed to send first batch");

   /* The statement name is still taken on the server */
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_prepare(pipeline, "insert", "INSERT INTO test_reuse VALUES ($1)", 1), 0, cleanup, "Failed to prepare insert again");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_execute(pipeline, "insert", 1, second), 0, cleanup, "Failed to queue second row");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(ssl, fd, pipeline), 0, cleanup, "Failed to send second batch");

   result = query_value(ssl, fd, "SELECT count(*) FROM test_reuse", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Failed to count rows");
   MCTF_ASSERT_STR_EQ(result, "2", cleanup, "Expected 2 rows, got %s", result);

cleanup:
   free(result);
   pgexporter_pipeline_destroy(pipeline);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_pipeline_empty)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query_pipeline* pipeline = NULL;
   char* result = NULL;

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_create(&pipeline), 0, cleanup, "Failed to create pipeline");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(ssl, fd, pipeline), 0, cleanup, "Syncing an empty pipeline should succeed");

   result = query_value(ssl, fd, "SELECT 1", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Connection is not usable after an empty sync");

cleanup:
   free(result);
   pgexporter_pipeline_destroy(pipeline);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_database_pipeline_rollback)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct query_pipeline* pipeline = NULL;
   char* result = NULL;
   char* ids[] = {"1", "2", "3", "1"};

   pgexporter_test_setup();

   pgexporter_open_connections();
   MCTF_ASSERT_INT_EQ(get_connection(&ssl, &fd), 0, cleanup, "No connected server");

   MCTF_ASSERT_INT_EQ(pgexporter_execute_command_params(ssl, fd, "CREATE TEMP TABLE test_rollback (id int PRIMARY KEY)", 0, NULL), 0,
                      cleanup, "Failed to create table");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_create(&pipeline), 0, cleanup, "Failed to create pipeline");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_prepare(pipeline, "insert", "INSERT INTO test_rollback VALUES ($1)", 1), 0, cleanup, "Failed to prepare insert");

   for (int i = 0; i < 4; i++)
   {
      MCTF_ASSERT_INT_EQ(pgexporter_pipeline_execute(pipeline, "insert", 1, &ids[i]), 0, cleanup, "Failed to queue id %s", ids[i]);
   }

   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(ssl, fd, pipeline), 1, cleanup, "Duplicate key was not reported");

   result = query_value(ssl, fd, "SELECT count(*) FROM test_rollback", 0, NULL);
   MCTF_ASSERT_PTR_NONNULL(result, cleanup, "Failed to count rows");
   MCTF_ASSERT_STR_EQ(result, "0", cleanup, "Batch was not rolled back, %s rows left", result);

   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_prepare(pipeline, "insert", "INSERT INTO test_rollback VALUES ($1)", 1), 0, cleanup, "Failed to prepare insert again");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_execute(pipeline, "insert", 1, &ids[0]), 0, cleanup, "Failed to queue row");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(ssl, fd, pipeline), 0, cleanup, "Pipeline did not recover after a failed batch");

cleanup:
   free(result);
   pgexporter_pipeline_destroy(pipeline);
   pgexporter_close_connections();
   pgexporter_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_database_pipeline_invalid)
{
   char* values[] = {"1"};

   pgexporter_test_setup();

   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_prepare(NULL, "insert", "SELECT 1", 0), 1, cleanup, "Prepare should fail without a pipeline");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_execute(NULL, "insert", 1, values), 1, cleanup, "Execute should fail without a pipeline");
   MCTF_ASSERT_INT_EQ(pgexporter_pipeline_sync(NULL, -1, NULL), 1, cleanup, "Sync should fail without a pipeline");

   pgexporter_pipeline_destroy(NULL);

cleanup:
   pgexporter_test_teardown();
   MCTF_FINISH();
}

static int
get_connection(SSL** ssl, int* fd)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (config->servers[i].fd != -1)
      {
         *ssl = config->servers[i].ssl;
         *fd = config->servers[i].fd;
         return 0;
      }
   }

   return 1;
}

static char*
query_value(SSL* ssl, int fd, char* sql, int nparams, char** values)
{
   struct query* q = NULL;
   char* result = NULL;

   if (!pgexporter_query_execute_params(ssl, fd, sql, nparams, values, "test", &q) &&
       q != NULL && q->tuples != NULL && q->tuples->data[0] != NULL)
   {
      result = pgexporter_append(NULL, q->tuples->data[0]);
   }

   pgexporter_free_query(q);

   return result;
}
