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

#include <mctf.h>

MCTF_TEST(test_cli_ping)
{
   int socket = -1;
   int ret;

   socket = pgexporter_tsclient_get_connection();
   MCTF_ASSERT(pgexporter_socket_isvalid(socket), cleanup, "Failed to get connection to pgexporter");

   ret = pgexporter_management_request_ping(NULL, socket, MANAGEMENT_COMPRESSION_NONE,
                                            MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to send ping request");

   ret = pgexporter_tsclient_check_outcome(socket);
   MCTF_ASSERT(ret == 0, cleanup, "Ping command returned unsuccessful outcome");

   pgexporter_disconnect(socket);
   socket = -1;
cleanup:
   if (socket >= 0)
   {
      pgexporter_disconnect(socket);
   }
   MCTF_FINISH();
}

MCTF_TEST(test_cli_status)
{
   int socket = -1;
   int ret;

   socket = pgexporter_tsclient_get_connection();
   MCTF_ASSERT(pgexporter_socket_isvalid(socket), cleanup, "Failed to get connection to pgexporter");

   ret = pgexporter_management_request_status(NULL, socket, MANAGEMENT_COMPRESSION_NONE,
                                              MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON);
   MCTF_ASSERT(ret == 0, cleanup, "Failed to send status request");

   ret = pgexporter_tsclient_check_outcome(socket);
   MCTF_ASSERT(ret == 0, cleanup, "Status command returned unsuccessful outcome");

   pgexporter_disconnect(socket);
   socket = -1;
cleanup:
   if (socket >= 0)
   {
      pgexporter_disconnect(socket);
   }
   MCTF_FINISH();
}
