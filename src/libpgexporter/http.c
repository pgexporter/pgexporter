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
#include <http.h>
#include <logging.h>
#include <memory.h>
#include <utils.h>

#include <curl/curl.h>

static size_t write_header_callback(char* ptr, size_t size, size_t nmemb, struct http* http);
static size_t write_body_callback(char* ptr, size_t size, size_t nmemb, struct http* http);
 
static int basic_settings(struct http* http, int type);

int
pgexporter_http_create(char* url, struct http** http)
{
   struct http* h = NULL;

   h = (struct http*)malloc(sizeof(struct http));

   if (h == NULL)
   {
      goto error;
   }

   h->curl = curl_easy_init();
   if (h->curl == NULL)
   {
      goto error;
   }

   h->headers = NULL;

   h->url = (char*)pgexporter_memory_dynamic_create(&h->url_length);
   h->url = (char*)pgexporter_memory_dynamic_append(h->url, h->url_length, (void*)url, strlen(url), &h->url_length); 
   h->url = (char*)pgexporter_memory_dynamic_append(h->url, h->url_length, (void*)'\0', (size_t)1, &h->url_length); 
 
   h->header = (char*)pgexporter_memory_dynamic_create(&h->header_length);
   h->body = (char*)pgexporter_memory_dynamic_create(&h->body_length);

   *http = h;

   return 0;

error:

   return 1;
}

int
pgexporter_http_add_header(struct http* http, char* header, char* value)
{
   char* h = NULL;

   if (http == NULL)
   {
      goto error;
   }

   h = pgexporter_append(h, header);
   h = pgexporter_append(h, ": ");
   h = pgexporter_append(h, value);

   http->headers = curl_slist_append(http->headers, h);

   free(h);

   return 0;

error:

   return 1;
}

int
pgexporter_http_get(struct http* http)
{
   CURLcode cres;

   if (http == NULL)
   {
      goto error;
   }

   if (basic_settings(http, HTTP_GET))
   {
      goto error;
   }

   cres = curl_easy_perform(http->curl);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_http_put(struct http* http)
{
   CURLcode cres;

   if (http == NULL)
   {
      goto error;
   }

   if (basic_settings(http, HTTP_PUT))
   {
      goto error;
   }

   cres = curl_easy_perform(http->curl);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_http_post(struct http* http)
{
   CURLcode cres;

   if (http == NULL)
   {
      goto error;
   }

   if (basic_settings(http, HTTP_POST))
   {
      goto error;
   }

   cres = curl_easy_perform(http->curl);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

void
pgexporter_http_log(struct http* http)
{
   if (http == NULL)
   {
      pgexporter_log_debug("HTTP is NULL");
   }
   else
   {
      pgexporter_log_debug("HTTP: %p", http);

      if (http->url == NULL)
      {
         pgexporter_log_debug("URL is NULL");
      }
      else
      {
         pgexporter_log_mem(http->url, http->url_length);
      }

      if (http->header == NULL)
      {
         pgexporter_log_debug("Header is NULL");
      }
      else
      {
         pgexporter_log_mem(http->header, http->header_length);
      }

      if (http->body == NULL)
      {
         pgexporter_log_debug("Body is NULL");
      }
      else
      {
         pgexporter_log_mem(http->body, http->body_length);
      }
   }
}

int
pgexporter_http_destroy(struct http* http)
{
   if (http != NULL)
   {
      curl_easy_cleanup(http->curl);

      free(http->url);
      free(http->header);
      free(http->body);
   }
   free(http);

   return 0;
}

static size_t
write_header_callback(char* ptr, size_t size, size_t nmemb, struct http* http)
{
   http->header = (char*)pgexporter_memory_dynamic_append(http->header, http->header_length, (void*)ptr, (size_t)(size * nmemb), &http->header_length); 
   http->header = (char*)pgexporter_memory_dynamic_append(http->header, http->header_length, (void*)'\0', (size_t)1, &http->header_length); 

   return size * nmemb;
}

static size_t
write_body_callback(char* ptr, size_t size, size_t nmemb, struct http* http)
{
   http->body = (char*)pgexporter_memory_dynamic_append(http->body, http->body_length, (void*)ptr, (size_t)(size * nmemb), &http->body_length); 
   http->body = (char*)pgexporter_memory_dynamic_append(http->body, http->body_length, (void*)'\0', (size_t)1, &http->body_length); 

   return size * nmemb;
}

static int
basic_settings(struct http* http, int type)
{
   CURLcode cres = 1;

   http->type = type;
   if (type == HTTP_GET)
   {
      cres = curl_easy_setopt(http->curl, CURLOPT_HTTPGET, 1L);

      cres = curl_easy_setopt(http->curl, CURLOPT_HTTPGET, 1L);
      if (cres != CURLE_OK)
      {
         goto error;
      }
   }
   else if (type == HTTP_PUT)
   {
      cres = curl_easy_setopt(http->curl, CURLOPT_UPLOAD, 1L);
   }
   else if (type == HTTP_POST)
   {
      cres = curl_easy_setopt(http->curl, CURLOPT_UPLOAD, 1L);
   }
   if (cres != CURLE_OK)
   {
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_URL, http->url);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_HEADER, 1L);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_HTTPHEADER, http->headers);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_HEADERFUNCTION, write_header_callback);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, write_body_callback);
   if (cres != CURLE_OK)
   {
      goto error;
   }

   return 0;

error:

   return 1;
}
