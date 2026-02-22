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
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_VAR_CONF_PATH "PGEXPORTER_TEST_CONF"
#define ENV_VAR_USER_CONF "PGEXPORTER_TEST_USER_CONF"
#define ENV_VAR_BASE_DIR  "PGEXPORTER_TEST_BASE_DIR"

char TEST_BASE_DIR[MAX_PATH];

void
pgexporter_test_environment_create(void)
{
   struct configuration* config;
   char* conf_path = NULL;
   char* base_dir = NULL;

   memset(TEST_BASE_DIR, 0, sizeof(TEST_BASE_DIR));

   conf_path = getenv(ENV_VAR_CONF_PATH);
   assert(conf_path != NULL);

   // Create the shared memory for the configuration
   assert(!pgexporter_create_shared_memory(sizeof(struct configuration), HUGEPAGE_OFF, &shmem));

   pgexporter_init_configuration(shmem);

   // Try reading configuration from the configuration path
   assert(!pgexporter_read_configuration(shmem, conf_path));
   config = (struct configuration*)shmem;

   // Some validations just to be safe
   memcpy(&config->configuration_path[0], conf_path, MIN(strlen(conf_path), MAX_PATH - 1));
   assert(config->number_of_servers > 0);

   base_dir = getenv(ENV_VAR_BASE_DIR);
   assert(base_dir != NULL);
   memcpy(TEST_BASE_DIR, base_dir, strlen(base_dir));

   assert(getenv(ENV_VAR_USER_CONF) != NULL);

   pgexporter_start_logging();

   // Try reading the users configuration path
   assert(!pgexporter_read_users_configuration(shmem, getenv(ENV_VAR_USER_CONF)));
}

void
pgexporter_test_environment_destroy(void)
{
   size_t size;

   size = sizeof(struct configuration);

   memset(TEST_BASE_DIR, 0, sizeof(TEST_BASE_DIR));

   pgexporter_stop_logging();

   pgexporter_destroy_shared_memory(shmem, size);
}

void
pgexporter_test_setup(void)
{
   pgexporter_memory_init();
}

void
pgexporter_test_teardown(void)
{
   pgexporter_memory_destroy();
}

static int
json_get_int64_safe(struct json* j, char* key, int64_t* out)
{
   enum value_type type = ValueNone;
   uintptr_t data = 0;

   if (!pgexporter_json_contains_key(j, key))
      return -1;

   data = pgexporter_json_get_typed(j, key, &type);

   switch (type)
   {
      case ValueInt32:
         *out = (int64_t)(int32_t)data;
         return 0;
      case ValueInt64:
         *out = (int64_t)data;
         return 0;
      case ValueUInt32:
         *out = (int64_t)(uint32_t)data;
         return 0;
      case ValueUInt64:
         *out = (int64_t)data;
         return 0;
      default:
         return -1;
   }
}

int
pgexporter_test_assert_conf_set_ok(char* key, char* value, int64_t expected)
{
   int socket = -1;
   struct json* read = NULL;
   struct json* outcome = NULL;
   struct json* response = NULL;
   int64_t got = 0;

   socket = pgexporter_tsclient_get_connection();
   if (!pgexporter_socket_isvalid(socket))
      goto fail;

   if (pgexporter_management_request_conf_set(NULL, socket, key, value,
                                              MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE,
                                              MANAGEMENT_OUTPUT_FORMAT_JSON) != 0)
      goto fail;

   if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read) != 0)
      goto fail;

   outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!(bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
      goto fail;

   response = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);
   if (json_get_int64_safe(response, key, &got) != 0 || got != expected)
      goto fail;

   pgexporter_json_destroy(read);
   pgexporter_disconnect(socket);
   return 0;
fail:
   if (read)
      pgexporter_json_destroy(read);
   if (socket >= 0)
      pgexporter_disconnect(socket);
   return -1;
}

int
pgexporter_test_assert_conf_set_fail(char* key, char* value)
{
   int socket = -1;
   struct json* read = NULL;
   struct json* outcome = NULL;

   socket = pgexporter_tsclient_get_connection();
   if (!pgexporter_socket_isvalid(socket))
      goto fail;

   if (pgexporter_management_request_conf_set(NULL, socket, key, value,
                                              MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE,
                                              MANAGEMENT_OUTPUT_FORMAT_JSON) != 0)
      goto fail;

   if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read) != 0)
      goto fail;

   outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if ((bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
      goto fail;

   pgexporter_json_destroy(read);
   pgexporter_disconnect(socket);
   return 0;
fail:
   if (read)
      pgexporter_json_destroy(read);
   if (socket >= 0)
      pgexporter_disconnect(socket);
   return -1;
}

int
pgexporter_test_assert_conf_get_ok(char* key, int64_t expected)
{
   int socket = -1;
   struct json* read = NULL;
   struct json* outcome = NULL;
   struct json* response = NULL;
   int64_t got = 0;

   socket = pgexporter_tsclient_get_connection();
   if (!pgexporter_socket_isvalid(socket))
      goto fail;

   if (pgexporter_management_request_conf_get(NULL, socket,
                                               MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE,
                                               MANAGEMENT_OUTPUT_FORMAT_JSON) != 0)
      goto fail;

   if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read) != 0)
      goto fail;

   outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!(bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
      goto fail;

   response = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);
   if (json_get_int64_safe(response, key, &got) != 0 || got != expected)
      goto fail;

   pgexporter_json_destroy(read);
   pgexporter_disconnect(socket);
   return 0;
fail:
   if (read)
      pgexporter_json_destroy(read);
   if (socket >= 0)
      pgexporter_disconnect(socket);
   return -1;
}
