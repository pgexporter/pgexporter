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

#ifndef PGEXPORTER_CACHE_H
#define PGEXPORTER_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <stdlib.h>

/**
 * Initialize a prometheus cache in shared memory.
 * @param cache_size The size of the cache data payload
 * @param p_size Pointer to store the total allocated size
 * @param p_shmem Pointer to store the shared memory pointer
 * @return 0 on success, otherwise 1
 */
int
pgexporter_cache_init(size_t cache_size, size_t* p_size, void** p_shmem);

/**
 * Check if the cache is still valid.
 * A cache is valid if it has a non-empty payload and
 * a timestamp in the future.
 * @param cache The cache
 * @return true if the cache is still valid
 */
bool
pgexporter_cache_is_valid(struct prometheus_cache* cache);

/**
 * Invalidate the cache.
 * The payload is zero-filled and the valid_until field
 * is set to zero.
 * Requires the caller to hold the lock on the cache.
 * @param cache The cache
 */
void
pgexporter_cache_invalidate(struct prometheus_cache* cache);

/**
 * Append data to the cache.
 * If the cache would overflow, it is invalidated instead.
 * Requires the caller to hold the lock on the cache.
 * @param cache The cache
 * @param data The data to append
 * @return true if the data was appended, otherwise false
 */
bool
pgexporter_cache_append(struct prometheus_cache* cache, char* data);

/**
 * Finalize the cache by setting its expiry time.
 * Requires the caller to hold the lock on the cache.
 * @param cache The cache
 * @param max_age The maximum age of the cache
 * @return true if the cache was finalized, otherwise false
 */
bool
pgexporter_cache_finalize(struct prometheus_cache* cache, pgexporter_time_t max_age);

#ifdef __cplusplus
}
#endif

#endif
