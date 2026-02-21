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
 */

/* pgexporter */
#include <pgexporter.h>
#include <cache.h>
#include <logging.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int
pgexporter_cache_init(size_t cache_size, size_t* p_size, void** p_shmem)
{
   struct prometheus_cache* cache;
   struct configuration* config;
   size_t struct_size = 0;

   config = (struct configuration*)shmem;
   struct_size = sizeof(struct prometheus_cache);

   if (pgexporter_create_shared_memory(struct_size + cache_size, config->hugepage, (void*)&cache))
   {
      goto error;
   }

   memset(cache, 0, struct_size + cache_size);
   cache->valid_until = 0;
   cache->size = cache_size;
   atomic_init(&cache->lock, STATE_FREE);

   *p_shmem = cache;
   *p_size = cache_size + struct_size;

   return 0;

error:
   *p_size = 0;
   *p_shmem = NULL;

   return 1;
}

bool
pgexporter_cache_is_valid(struct prometheus_cache* cache)
{
   time_t now;

   if (cache == NULL || cache->valid_until == 0 || strlen(cache->data) == 0)
   {
      return false;
   }

   now = time(NULL);
   return now <= cache->valid_until;
}

void
pgexporter_cache_invalidate(struct prometheus_cache* cache)
{
   if (cache == NULL)
   {
      return;
   }

   memset(cache->data, 0, cache->size);
   cache->valid_until = 0;
}

bool
pgexporter_cache_append(struct prometheus_cache* cache, char* data)
{
   size_t origin_length = 0;
   size_t append_length = 0;

   if (cache == NULL || data == NULL)
   {
      return false;
   }

   origin_length = strlen(cache->data);
   append_length = strlen(data);

   if (origin_length + append_length >= cache->size)
   {
      pgexporter_log_debug("Cannot append %d bytes to the cache because it will overflow the size of %d bytes (currently at %d bytes).",
                           append_length,
                           cache->size,
                           origin_length);
      pgexporter_cache_invalidate(cache);
      return false;
   }

   memcpy(cache->data + origin_length, data, append_length);
   cache->data[origin_length + append_length] = '\0';

   return true;
}

bool
pgexporter_cache_finalize(struct prometheus_cache* cache, pgexporter_time_t max_age)
{
   time_t now;

   if (cache == NULL)
   {
      return false;
   }

   now = time(NULL);
   cache->valid_until = now + pgexporter_time_convert(max_age, FORMAT_TIME_S);

   return cache->valid_until > now;
}
