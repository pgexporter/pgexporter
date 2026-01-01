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

#ifndef PGEXPORTER_BRIDGE_H
#define PGEXPORTER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/*
 * Value to disable the bridge cache,
 * it is equivalent to set `bridge_cache`
 * to 0 (seconds).
 */
#define PROMETHEUS_BRIDGE_CACHE_DISABLED 0

/**
 * Default size of the cache (in bytes).
 * If the cache request exceeds this size
 * the caching should be aborted in some way.
 */
#define PROMETHEUS_DEFAULT_BRIDGE_CACHE_SIZE (10 * 1024 * 1024)

/**
 * Max size of the cache (in bytes).
 * If the cache request exceeds this size
 * the caching should be aborted in some way.
 */
#define PROMETHEUS_MAX_BRIDGE_CACHE_SIZE (1 * 1024 * 1024 * 1024)

/**
 * Default size of the cache (in bytes).
 * If the cache request exceeds this size
 * the caching should be aborted in some way.
 */
#define PROMETHEUS_DEFAULT_BRIDGE_JSON_CACHE_SIZE (10 * 1024 * 1024)

/**
 * Max size of the cache (in bytes).
 * If the cache request exceeds this size
 * the caching should be aborted in some way.
 */
#define PROMETHEUS_MAX_BRIDGE_JSON_CACHE_SIZE (1 * 1024 * 1024 * 1024)

/**
 * Create a prometheus bridge
 * @param fd The client descriptor
 */
void
pgexporter_bridge(int fd);

/**
 * Allocates, for the first time, the bridge cache.
 *
 * The cache structure, as well as its dynamically sized payload,
 * are created as shared memory chunks.
 *
 * Assumes the shared memory for the cofiguration is already set.
 *
 * The cache will be allocated as soon as this method is invoked,
 * even if the cache has not been configured at all!
 *
 * If the memory cannot be allocated, the function issues errors
 * in the logs and disables the caching machinaery.
 *
 * @param p_size a pointer to where to store the size of
 * allocated chunk of memory
 * @param p_shmem the pointer to the pointer at which the allocated chunk
 * of shared memory is going to be inserted
 *
 * @return 0 on success, otherwise 1
 */
int
pgexporter_bridge_init_cache(size_t* p_size, void** p_shmem);

/**
 * Create a prometheus JSON bridge
 * @param fd The client descriptor
 */
void
pgexporter_bridge_json(int fd);

/**
 * Allocates, for the first time, the bridge JSON cache.
 *
 * The cache structure, as well as its dynamically sized payload,
 * are created as shared memory chunks.
 *
 * Assumes the shared memory for the cofiguration is already set.
 *
 * The cache will be allocated as soon as this method is invoked,
 * even if the cache has not been configured at all!
 *
 * If the memory cannot be allocated, the function issues errors
 * in the logs and disables the caching machinaery.
 *
 * @param p_size a pointer to where to store the size of
 * allocated chunk of memory
 * @param p_shmem the pointer to the pointer at which the allocated chunk
 * of shared memory is going to be inserted
 *
 * @return 0 on success, otherwise 1
 */
int
pgexporter_bridge_json_init_cache(size_t* p_size, void** p_shmem);

#ifdef __cplusplus
}
#endif

#endif
