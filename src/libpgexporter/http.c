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
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <utils.h>

#include <curl/curl.h>

#define HTTP_GET  0
#define HTTP_PUT  1
#define HTTP_POST 2

static size_t write_callback(char* ptr, size_t size, size_t nmemb, char* data);

static int basic_settings(struct http* http, int type);

#ifdef DEBUG

static int
http_interaction(CURL* curl, curl_infotype type, char* data, size_t size, void* userp)
{
   char* text = NULL;

   switch (type)
   {
      case CURLINFO_TEXT:
         text = pgexporter_append(text, "== Info");
         break;
      case CURLINFO_HEADER_OUT:
         text = pgexporter_append(text, "=> Send header");
         break;
      case CURLINFO_DATA_OUT:
         text = pgexporter_append(text, "=> Send data");
         break;
      case CURLINFO_SSL_DATA_OUT:
         text = pgexporter_append(text, "=> Send SSL data");
         break;
      case CURLINFO_HEADER_IN:
         text = pgexporter_append(text, "<= Recv header");
         break;
      case CURLINFO_DATA_IN:
         text = pgexporter_append(text, "<= Recv data");
         break;
      case CURLINFO_SSL_DATA_IN:
         text = pgexporter_append(text, "<= Recv SSL data");
         break;
      default:
         pgexporter_log_warn("Unknown type: %d", type);
   }

   pgexporter_log_debug("%s", text);
   pgexporter_log_mem(data, size);

   free(text);

   return 0;
}

#endif

int
pgexporter_http_create(char* url, struct http** http)
{
   struct http* h = NULL;

   *http = NULL;

   h = (struct http*)malloc(sizeof(struct http));

   if (h == NULL)
   {
      pgexporter_log_error("Failed to allocate to HTTP");
      goto error;
   }

   h->curl = curl_easy_init();
   if (h->curl == NULL)
   {
      pgexporter_log_error("Could not initialize CURL");
      goto error;
   }

   /* Initialize */
   h->url = NULL;
   h->headers = NULL;
   h->header = NULL;
   h->body = NULL;

   /* Fill in values */
   h->url = pgexporter_append(h->url, url);
   h->header = pgexporter_append(h->header, "");
   h->body = pgexporter_append(h->body, "");

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
   char* url;
   long response_code;
   double elapsed;

   if (http == NULL)
   {
      pgexporter_log_error("HTTP/GET is NULL");
      goto error;
   }

   if (basic_settings(http, HTTP_GET))
   {
      goto error;
   }

   pgexporter_log_trace("HTTP/GET interaction: %s", http->url);
   cres = curl_easy_perform(http->curl);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not perform HTTP/GET interaction: %s", curl_easy_strerror(cres));
      goto error;
   }

   curl_easy_getinfo(http->curl, CURLINFO_RESPONSE_CODE, &response_code);
   curl_easy_getinfo(http->curl, CURLINFO_TOTAL_TIME, &elapsed);
   curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &url);

   pgexporter_log_debug("HTTP/GET: %s -> %ld (%f)", url, response_code, elapsed);

   return 0;

error:

   return 1;
}

int
pgexporter_http_put(struct http* http)
{
   CURLcode cres;
   char* url;
   long response_code;
   double elapsed;

   if (http == NULL)
   {
      pgexporter_log_error("HTTP/PUT is NULL");
      goto error;
   }

   if (basic_settings(http, HTTP_PUT))
   {
      goto error;
   }

   pgexporter_log_trace("HTTP/PUT interaction: %s", http->url);
   cres = curl_easy_perform(http->curl);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not perform HTTP/PUT interaction");
      goto error;
   }

   curl_easy_getinfo(http->curl, CURLINFO_RESPONSE_CODE, &response_code);
   curl_easy_getinfo(http->curl, CURLINFO_TOTAL_TIME, &elapsed);
   curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &url);

   pgexporter_log_debug("HTTP/PUT: %s -> %ld (%f)", url, response_code, elapsed);

   return 0;

error:

   return 1;
}

