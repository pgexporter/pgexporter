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

#ifndef PGEXPORTER_QUERIES_H
#define PGEXPORTER_QUERIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <stdbool.h>

/** @struct tuple
 * Defines a tuple
 */
struct tuple
{
   int server;                    /**< The server */
   char** data;                   /**< The data */
   struct tuple* next;            /**< The next tuple */
} __attribute__ ((aligned (64)));

/** @struct query
 * Defines a query
 */
struct query
{
   char tag[PROMETHEUS_LENGTH];                             /**< The tag */
   char names[MAX_NUMBER_OF_COLUMNS][PROMETHEUS_LENGTH];    /**< The column names */
   int number_of_columns;                                   /**< The number of columns */

   struct tuple* tuples;                                    /**< The tuples */
} __attribute__ ((aligned (64)));

/**
 * @struct query_alts_base
 * Base structure containing common fields for query alternatives.
 * Shared by both PostgreSQL and extension query types.
 *
 * Query Alternatives are alternative versions of queries with a PostgreSQL
 * version attached that will support the entire query. Ideally it should be the
 * **minimum** version that supports the entire query, but it can be any version
 * that supports the entire query.
 *
 * A query alternative node with version 'v' is chosen to provide the query if
 * the requesting server with version 'u' if 'u' >= 'v' and there doesn't exist
 * another node in the same AVL tree with a version 'w' where 'u' >= 'w'.
 */
struct query_alts_base
{
   char query[MAX_QUERY_LENGTH];                   /**< Query String */
   struct column columns[MAX_NUMBER_OF_COLUMNS];   /**< Columns of query */
   int n_columns;                                  /**< No. of columns */
   bool is_histogram;                              /**< Is the query for a histogram metric */

} __attribute__ ((aligned (64)));

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
 * Get functions
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_get_functions(int server, struct query** query);

/**
 * Execute query
 * @param server The server
 * @param sql The SQL query
 * @param tag The tag
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_execute(int server, char* sql, char* tag, struct query** query);

/**
 * Query for used disk space
 * @param server The server
 * @param data Data (true) or WAL (false)
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_used_disk_space(int server, bool data, struct query** query);

/**
 * Query for free disk space
 * @param server The server
 * @param data Data (true) or WAL (false)
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_free_disk_space(int server, bool data, struct query** query);

/**
 * Query for total disk space
 * @param server The server
 * @param data Data (true) or WAL (false)
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_total_disk_space(int server, bool data, struct query** query);

/**
 * Query PostgreSQL version
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_version(int server, struct query** query);

/**
 * Query PostgreSQL uptime
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_uptime(int server, struct query** query);

/**
 * Query PostgreSQL if it is primary
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_primary(int server, struct query** query);

/**
 * Query pg_database for size
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_database_size(int server, struct query** query);

/**
 * Query for installed extensions
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_extensions_list(int server, struct query** query);

/**
 * Query pg_replication_slot for active
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_replication_slot_active(int server, struct query** query);

/**
 * Query pg_locks
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_locks(int server, struct query** query);

/**
 * Query pg_stat_bgwriter
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_stat_bgwriter(int server, struct query** query);

/**
 * Query pg_stat_database
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_stat_database(int server, struct query** query);

/**
 * Query pg_stat_database_conflicts
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_stat_database_conflicts(int server, struct query** query);

/**
 * Query pg_settings
 * @param server The server
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_query_settings(int server, struct query** query);

/**
 * Query custom metrics
 * @param server The server
 * @param qs Query string
 * @param tag
 * @param columns
 * @param names
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_custom_query(int server, char* qs, char* tag, int columns, char** names, struct query** query);

/**
 * Merge queries
 * @param q1 The first query
 * @param q2 The second query
 * @param sort The sort key
 * @return The resulting list
 */
struct query*
pgexporter_merge_queries(struct query* q1, struct query* q2, int sort);

/**
 * Free allocated memory for tuples linked list
 * @param query The query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_free_tuples(struct tuple** tuples, int n_columns);

/**
 * Free query
 * @param query The query
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_free_query(struct query* query);

/**
 * Get column from a tuple
 * @param col The column
 * @param tuple The tuple
 * @return 0 upon success, otherwise 1
 */
char*
pgexporter_get_column(int col, struct tuple* tuple);

/**
 * Debug query
 * @param query The resulting query
 * @return 0 upon success, otherwise 1
 */
void
pgexporter_query_debug(struct query* query);

/**
 * Get column from a tuple by name
 * @param name The column name
 * @param query The query
 * @param tuple The tuple
 */
char*
pgexporter_get_column_by_name(char* name, struct query* query, struct tuple* tuple);

/**
 * Change connection to a database
 * @param server Server
 * @param database Database name (default: postgres)
 */
int
pgexporter_switch_db(int server, char* database);

#ifdef __cplusplus
}
#endif

#endif
