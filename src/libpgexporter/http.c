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
#include <string.h>
#include <unistd.h>
#include <openssl/err.h>

static int http_read_response(SSL* ssl, int socket, char** response);
static int http_parse_response(char* response, struct http_response* http_response);
static int http_build_request(struct http* connection, struct http_request* request, char** full_request, size_t* full_request_size);
static char* http_method_to_string(int method);

int
pgexporter_http_create(char* hostname, int port, bool secure, struct http** result)
{
   struct http* connection = NULL;
   int socket_fd = -1;
   SSL* ssl = NULL;
   SSL_CTX* ctx = NULL;

   if (hostname == NULL || result == NULL)
   {
      pgexporter_log_error("Invalid parameters for HTTP connection");
      goto error;
   }

   pgexporter_log_debug("Creating HTTP connection to %s:%d (secure: %d)", hostname, port, secure);

   connection = (struct http*)malloc(sizeof(struct http));
   if (connection == NULL)
   {
      pgexporter_log_error("Failed to allocate HTTP connection structure");
      goto error;
   }

   memset(connection, 0, sizeof(struct http));

   if (pgexporter_connect(hostname, port, &socket_fd))
   {
      pgexporter_log_error("Failed to connect to %s:%d", hostname, port);
      goto error;
   }

   connection->socket = socket_fd;
   connection->hostname = strdup(hostname);
   connection->port = port;
   connection->secure = secure;

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

      connection->ssl = ssl;
   }

   *result = connection;

   return PGEXPORTER_HTTP_STATUS_OK;

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
   if (connection != NULL)
   {
      free(connection->hostname);
      free(connection);
   }

   return PGEXPORTER_HTTP_STATUS_ERROR;
}

int
pgexporter_http_request_create(int method, char* path, struct http_request** result)
{
   struct http_request* request = NULL;

   if (path == NULL || result == NULL)
   {
      pgexporter_log_error("Invalid parameters for HTTP request");
      goto error;
   }

   request = (struct http_request*)malloc(sizeof(struct http_request));
   if (request == NULL)
   {
      pgexporter_log_error("Failed to allocate HTTP request structure");
      goto error;
   }

   memset(request, 0, sizeof(struct http_request));

   request->method = method;
   request->path = strdup(path);
   if (request->path == NULL)
   {
      pgexporter_log_error("Failed to duplicate path string");
      goto error;
   }

   if (pgexporter_deque_create(false, &request->payload.headers))
   {
      pgexporter_log_error("Failed to create headers deque");
      goto error;
   }

   *result = request;

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   if (request != NULL)
   {
      free(request->path);
      pgexporter_deque_destroy(request->payload.headers);
      free(request);
   }
   return PGEXPORTER_HTTP_STATUS_ERROR;
}

int
pgexporter_http_request_add_header(struct http_request* request, char* name, char* value)
{
   if (request == NULL || name == NULL || value == NULL)
   {
      pgexporter_log_error("Invalid parameters for adding HTTP header");
      goto error;
   }

   if (request->payload.headers == NULL)
   {
      pgexporter_log_error("Headers deque is NULL");
      goto error;
   }

   if (pgexporter_deque_add(request->payload.headers, name, (uintptr_t)value, ValueString))
   {
      pgexporter_log_error("Failed to add header to deque");
      goto error;
   }

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   return PGEXPORTER_HTTP_STATUS_ERROR;
}

char*
pgexporter_http_request_get_header(struct http_request* request, char* name)
{
   if (request == NULL || name == NULL)
   {
      return NULL;
   }

   if (request->payload.headers == NULL)
   {
      return NULL;
   }

   return (char*)pgexporter_deque_get(request->payload.headers, name);
}

