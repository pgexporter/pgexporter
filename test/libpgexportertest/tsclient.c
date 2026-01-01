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
#include <management.h>
#include <network.h>
#include <shmem.h>
#include <tsclient.h>

#include <stdlib.h>
#include <string.h>

int
pgexporter_tsclient_check_outcome(int socket)
{
   struct json* read = NULL;
   struct json* outcome = NULL;
   int ret = 1;

   if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (!pgexporter_json_contains_key(read, MANAGEMENT_CATEGORY_OUTCOME))
   {
      goto error;
   }

   outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!pgexporter_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS) ||
       !(bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      goto error;
   }

   ret = 0;

error:
   pgexporter_json_destroy(read);
   return ret;
}

int
pgexporter_tsclient_get_connection(void)
{
   int socket = -1;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (pgexporter_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
   {
      return -1;
   }

   return socket;
}