int
pgexporter_http_post(struct http* http)
{
   CURLcode cres;
   char* url;
   long response_code;
   double elapsed;

   if (http == NULL)
   {
      pgexporter_log_error("HTTP/POST is NULL");
      goto error;
   }

   if (basic_settings(http, HTTP_POST))
   {
      goto error;
   }

   pgexporter_log_trace("HTTP/POST interaction: %s", http->url);
   cres = curl_easy_perform(http->curl);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not perform HTTP/POST interaction");
      goto error;
   }

   curl_easy_getinfo(http->curl, CURLINFO_RESPONSE_CODE, &response_code);
   curl_easy_getinfo(http->curl, CURLINFO_TOTAL_TIME, &elapsed);
   curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &url);

   pgexporter_log_debug("HTTP/POST: %s -> %ld (%f)", url, response_code, elapsed);

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
         pgexporter_log_mem(http->url, strlen(http->url));
      }

      if (http->header == NULL)
      {
         pgexporter_log_debug("Header is NULL");
      }
      else
      {
         pgexporter_log_mem(http->header, strlen(http->header) + 1);
      }

      if (http->body == NULL)
      {
         pgexporter_log_debug("Body is NULL");
      }
      else
      {
         pgexporter_log_mem(http->body, strlen(http->body) + 1);
      }
   }
}

int
pgexporter_http_destroy(struct http* http)
{
   if (http != NULL)
   {
      if (http->curl != NULL)
      {
         curl_easy_cleanup(http->curl);
      }
      http->curl = NULL;

      if (http->headers != NULL)
      {
         curl_slist_free_all(http->headers);
      }
      http->headers = NULL;

      free(http->url);
      free(http->header);
      free(http->body);
   }
   free(http);
   http = NULL;

   curl_global_cleanup();

   return 0;
}

static size_t
write_callback(char* ptr, size_t size, size_t nmemb, char* data)
{
   data = pgexporter_append(data, ptr);

   return size * nmemb;
}

static int
basic_settings(struct http* http, int type)
{
   CURLcode cres = 1;
   char agent[MISC_LENGTH];

   if (type == HTTP_GET)
   {
      cres = curl_easy_setopt(http->curl, CURLOPT_HTTPGET, 1L);
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
      pgexporter_log_error("Could not set type");
      goto error;
   }

#ifdef DEBUG
   cres = curl_easy_setopt(http->curl, CURLOPT_DEBUGFUNCTION, http_interaction);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set debug function");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_VERBOSE, 1L);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set verbose");
      goto error;
   }
#endif

   cres = curl_easy_setopt(http->curl, CURLOPT_URL, http->url);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set URL");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_FOLLOWLOCATION, 1L);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set follow location");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_NOPROGRESS, 1L);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set noprogress");
      goto error;
   }

   memset(&agent[0], 0, sizeof(agent));
   snprintf(&agent[0], sizeof(agent) - 1, "pgexporter/%s", VERSION);

   cres = curl_easy_setopt(http->curl, CURLOPT_USERAGENT, &agent[0]);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set useragent");
      goto error;
   }

   curl_easy_setopt(http->curl, CURLOPT_MAXREDIRS, 50L);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set max redirects");
      goto error;
   }

   curl_easy_setopt(http->curl, CURLOPT_TCP_KEEPALIVE, 1L);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set TCP keepalive");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_HEADER, 1L);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set header");
      goto error;
   }

   if (http->headers != NULL)
   {
      cres = curl_easy_setopt(http->curl, CURLOPT_HTTPHEADER, http->headers);
      if (cres != CURLE_OK)
      {
         pgexporter_log_error("Could not set headers");
         goto error;
      }
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_HEADERFUNCTION, write_callback);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set header write callback");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_WRITEFUNCTION, write_callback);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set write callback");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_WRITEDATA, http->body);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set body data");
      goto error;
   }

   cres = curl_easy_setopt(http->curl, CURLOPT_HEADERDATA, http->header);
   if (cres != CURLE_OK)
   {
      pgexporter_log_error("Could not set header data");
      goto error;
   }

   return 0;

error:

   return 1;
}
