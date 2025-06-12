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
#include <pgexporter.h>
#include <http.h>
#include <logging.h>
#include <network.h>
#include <security.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <unistd.h>
#include <openssl/err.h>

static int http_build_header(int method, char* path, char** request);
static int http_extract_headers_body(char* response, struct http* http);

int
pgexporter_http_add_header(struct http* http, char* name, char* value)
{
   char* tmp = NULL;

   tmp = pgexporter_append(http->request_headers, name);
   if (tmp == NULL)
   {
      return 1;
   }
   http->request_headers = tmp;

   tmp = pgexporter_append(http->request_headers, ": ");
   if (tmp == NULL)
   {
      return 1;
   }
   http->request_headers = tmp;

   tmp = pgexporter_append(http->request_headers, value);
   if (tmp == NULL)
   {
      return 1;
   }
   http->request_headers = tmp;

   tmp = pgexporter_append(http->request_headers, "\r\n");
   if (tmp == NULL)
   {
      return 1;
   }
   http->request_headers = tmp;

   return 0;
}

static int
http_build_header(int method, char* path, char** request)
{
   char* r = NULL;
   *request = NULL;

   if (method == PGEXPORTER_HTTP_GET)
   {
      r = pgexporter_append(r, "GET ");
   }
   else if (method == PGEXPORTER_HTTP_POST)
   {
      r = pgexporter_append(r, "POST ");
   }
   else if (method == PGEXPORTER_HTTP_PUT)
   {
      r = pgexporter_append(r, "PUT ");
   }
   else
   {
      pgexporter_log_error("Invalid HTTP method: %d", method);
      return 1;
   }

   r = pgexporter_append(r, path);
   r = pgexporter_append(r, " HTTP/1.1\r\n");

   *request = r;

   return 0;
}

static int
http_extract_headers_body(char* response, struct http* http)
{
   bool header = true;
   char* p = NULL;
   char* response_copy = NULL;

   if (response == NULL)
   {
      pgexporter_log_error("Response is NULL");
      goto error;
   }

   response_copy = strdup(response);
   if (response_copy == NULL)
   {
      pgexporter_log_error("Failed to duplicate response string");
      goto error;
   }

   p = strtok(response_copy, "\n");
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

   free(response_copy);
   return 0;

error:
   free(response_copy);
   return 1;
}

int
pgexporter_http_get(struct http* http, char* hostname, char* path)
{
   struct message msg_request;
   struct message* msg_response = NULL;
   int error = 0;
   int status;
   char* request = NULL;
   char* full_request = NULL;
   char* response = NULL;
   char* user_agent = NULL;
   char* endpoint = (path != NULL) ? path : "/metrics";

   memset(&msg_request, 0, sizeof(struct message));

   pgexporter_log_trace("Starting pgexporter_http_get");
   if (http_build_header(PGEXPORTER_HTTP_GET, endpoint, &request))
   {
      pgexporter_log_error("Failed to build HTTP header");
      goto error;
   }

   pgexporter_http_add_header(http, "Host", hostname);
   user_agent = pgexporter_append(user_agent, "pgexporter/");
   user_agent = pgexporter_append(user_agent, VERSION);
   pgexporter_http_add_header(http, "User-Agent", user_agent);
   pgexporter_http_add_header(http, "Accept", "text/*");
   pgexporter_http_add_header(http, "Connection", "close");

   full_request = pgexporter_append(NULL, request);
   full_request = pgexporter_append(full_request, http->request_headers);
   full_request = pgexporter_append(full_request, "\r\n");

   msg_request.data = full_request;
   msg_request.length = strlen(full_request) + 1;

   error = 0;
req:
   if (error < 5)
   {
      status = pgexporter_write_message(http->ssl, http->socket, &msg_request);
      if (status != MESSAGE_STATUS_OK)
      {
         error++;
         pgexporter_log_debug("Write failed, retrying (%d/5)", error);
         goto req;
      }
   }
   else
   {
      pgexporter_log_error("Failed to write after 5 attempts");
      goto error;
   }

res:
   // Get a pointer to the global message structure
   status = pgexporter_read_block_message(http->ssl, http->socket, &msg_response);
   if (status != MESSAGE_STATUS_ZERO)
   {
      if (status == MESSAGE_STATUS_OK)
      {
         // Get data from the response message
         if (msg_response != NULL && msg_response->data != NULL)
         {
            response = pgexporter_append(response, (char*)msg_response->data);
         }
         pgexporter_clear_message();
         goto res;
      }
      else
      {
         pgexporter_log_error("Error reading response");
         goto error;
      }
   }

   if (msg_response != NULL && msg_response->length > 0 && msg_response->data != NULL)
   {
      response = pgexporter_append(response, (char*)msg_response->data);
   }

   if (http_extract_headers_body(response, http))
   {
      pgexporter_log_error("Failed to extract headers and body");
      goto error;
   }

   pgexporter_log_debug("HTTP Headers: %s", http->headers != NULL ? http->headers : "NULL");
   pgexporter_log_debug("HTTP Body: %s", http->body != NULL ? http->body : "NULL");

   free(request);
   free(full_request);
   free(response);
   free(user_agent);

   free(http->request_headers);
   http->request_headers = NULL;

   return 0;

error:
   free(request);
   free(full_request);
   free(response);
   free(user_agent);
   free(http->request_headers);
   http->request_headers = NULL;
   return 1;
}

