/*
 * Copyright (C) 2021 Red Hat
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

#ifndef PGEXPORTER_QUERIES_H
#define PGEXPORTER_QUERIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <stdbool.h>
#include <stdlib.h>

struct tuple
{
   int server;              /**< The server */
   int columns;             /**< The number of columns */
   char tag[MISC_LENGTH];   /**< The tag */
   char name[MISC_LENGTH];  /**< The name */
   char value[MISC_LENGTH]; /**< The value */
   char desc[MISC_LENGTH];  /**< The description */
} __attribute__ ((aligned (64)));

struct tuples
{
   struct tuple* tuple;
   struct tuples* next;
};

/**
 * Open database connections
 */
void
pgexporter_open_connections(void);

/**
 * Close database connections
 */
void
pgexporter_close_connections(void);

/**
 * Query pg_database for size
 * @param server The server
 * @param tuples The resulting tuples
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_database_size(int server, struct tuples** tuples);

/**
 * Query pg_replication_slot for active
 * @param server The server
 * @param tuples The resulting tuples
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_replication_slot_active(int server, struct tuples** tuples);

/**
 * Query pg_settings
 * @param server The server
 * @param tuples The resulting tuples
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_settings(int server, struct tuples** tuples);

/**
 * Merge tuples
 * @param t1 The first tuples list
 * @param t2 The second tuples list
 * @return The resulting list
 */
struct tuples*
pgexporter_merge_tuples(struct tuples* t1, struct tuples* t2);

/**
 * Free tuples
 * @param tuples The tuples
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_free_tuples(struct tuples* tuples);

#ifdef __cplusplus
}
#endif

#endif
