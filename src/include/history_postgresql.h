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

#ifndef PGEXPORTER_HISTORY_POSTGRESQL_H
#define PGEXPORTER_HISTORY_POSTGRESQL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <history.h>
#include <time.h>

/**
 * Create the schema in the history store if it does not exist yet.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_postgresql_create(void);

/**
 * Connect to the history store. Does nothing when already connected.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_postgresql_init(void);

/**
 * Insert a batch of records inside a single transaction.
 * @param records Array of history_record structs
 * @param count   Number of records
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_postgresql_write_batch(struct history_record* records, int count);

/**
 * Query records for a metric within [start, end].
 * @param metric      Metric name
 * @param start       Start timestamp (inclusive)
 * @param end         End timestamp (inclusive)
 * @param records_out Pointer to a caller-freeable array of results; may be NULL
 * @param count_out   Number of returned records
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_postgresql_query_range(const char* metric, time_t start, time_t end,
                                          struct history_record** records_out, int* count_out);

/**
 * Delete samples whose timestamp is older than config->history_retention, then
 * remove any series left with no samples at all.
 *
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_postgresql_prune(void);

/**
 * Close the connection to the history store.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_postgresql_shutdown(void);

extern const struct history_backend_ops pgexporter_history_postgresql_ops;

#ifdef __cplusplus
}
#endif

#endif
