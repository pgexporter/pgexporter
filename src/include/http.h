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

#ifndef PGEXPORTER_HTTP_H
#define PGEXPORTER_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdio.h>
#include <curl/curl.h>

#define HTTP_GET  0
#define HTTP_PUT  1
#define HTTP_POST 2

/** @struct http
 * Defines a HTTP interaction
 */
struct http
{
   CURL* curl;                 /**< The CURL backup */
   struct curl_slist* headers; /**< The HTTP header options */
   int type;                   /**< The HTTP type (GET, PUT, POST) */
   char* url;                  /**< The URL */
   size_t url_length;          /**< The URL length */
   char* header;               /**< The HTTP header */
   size_t header_length;       /**< The HTTP header length */
   char* body;                 /**< The HTTP body */
   size_t body_length;         /**< The HTTP body length */
};

/**
 * Create a HTTP interaction a header
 * @param url The URL
 * @param http The resulting HTTP interaction
 * @return 0 if success, otherwise 1
 */
int
pgexporter_http_create(char* url, struct http** http);

/**
 * Add a HTTP header
 * @param http The HTTP interaction
 * @param header The header
 * @param value The value
 * @return 0 if success, otherwise 1
 */
int
pgexporter_http_add_header(struct http* http, char* header, char* value);

/**
 * Execute GET request
 * @param http The HTTP interaction
 * @return 0 if success, otherwise 1
 */
int
pgexporter_http_get(struct http* http);
   
/**
 * Execute PUT request
 * @param http The HTTP interaction
 * @return 0 if success, otherwise 1
 */
int
pgexporter_http_put(struct http* http);
   
/**
 * Execute POST request
 * @param http The HTTP interaction
 * @return 0 if success, otherwise 1
 */
int
pgexporter_http_post(struct http* http);

/**
 * Destroy HTTP interaction
 * @param http The HTTP interaction
 * @return 0 if success, otherwise 1
 */
int
pgexporter_http_destroy(struct http* http);
   
#ifdef __cplusplus
}
#endif

#endif