int
pgexporter_http_connect(char* hostname, int port, bool secure, struct http** result)
{
   struct http* h = NULL;
   int socket_fd = -1;
   SSL* ssl = NULL;
   SSL_CTX* ctx = NULL;

   pgexporter_log_debug("Connecting to %s:%d (secure: %d)", hostname, port, secure);
   h = (struct http*) malloc(sizeof(struct http));
   if (h == NULL)
   {
      pgexporter_log_error("Failed to allocate HTTP structure");
      goto error;
   }

   memset(h, 0, sizeof(struct http));

   if (pgexporter_connect(hostname, port, &socket_fd))
   {
      pgexporter_log_error("Failed to connect to %s:%d", hostname, port);
      goto error;
   }

   h->socket = socket_fd;

   if (secure)
   {
      if (pgexporter_create_ssl_ctx(true, &ctx))
      {
         pgexporter_log_error("Failed to create SSL context");
         goto error;
      }

      if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) == 0)
      {
         pgexporter_log_error("Failed to set minimum TLS version");
         goto error;
      }

      ssl = SSL_new(ctx);
      if (ssl == NULL)
      {
         pgexporter_log_error("Failed to create SSL structure");
         goto error;
      }

      if (SSL_set_fd(ssl, socket_fd) == 0)
      {
         pgexporter_log_error("Failed to set SSL file descriptor");
         goto error;
      }

      int connect_result;
      do
      {
         connect_result = SSL_connect(ssl);

         if (connect_result != 1)
         {
            int err = SSL_get_error(ssl, connect_result);
            switch (err)
            {
               case SSL_ERROR_WANT_READ:
               case SSL_ERROR_WANT_WRITE:
                  continue;
               default:
                  pgexporter_log_error("SSL connection failed: %s", ERR_error_string(err, NULL));
                  goto error;
            }
         }
      }
      while (connect_result != 1);

      h->ssl = ssl;
   }

   *result = h;

   return 0;

error:
   if (ssl != NULL)
   {
      SSL_free(ssl);
   }
   if (ctx != NULL)
   {
      SSL_CTX_free(ctx);
   }
   if (socket_fd != -1)
   {
      pgexporter_disconnect(socket_fd);
   }
   free(h);
   return 1;
}

