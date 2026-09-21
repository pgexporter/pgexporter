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

static int count_history_tables(void);

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
