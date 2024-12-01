/*
 * Copyright (C) 2024 The pgexporter community
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

/* pgexporter */
#include <pgexporter.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <status.h>
#include <utils.h>

void
pgexporter_status(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct configuration* config;

   pgexporter_memory_init();
   pgexporter_start_logging();

   config = (struct configuration*)shmem;

   start_time = time(NULL);

   if (pgexporter_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   pgexporter_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS, (uintptr_t)config->number_of_servers, ValueInt32);

   pgexporter_json_create(&servers);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      struct json* js = NULL;

      pgexporter_json_create(&js);

      pgexporter_json_put(js, MANAGEMENT_ARGUMENT_ACTIVE, (uintptr_t)config->servers[i].fd != -1 ? true : false, ValueBool);
      pgexporter_json_put(js, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[i].name, ValueString);

      pgexporter_json_append(servers, (uintptr_t)js, ValueJSON);
   }

   pgexporter_json_put(response, MANAGEMENT_ARGUMENT_SERVERS, (uintptr_t)servers, ValueJSON);

   end_time = time(NULL);

   if (pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_NETWORK, compression, encryption, payload);
      pgexporter_log_error("Status: Error sending response");

      goto error;
   }

   elapsed = pgexporter_get_timestamp_string(start_time, end_time, &total_seconds);

   pgexporter_log_info("Status (Elapsed: %s)", elapsed);

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();
   pgexporter_memory_destroy();

   exit(0);

error:

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();
   pgexporter_memory_destroy();

   exit(1);
}

void
pgexporter_status_details(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct configuration* config;

   pgexporter_memory_init();
   pgexporter_start_logging();

   config = (struct configuration*)shmem;

   start_time = time(NULL);

   if (pgexporter_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   pgexporter_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS, (uintptr_t)config->number_of_servers, ValueInt32);

   pgexporter_json_create(&servers);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      struct json* js = NULL;

      pgexporter_json_create(&js);

      pgexporter_json_put(js, MANAGEMENT_ARGUMENT_ACTIVE, (uintptr_t)config->servers[i].fd != -1 ? true : false, ValueBool);
      pgexporter_json_put(js, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[i].name, ValueString);

      pgexporter_json_append(servers, (uintptr_t)js, ValueJSON);
   }

   pgexporter_json_put(response, MANAGEMENT_ARGUMENT_SERVERS, (uintptr_t)servers, ValueJSON);

   end_time = time(NULL);

   if (pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK, compression, encryption, payload);
      pgexporter_log_error("Status details: Error sending response");

      goto error;
   }

   elapsed = pgexporter_get_timestamp_string(start_time, end_time, &total_seconds);

   pgexporter_log_info("Status details (Elapsed: %s)", elapsed);

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();
   pgexporter_memory_destroy();

   exit(0);

error:

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();
   pgexporter_memory_destroy();

   exit(1);
}