int
pgexporter_http_post(struct http* http, char* hostname, char* path, char* data, size_t length)
{
   struct message msg_request;
   struct message* msg_request_ptr = NULL;
   int error = 0;
   int status;
   char* request = NULL;
   char* full_request = NULL;
   char* response = NULL;
   char* user_agent = NULL;
   char content_length[32];

   memset(&msg_request, 0, sizeof(struct message));

   pgexporter_log_trace("Starting pgexporter_http_post");
   if (http_build_header(PGEXPORTER_HTTP_POST, path, &request))
   {
      pgexporter_log_error("Failed to build HTTP header");
      goto error;
   }

   pgexporter_http_add_header(http, "Host", hostname);
   user_agent = pgexporter_append(user_agent, "pgexporter/");
   user_agent = pgexporter_append(user_agent, VERSION);
   pgexporter_http_add_header(http, "User-Agent", user_agent);
   pgexporter_http_add_header(http, "Connection", "close");

   sprintf(content_length, "%zu", length);
   pgexporter_http_add_header(http, "Content-Length", content_length);
   pgexporter_http_add_header(http, "Content-Type", "application/x-www-form-urlencoded");

   full_request = pgexporter_append(NULL, request);
   full_request = pgexporter_append(full_request, http->request_headers);
   full_request = pgexporter_append(full_request, "\r\n");

   if (data != NULL && length > 0)
   {
      full_request = pgexporter_append(full_request, data);
   }

   msg_request_ptr = (struct message*)malloc(sizeof(struct message));
   if (msg_request_ptr == NULL)
   {
      pgexporter_log_error("Failed to allocate msg_request");
      goto error;
   }

   memset(msg_request_ptr, 0, sizeof(struct message));

   msg_request_ptr->data = full_request;
   msg_request_ptr->length = strlen(full_request) + 1;

   error = 0;
req:
   if (error < 5)
   {
      status = pgexporter_write_message(http->ssl, http->socket, msg_request_ptr);
      if (status != MESSAGE_STATUS_OK)
      {
         error++;
         pgexporter_log_debug("Write failed, retrying (%d/5)", error);
         goto req;
      }
   }
   else
   {
      pgexporter_log_error("Failed to write after 5 attempts");
      goto error;
   }

   status = pgexporter_http_read(http->ssl, http->socket, &response);

   if (response == NULL)
   {
      pgexporter_log_error("No response data collected");
      goto error;
   }

   if (http_extract_headers_body(response, http))
   {
      pgexporter_log_error("Failed to extract headers and body");
      goto error;
   }

   free(request);
   free(full_request);
   free(response);
   free(msg_request_ptr);
   free(user_agent);

   free(http->request_headers);
   http->request_headers = NULL;

   return 0;

error:
   free(request);
   free(full_request);
   free(response);
   free(msg_request_ptr);
   free(user_agent);
   free(http->request_headers);
   http->request_headers = NULL;
   return 1;
}

int
pgexporter_http_put(struct http* http, char* hostname, char* path, const void* data, size_t length)
{
   struct message msg_request;
   struct message* msg_request_ptr = NULL;
   int error = 0;
   int status;
   char* request = NULL;
   char* full_request = NULL;
   char* response = NULL;
   char* user_agent = NULL;
   char* complete_request = NULL;
   char content_length[32];

   memset(&msg_request, 0, sizeof(struct message));

   pgexporter_log_trace("Starting pgexporter_http_put");
   if (http_build_header(PGEXPORTER_HTTP_PUT, path, &request))
   {
      pgexporter_log_error("Failed to build HTTP header");
      goto error;
   }

   pgexporter_http_add_header(http, "Host", hostname);
   user_agent = pgexporter_append(user_agent, "pgexporter/");
   user_agent = pgexporter_append(user_agent, VERSION);
   pgexporter_http_add_header(http, "User-Agent", user_agent);
   pgexporter_http_add_header(http, "Connection", "close");

   sprintf(content_length, "%zu", length);
   pgexporter_http_add_header(http, "Content-Length", content_length);
   pgexporter_http_add_header(http, "Content-Type", "application/octet-stream");

   full_request = pgexporter_append(NULL, request);
   full_request = pgexporter_append(full_request, http->request_headers);
   full_request = pgexporter_append(full_request, "\r\n");

   size_t headers_len = strlen(full_request);
   size_t total_len = headers_len + length;

   complete_request = malloc(total_len + 1);
   if (complete_request == NULL)
   {
      pgexporter_log_error("Failed to allocate complete request");
      goto error;
   }

   memcpy(complete_request, full_request, headers_len);

   if (data != NULL && length > 0)
   {
      memcpy(complete_request + headers_len, data, length);
   }

   complete_request[total_len] = '\0';

   msg_request_ptr = (struct message*)malloc(sizeof(struct message));
   if (msg_request_ptr == NULL)
   {
      pgexporter_log_error("Failed to allocate msg_request");
      goto error;
   }

   memset(msg_request_ptr, 0, sizeof(struct message));

   msg_request_ptr->data = complete_request;
   msg_request_ptr->length = total_len + 1;

   error = 0;
req:
   if (error < 5)
   {
      status = pgexporter_write_message(http->ssl, http->socket, msg_request_ptr);
      if (status != MESSAGE_STATUS_OK)
      {
         error++;
         pgexporter_log_debug("Write failed, retrying (%d/5)", error);
         goto req;
      }
   }
   else
   {
      pgexporter_log_error("Failed to write after 5 attempts");
      goto error;
   }

   status = pgexporter_http_read(http->ssl, http->socket, &response);

   if (response == NULL)
   {
      pgexporter_log_error("No response data collected");
      goto error;
   }

   if (http_extract_headers_body(response, http))
   {
      pgexporter_log_error("Failed to extract headers and body");
      goto error;
   }

   free(request);
   free(full_request);
   free(response);
   free(msg_request_ptr->data);
   free(msg_request_ptr);
   free(user_agent);

   free(http->request_headers);
   http->request_headers = NULL;

   return 0;

error:
   free(request);
   free(full_request);
   free(response);
   free(complete_request);
   free(msg_request_ptr);
   free(user_agent);
   free(http->request_headers);
   http->request_headers = NULL;

   return 1;
}

