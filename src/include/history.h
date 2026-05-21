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

#ifndef PGEXPORTER_HISTORY_H
#define PGEXPORTER_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @struct history_record
 * @brief Stored metric sample for the history backend.
 */
struct history_record
{
   time_t ts;                      /**< Unix timestamp of the snapshot */
   char server[MISC_LENGTH];       /**< Server name */
   char metric[PROMETHEUS_LENGTH]; /**< Metric name */
   char labels[MAX_PATH];          /**< Serialized label set (key=val,...) */
   double value;                   /**< Metric value */
};

/**
 * Virtual function table for a history storage backend.
 * Every backend must provide one static instance of this struct and expose it
 * so that history.c can register it in the backend registry.
 */
struct history_backend_ops
{
   int (*init)(void);                                             /**< Initialize backend resources. */
   int (*write_batch)(struct history_record* records, int count); /**< Persist a batch of history records. */
   int (*query_range)(const char* metric, time_t start, time_t end,
                      struct history_record** out, int* count_out); /**< Query records for a metric and time range. */
   int (*prune)(void);                                              /**< Remove records older than configured retention. */
   int (*shutdown)(void);                                           /**< Release backend resources. */
};

/**
 * Open the backend connection/file and create the schema if needed.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_init(void);

/**
 * Write a batch of records inside a single transaction.
 * @param records  Array of history_record structs
 * @param count    Number of records in the array
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_write_batch(struct history_record* records, int count);

/**
 * Retrieve records for a given metric within a time window.
 * @param metric      Metric name to query
 * @param start       Start of the time window
 * @param end         End of the time window
 * @param records_out Caller-allocated array to fill (may be NULL to count only)
 * @param count_out   Set to the number of records returned
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_query_range(const char* metric, time_t start, time_t end,
                               struct history_record** records_out, int* count_out);

/**
 * Delete records older than the configured retention threshold.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_prune(void);

/**
 * Close the backend connection/file.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_shutdown(void);

/**
 * Periodic callback: fork a history worker to snapshot current metrics.
 * Skipped if a previous worker is still running.
 */
void
pgexporter_history_tick_cb(void);

/**
 * Periodic callback: fork a retention worker to prune old records.
 */
void
pgexporter_history_retention_tick_cb(void);

#ifdef __cplusplus
}
#endif

#endif
