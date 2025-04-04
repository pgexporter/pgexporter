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
 */

/* pgexporter */
#include "message.h"
#include <pgexporter.h>
#include <http.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <stdlib.h>
#include <utils.h>

#include <stdio.h>
#include <string.h>

static int build_get_request(int endpoint, char** request);
static int extract_headers_body(char* response, struct http* http);

int
pgexporter_http_create(int endpoint, struct http** http)
{
   struct http* h = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   *http = NULL;

   h = (struct http*)malloc(sizeof(struct http));

   if (h == NULL)
   {
      pgexporter_log_error("Failed to allocate to HTTP");
      goto error;
   }

   /* Initialize */
   memset(h, 0, sizeof(struct http));

   h->endpoint = endpoint;

   if (pgexporter_connect(config->endpoints[endpoint].host, config->endpoints[endpoint].port, &h->socket))
   {
      pgexporter_log_error("Failed to connect to %s:%d",
                           config->endpoints[endpoint].host,
                           config->endpoints[endpoint].port);
      goto error;
   }

   *http = h;

   return 0;

error:

   free(h);

   return 1;
}

int
pgexporter_http_get(struct http* http)
{
   struct message* msg_request = NULL;
   struct message* msg_response = NULL;
   int error = 0;
   int status;
   char* request = NULL;
   char* response = NULL;

   if (build_get_request(http->endpoint, &request))
   {
      goto error;
   }

   msg_request = (struct message*)malloc(sizeof(struct message));
   if (msg_request == NULL)
   {
      goto error;
   }

   memset(msg_request, 0, sizeof(struct message));

   msg_request->data = request;
   msg_request->length = strlen(request) + 1;

   error = 0;
req:
   if (error < 5)
   {
      status = pgexporter_write_message(NULL, http->socket, msg_request);
      if (status != MESSAGE_STATUS_OK)
      {
         error++;
         goto req;
      }
   }
   else
   {
      goto error;
   }

res:
   status = pgexporter_read_block_message(NULL, http->socket, &msg_response);
   if (status != MESSAGE_STATUS_ZERO)
   {
      if (status == MESSAGE_STATUS_OK)
      {
         response = pgexporter_append(response, (char*)msg_response->data);
         pgexporter_clear_message(msg_response);
         goto res;
      }
      else
      {
         goto error;
      }
   }

   if (msg_response->length > 0)
   {
      response = pgexporter_append(response, (char*)msg_response->data);
   }

   if (extract_headers_body(response, http))
   {
      goto error;
   }

   free(request);
   free(response);

   free(msg_request);

   return 0;

error:

   free(request);
   free(response);

   free(msg_request);

   return 1;
}

int
pgexporter_http_destroy(struct http* http)
{
   if (http != NULL)
   {
      pgexporter_disconnect(http->socket);

      free(http->headers);
      free(http->body);
   }

   free(http);
   http = NULL;

   return 0;
}

static int
build_get_request(int endpoint, char** request)
{
   char* r = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;

   *request = NULL;

   r = pgexporter_append(r, "GET /metrics HTTP/1.1\r\n");

   r = pgexporter_append(r, "Host: ");
   r = pgexporter_append(r, config->endpoints[endpoint].host);
   r = pgexporter_append(r, "\r\n");

   r = pgexporter_append(r, "User-Agent: pgexporter/");
   r = pgexporter_append(r, VERSION);
   r = pgexporter_append(r, "\r\n");

   r = pgexporter_append(r, "Accept: text/*\r\n");

   r = pgexporter_append(r, "\r\n");

   *request = r;

   return 0;
}

static int
extract_headers_body(char* response, struct http* http)
{
   bool header = true;
   char* p = NULL;

   p = strtok(response, "\n");
   while (p != NULL)
   {
      if (*p == '\r')
      {
         header = false;
      }
      else
      {
         if (!pgexporter_is_number(p, 16))
         {
            if (header)
            {
               http->headers = pgexporter_append(http->headers, p);
               http->headers = pgexporter_append_char(http->headers, '\n');
            }
            else
            {
               http->body = pgexporter_append(http->body, p);
               http->body = pgexporter_append_char(http->body, '\n');
            }
         }
      }

      p = strtok(NULL, "\n");
   }

   return 0;
}