int
pgexporter_http_put_file(struct http* http, char* hostname, char* path, FILE* file, size_t file_size, char* content_type)
{
   struct message msg_request;
   struct message* msg_request_ptr = NULL;
   int error = 0;
   int status;
   char* request = NULL;
   char* header_part = NULL;
   char* response = NULL;
   char* user_agent = NULL;
   char* full_request = NULL;
   char content_length[32];
   void* file_buffer = NULL;

   memset(&msg_request, 0, sizeof(struct message));

   pgexporter_log_trace("Starting pgexporter_http_put_file");
   if (file == NULL)
   {
      pgexporter_log_error("File is NULL");
      goto error;
   }

   if (http_build_header(PGEXPORTER_HTTP_PUT, path, &request))
   {
      pgexporter_log_error("Failed to build HTTP header");
      goto error;
   }

   pgexporter_http_add_header(http, "Host", hostname);
   user_agent = pgexporter_append(user_agent, "pgexporter/");
   user_agent = pgexporter_append(user_agent, VERSION);
   pgexporter_http_add_header(http, "User-Agent", user_agent);
   pgexporter_http_add_header(http, "Connection", "close");

   sprintf(content_length, "%zu", file_size);
   pgexporter_http_add_header(http, "Content-Length", content_length);

   /* default to application/octet-stream if not specified */
   char* type = (content_type != NULL) ? content_type : "application/octet-stream";
   pgexporter_http_add_header(http, "Content-Type", type);

   header_part = pgexporter_append(NULL, request);
   header_part = pgexporter_append(header_part, http->request_headers);
   header_part = pgexporter_append(header_part, "\r\n");

   pgexporter_log_trace("File size: %zu", file_size);

   rewind(file);

   file_buffer = malloc(file_size);
   if (file_buffer == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for file content: %zu bytes", file_size);
      goto error;
   }

   size_t bytes_read = fread(file_buffer, 1, file_size, file);
   if (bytes_read != file_size)
   {
      pgexporter_log_error("Failed to read entire file. Expected %zu bytes, got %zu", file_size, bytes_read);
      goto error;
   }

   pgexporter_log_trace("Read %zu bytes from file", bytes_read);

   msg_request_ptr = (struct message*)malloc(sizeof(struct message));
   if (msg_request_ptr == NULL)
   {
      pgexporter_log_error("Failed to allocate msg_request");
      goto error;
   }

   memset(msg_request_ptr, 0, sizeof(struct message));

   size_t header_len = strlen(header_part);
   size_t total_len = header_len + file_size;

   full_request = malloc(total_len + 1);
   if (full_request == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for full request: %zu bytes", total_len + 1);
      goto error;
   }

   memcpy(full_request, header_part, header_len);

   memcpy(full_request + header_len, file_buffer, file_size);

   full_request[total_len] = '\0';

   pgexporter_log_trace("Setting msg_request data, total size: %zu", total_len);
   msg_request_ptr->data = full_request;
   msg_request_ptr->length = total_len;

   error = 0;
req:
   if (error < 5)
   {
      status = pgexporter_write_message(http->ssl, http->socket, msg_request_ptr);
      if (status != MESSAGE_STATUS_OK)
      {
         error++;
         pgexporter_log_debug("Write failed, retrying (%d/5)", error);
         goto req;
      }
   }
   else
   {
      pgexporter_log_error("Failed to write after 5 attempts");
      goto error;
   }

   status = pgexporter_http_read(http->ssl, http->socket, &response);

   if (response == NULL)
   {
      pgexporter_log_error("No response data collected");
      goto error;
   }

   if (http_extract_headers_body(response, http))
   {
      pgexporter_log_error("Failed to extract headers and body");
      goto error;
   }

   int status_code = 0;
   if (http->headers != NULL && sscanf(http->headers, "HTTP/1.1 %d", &status_code) == 1)
   {
      pgexporter_log_debug("HTTP status code: %d", status_code);
      if (status_code >= 200 && status_code < 300)
      {
         pgexporter_log_debug("HTTP request successful");
      }
      else
      {
         pgexporter_log_error("HTTP request failed with status code: %d", status_code);
      }
   }

   free(request);
   free(header_part);
   free(response);
   free(file_buffer);
   free(full_request);
   free(msg_request_ptr);
   free(user_agent);

   free(http->request_headers);
   http->request_headers = NULL;

   return (status_code >= 200 && status_code < 300) ? 0 : 1;

error:
   free(request);
   free(header_part);
   free(response);
   free(file_buffer);
   free(full_request);
   free(msg_request_ptr);
   free(user_agent);
   free(http->request_headers);
   http->request_headers = NULL;

   return 1;
}