int
pgexporter_http_request_update_header(struct http_request* request, char* name, char* value)
{
   if (request == NULL || name == NULL || value == NULL)
   {
      pgexporter_log_error("Invalid parameters for updating HTTP header");
      goto error;
   }

   if (request->payload.headers == NULL)
   {
      pgexporter_log_error("Headers deque is NULL");
      goto error;
   }

   if (pgexporter_deque_remove(request->payload.headers, name) > 0)
   {
      pgexporter_log_trace("Removed existing header: %s", name);
   }

   if (pgexporter_deque_add(request->payload.headers, name, (uintptr_t)value, ValueString))
   {
      pgexporter_log_error("Failed to add updated header to deque");
      goto error;
   }

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   return PGEXPORTER_HTTP_STATUS_ERROR;
}

int
pgexporter_http_request_remove_header(struct http_request* request, char* name)
{
   if (request == NULL || name == NULL)
   {
      pgexporter_log_error("Invalid parameters for removing HTTP header");
      goto error;
   }

   if (request->payload.headers == NULL)
   {
      pgexporter_log_error("Headers deque is NULL");
      goto error;
   }

   int removed = pgexporter_deque_remove(request->payload.headers, name);
   if (removed == 0)
   {
      pgexporter_log_debug("Header not found for removal: %s", name);
   }

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   return PGEXPORTER_HTTP_STATUS_ERROR;
}

int
pgexporter_http_set_data(struct http_request* request, void* data, size_t size)
{
   if (request == NULL)
   {
      pgexporter_log_error("Invalid request parameter");
      goto error;
   }

   if (request->payload.data != NULL)
   {
      free(request->payload.data);
      request->payload.data = NULL;
      request->payload.data_size = 0;
   }

   if (data != NULL && size > 0)
   {
      request->payload.data = malloc(size);
      if (request->payload.data == NULL)
      {
         pgexporter_log_error("Failed to allocate memory for request data");
         goto error;
      }

      memcpy(request->payload.data, data, size);
      request->payload.data_size = size;
   }

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   return PGEXPORTER_HTTP_STATUS_ERROR;
}

char*
pgexporter_http_get_response_header(struct http_response* response, char* name)
{
   if (response == NULL || name == NULL)
   {
      return NULL;
   }

   if (response->payload.headers == NULL)
   {
      return NULL;
   }

   return (char*)pgexporter_deque_get(response->payload.headers, name);
}

int
pgexporter_http_invoke(struct http* connection, struct http_request* request, struct http_response** response)
{
   struct message* msg_request = NULL;
   char* full_request = NULL;
   size_t full_request_size = 0;
   char* response_text = NULL;
   struct http_response* http_response = NULL;
   int error = 0;
   int status;

   if (connection == NULL || request == NULL || response == NULL)
   {
      pgexporter_log_error("Invalid parameters for HTTP invoke");
      goto error;
   }

   pgexporter_log_trace("Invoking HTTP request");

   http_response = (struct http_response*)malloc(sizeof(struct http_response));
   if (http_response == NULL)
   {
      pgexporter_log_error("Failed to allocate HTTP response structure");
      goto error;
   }
   memset(http_response, 0, sizeof(struct http_response));

   if (http_build_request(connection, request, &full_request, &full_request_size))
   {
      pgexporter_log_error("Failed to build HTTP request");
      goto error;
   }

   msg_request = (struct message*)malloc(sizeof(struct message));
   if (msg_request == NULL)
   {
      pgexporter_log_error("Failed to allocate message structure");
      goto error;
   }

   memset(msg_request, 0, sizeof(struct message));
   msg_request->data = full_request;
   msg_request->length = full_request_size;

   error = 0;
req:
   if (error < 5)
   {
      status = pgexporter_write_message(connection->ssl, connection->socket, msg_request);
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

   status = http_read_response(connection->ssl, connection->socket, &response_text);
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_log_error("Failed to read HTTP response");
      goto error;
   }

   if (http_parse_response(response_text, http_response))
   {
      pgexporter_log_error("Failed to parse HTTP response");
      goto error;
   }

   *response = http_response;

   free(full_request);
   free(response_text);
   free(msg_request);

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   free(full_request);
   free(response_text);
   free(msg_request);
   if (http_response != NULL)
   {
      pgexporter_http_response_destroy(http_response);
   }

   return PGEXPORTER_HTTP_STATUS_ERROR;
}

