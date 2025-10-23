/*
 * Copyright (C) 2025 The pgexporter community
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
#include <logging.h>
#include <memory.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define ENV_VAR_CONF_PATH "PGEXPORTER_TEST_CONF"
#define ENV_VAR_USER_CONF "PGEXPORTER_TEST_USER_CONF"
#define ENV_VAR_BASE_DIR "PGEXPORTER_TEST_BASE_DIR"

char TEST_BASE_DIR[MAX_PATH];

void
pgexporter_test_environment_create(void)
{
   struct configuration* config;
   char* conf_path = NULL;
   char* user_conf_path = NULL;
   char* base_dir = NULL;
   size_t size = 0;

   memset(TEST_BASE_DIR, 0, sizeof(TEST_BASE_DIR));

   conf_path = getenv(ENV_VAR_CONF_PATH);
   assert(conf_path != NULL);

   // Create the shared memory for the configuration
   size = sizeof(struct configuration);
   assert(!pgexporter_create_shared_memory(size, HUGEPAGE_OFF, &shmem));

   pgexporter_init_configuration(shmem);

   // Try reading configuration from the configuration path
   assert(!pgexporter_read_configuration(shmem, conf_path));
   config = (struct configuration*) shmem;

   // Some validations just to be safe
   memcpy(&config->configuration_path[0], conf_path, MIN(strlen(conf_path), MAX_PATH - 1));
   assert(config->number_of_servers > 0);

   base_dir = getenv(ENV_VAR_BASE_DIR);
   assert(base_dir != NULL);
   memcpy(TEST_BASE_DIR, base_dir, strlen(base_dir));

   user_conf_path = getenv(ENV_VAR_USER_CONF);
   assert(user_conf_path != NULL);

   pgexporter_start_logging();

   // Try reading the users configuration path
   assert(!pgexporter_read_users_configuration(shmem, user_conf_path));
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