int
pgexporter_http_read(SSL* ssl, int socket, char** response_text)
{
   char buffer[8192];
   ssize_t bytes_read;
   int total_bytes = 0;
   bool headers_complete = false;
   bool chunked_encoding = false;

   *response_text = NULL;

   while (1)
   {
      if (ssl != NULL)
      {
         bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
         if (bytes_read <= 0)
         {
            int err = SSL_get_error(ssl, bytes_read);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
               continue;
            }
            break;
         }
      }
      else
      {
         bytes_read = read(socket, buffer, sizeof(buffer) - 1);
         if (bytes_read < 0)
         {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
               continue;
            }
            break;
         }
         else if (bytes_read == 0)
         {
            break;
         }
      }

      buffer[bytes_read] = '\0';

      if (*response_text == NULL)
      {
         *response_text = malloc(bytes_read + 1);
         if (*response_text == NULL)
         {
            return MESSAGE_STATUS_ERROR;
         }
         memcpy(*response_text, buffer, bytes_read + 1);
      }
      else
      {
         size_t current_len = strlen(*response_text);
         char* new_response = realloc(*response_text, current_len + bytes_read + 1);
         if (new_response == NULL)
         {
            free(*response_text);
            *response_text = NULL;
            return MESSAGE_STATUS_ERROR;
         }
         *response_text = new_response;
         memcpy(*response_text + current_len, buffer, bytes_read + 1);
      }

      total_bytes += bytes_read;

      if (!headers_complete)
      {
         if (strstr(*response_text, "\r\n\r\n") != NULL)
         {
            headers_complete = true;
            if (strstr(*response_text, "Transfer-Encoding: chunked") != NULL)
            {
               chunked_encoding = true;
            }
         }
      }

      if (!chunked_encoding && bytes_read < sizeof(buffer) - 1)
      {
         break;
      }

      if (chunked_encoding && strstr(*response_text, "\r\n0\r\n\r\n") != NULL)
      {
         break;
      }
   }

   pgexporter_log_debug("Read %d bytes from socket", total_bytes);

   return total_bytes > 0 ? MESSAGE_STATUS_OK : MESSAGE_STATUS_ERROR;
}

int
pgexporter_http_disconnect(struct http* http)
{
   int status = 0;

   if (http != NULL)
   {
      if (http->ssl != NULL)
      {
         pgexporter_close_ssl(http->ssl);
         http->ssl = NULL;
      }

      if (http->socket != -1)
      {
         if (pgexporter_disconnect(http->socket))
         {
            pgexporter_log_error("Failed to disconnect socket in pgexporter_http_disconnect");
            status = 1;
         }
         http->socket = -1;
      }
   }

   if (status != 0)
   {
      goto error;
   }

   return 0;

error:
   return 1;
}

int
pgexporter_http_destroy(struct http* http)
{

   if (http != NULL)
   {
      if (http->headers != NULL)
      {
         free(http->headers);
         http->headers = NULL;
      }

      if (http->body != NULL)
      {
         free(http->body);
         http->body = NULL;
      }

      if (http->request_headers != NULL)
      {
         free(http->request_headers);
         http->request_headers = NULL;
      }
      free(http);
   }

   return 0;
}