int
pgexporter_http_request_destroy(struct http_request* request)
{
   if (request != NULL)
   {
      free(request->path);
      pgexporter_deque_destroy(request->payload.headers);
      free(request->payload.data);
      free(request);
   }

   return PGEXPORTER_HTTP_STATUS_OK;
}

int
pgexporter_http_response_destroy(struct http_response* response)
{
   if (response != NULL)
   {
      pgexporter_deque_destroy(response->payload.headers);
      free(response->payload.data);
      free(response);
   }

   return PGEXPORTER_HTTP_STATUS_OK;
}

int
pgexporter_http_destroy(struct http* connection)
{
   if (connection != NULL)
   {
      if (connection->ssl != NULL)
      {
         pgexporter_close_ssl(connection->ssl);
      }

      if (connection->socket != -1)
      {
         pgexporter_disconnect(connection->socket);
      }

      free(connection->hostname);
      free(connection);
   }

   return PGEXPORTER_HTTP_STATUS_OK;
}

static int
http_read_response(SSL* ssl, int socket, char** response_text)
{
   char buffer[8192];
   ssize_t bytes_read;
   int total_bytes = 0;
   bool headers_complete = false;
   bool chunked_encoding = false;

   *response_text = NULL;

   while (1)
   {
      if (ssl)
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

   return total_bytes > 0 ? MESSAGE_STATUS_OK : MESSAGE_STATUS_ERROR;
}

static int
http_parse_response(char* response, struct http_response* http_response)
{
   bool header = true;
   char* p = NULL;
   char* response_copy = NULL;
   char* status_line = NULL;
   char* colon = NULL;
   char* name = NULL;
   char* value = NULL;

   if (response == NULL || http_response == NULL)
   {
      pgexporter_log_error("Invalid parameters for parsing HTTP response");
      goto error;
   }

   response_copy = strdup(response);
   if (response_copy == NULL)
   {
      pgexporter_log_error("Failed to duplicate response string");
      goto error;
   }

   if (pgexporter_deque_create(false, &http_response->payload.headers))
   {
      pgexporter_log_error("Failed to create headers deque for response");
      goto error;
   }

   status_line = strtok(response_copy, "\n");
   if (status_line != NULL && sscanf(status_line, "HTTP/1.1 %d", &http_response->status_code) != 1)
   {
      pgexporter_log_error("Failed to parse HTTP status code");
      goto error;
   }

   free(response_copy);
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
               colon = strchr(p, ':');
               if (colon != NULL)
               {
                  *colon = '\0';
                  name = p;
                  value = colon + 1;

                  while (*value == ' ' || *value == '\t')
                  {
                     value++;
                  }

                  size_t len = strlen(value);
                  while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == '\n'))
                  {
                     value[len - 1] = '\0';
                     len--;
                  }

                  if (pgexporter_deque_add(http_response->payload.headers, name, (uintptr_t)value, ValueString))
                  {
                     pgexporter_log_warn("Failed to add response header: %s", name);
                  }
               }
            }
            else
            {
               http_response->payload.data = pgexporter_append((char*)http_response->payload.data, p);
               http_response->payload.data = pgexporter_append_char((char*)http_response->payload.data, '\n');
            }
         }
      }

      p = strtok(NULL, "\n");
   }

   if (http_response->payload.data != NULL)
   {
      http_response->payload.data_size = strlen((char*)http_response->payload.data);
   }

   free(response_copy);
   return PGEXPORTER_HTTP_STATUS_OK;

error:
   free(response_copy);
   return PGEXPORTER_HTTP_STATUS_ERROR;
}

