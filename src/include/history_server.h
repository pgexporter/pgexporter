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

#ifndef PGEXPORTER_HISTORY_SERVER_H
#define PGEXPORTER_HISTORY_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <history.h>
#include <queries.h>
#include <stdbool.h>
#include <time.h>

/**
 * Connect to the history server configured by history_postgresql_*.
 * Does nothing when already connected.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_init(void);

/**
 * Close the connection to the history server.
 */
void
pgexporter_history_server_shutdown(void);

/**
 * Report whether a connection to the history server is open.
 * @return true when connected, otherwise false
 */
bool
pgexporter_history_server_connected(void);

/**
 * Create the pgexporter schema and the series and sample tables.
 * The connection must already be open.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_create_schema(void);

/**
 * Insert a batch of records inside a single transaction.
 * @param records Array of history_record structs
 * @param count   Number of records
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_write_batch(struct history_record* records, int count);

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
pgexporter_history_server_query_range(const char* metric, time_t start, time_t end,
                                      struct history_record** records_out, int* count_out);

/**
 * Run a statement that returns rows. The caller frees query.
 * @param sql     The SQL text, using $1..$n placeholders
 * @param nparams The number of parameters
 * @param values  The parameter values as text; a NULL entry is SQL NULL
 * @param query   The resulting query
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_query(const char* sql, int nparams, char** values, struct query** query);

/**
 * Run a statement that returns rows and discard them.
 * @param sql     The SQL text, using $1..$n placeholders
 * @param nparams The number of parameters
 * @param values  The parameter values as text; a NULL entry is SQL NULL
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_execute(const char* sql, int nparams, char** values);

/**
 * Run a statement that does not return rows.
 * @param sql     The SQL text, using $1..$n placeholders
 * @param nparams The number of parameters
 * @param values  The parameter values as text; a NULL entry is SQL NULL
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_command(const char* sql, int nparams, char** values);

/**
 * Delete samples older than history_retention, then remove series left with none.
 * prune_sql takes the cutoff epoch as $1. When returns_rows is true the statement
 * is a query, such as drop_chunks, and its rows are discarded.
 * Returns 0 when retention is disabled or the connection is closed.
 * @param prune_sql    The delete statement
 * @param returns_rows Whether prune_sql returns a result set
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_server_prune(const char* prune_sql, bool returns_rows);

#ifdef __cplusplus
}
#endif

#endif
