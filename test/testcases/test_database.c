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
#include <queries.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

#include <mctf.h>
#include <stdio.h>
#include <unistd.h>

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