static int
http_build_request(struct http* connection, struct http_request* request, char** full_request, size_t* full_request_size)
{
   char* method_str = NULL;
   char* request_line = NULL;
   char* headers = NULL;
   char* user_agent = NULL;
   char content_length[32];
   size_t header_len = 0;
   size_t total_len = 0;

   if (connection == NULL || request == NULL || full_request == NULL || full_request_size == NULL)
   {
      pgexporter_log_error("Invalid parameters for HTTP build request");
      goto error;
   }

   method_str = http_method_to_string(request->method);
   if (method_str == NULL)
   {
      pgexporter_log_error("Invalid HTTP method: %d", request->method);
      goto error;
   }

   request_line = pgexporter_append(request_line, method_str);
   request_line = pgexporter_append(request_line, " ");
   request_line = pgexporter_append(request_line, request->path);
   request_line = pgexporter_append(request_line, " HTTP/1.1\r\n");

   headers = pgexporter_append(headers, "Host: ");
   headers = pgexporter_append(headers, connection->hostname);
   headers = pgexporter_append(headers, "\r\n");

   user_agent = pgexporter_append(user_agent, "pgexporter/");
   user_agent = pgexporter_append(user_agent, VERSION);
   headers = pgexporter_append(headers, "User-Agent: ");
   headers = pgexporter_append(headers, user_agent);
   headers = pgexporter_append(headers, "\r\n");

   headers = pgexporter_append(headers, "Connection: close\r\n");

   sprintf(content_length, "%zu", request->payload.data_size);
   headers = pgexporter_append(headers, "Content-Length: ");
   headers = pgexporter_append(headers, content_length);
   headers = pgexporter_append(headers, "\r\n");

   if (request->method == PGEXPORTER_HTTP_POST)
   {
      headers = pgexporter_append(headers, "Content-Type: application/x-www-form-urlencoded\r\n");
   }
   else if (request->method == PGEXPORTER_HTTP_PUT)
   {
      headers = pgexporter_append(headers, "Content-Type: application/octet-stream\r\n");
   }

   if (request->payload.headers != NULL && !pgexporter_deque_empty(request->payload.headers))
   {
      struct deque_iterator* iter = NULL;
      if (pgexporter_deque_iterator_create(request->payload.headers, &iter) == 0)
      {
         while (pgexporter_deque_iterator_next(iter))
         {
            headers = pgexporter_append(headers, iter->tag);
            headers = pgexporter_append(headers, ": ");
            headers = pgexporter_append(headers, (char*)pgexporter_value_data(iter->value));
            headers = pgexporter_append(headers, "\r\n");
         }
         pgexporter_deque_iterator_destroy(iter);
      }
   }

   headers = pgexporter_append(headers, "\r\n");

   header_len = strlen(request_line) + strlen(headers);
   total_len = header_len + request->payload.data_size;

   *full_request = malloc(total_len + 1);
   if (*full_request == NULL)
   {
      pgexporter_log_error("Failed to allocate memory for full request");
      goto error;
   }

   memcpy(*full_request, request_line, strlen(request_line));
   memcpy(*full_request + strlen(request_line), headers, strlen(headers));

   if (request->payload.data_size > 0)
   {
      memcpy(*full_request + header_len, request->payload.data, request->payload.data_size);
   }

   (*full_request)[total_len] = '\0';
   *full_request_size = total_len;

   free(request_line);
   free(headers);
   free(user_agent);

   return PGEXPORTER_HTTP_STATUS_OK;

error:
   free(request_line);
   free(headers);
   free(user_agent);

   return PGEXPORTER_HTTP_STATUS_ERROR;
}

static char*
http_method_to_string(int method)
{
   switch (method)
   {
      case PGEXPORTER_HTTP_GET:
         return "GET";
      case PGEXPORTER_HTTP_POST:
         return "POST";
      case PGEXPORTER_HTTP_PUT:
         return "PUT";
      default:
         return NULL;
   }
}