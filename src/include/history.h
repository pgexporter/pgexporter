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
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <openssl/ssl.h>

/** Bytes of the SHA-256 digest used for the canonical label hash: the full 256. */
#define HISTORY_LABEL_HASH_BYTES 32

/**
 * Length of the canonical label hash rendered as lowercase hex, including the
 * terminating NUL.
 */
#define HISTORY_LABEL_HASH_LENGTH ((HISTORY_LABEL_HASH_BYTES * 2) + 1)

/**
 * @struct history_record
 * @brief Stored metric sample for the history backend.
 */
struct history_record
{
   time_t ts;                      /**< Unix timestamp of the snapshot */
   char server[MISC_LENGTH];       /**< Server name */
   char metric[PROMETHEUS_LENGTH]; /**< Metric name */
   char* labels;                   /**< Serialized label set (key=val,...). May be NULL.
                                        Records returned by query_range own this string — release
                                        the whole array with pgexporter_history_records_free(). */
   double value;                   /**< Metric value */
};

/**
 * Compute the canonical hash of a Prometheus label set.
 *
 * The label string is the text between the braces of an exposition line, for
 * example `server="primary",database="postgres"`. Pairs are sorted by key
 * before hashing, so emission order does not change a series' identity.
 * Input that does not parse is hashed verbatim, which is still stable.
 *
 * @param labels The label string, or NULL/empty for a series with no labels
 * @param out    Buffer of at least HISTORY_LABEL_HASH_LENGTH bytes
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_label_hash(const char* labels, char* out);

/**
 * Look up one label's value in a Prometheus label string.
 *
 * @param labels   The label string, or NULL
 * @param key      The label name to find
 * @param out      Buffer receiving the value, NUL-terminated and truncated to fit
 * @param out_size Size of out in bytes
 * @return true if the key was found, otherwise false
 */
bool
pgexporter_history_label_find(const char* labels, const char* key, char* out, size_t out_size);

/**
 * Free an array of history records returned by pgexporter_history_query_range,
 * including each record's heap-allocated labels string.
 *
 * @param records The records array (may be NULL)
 * @param count The number of records in the array
 */
void
pgexporter_history_records_free(struct history_record* records, int count);

/**
 * @struct history_store
 * One history store: its settings and the shared state of its workers.
 */
struct history_store
{
   const char* name;                                   /**< Log prefix */
   int port;                                           /**< Port of the history JSON API */
   int backend;                                        /**< HISTORY_BACKEND_* id */
   const char* path;                                   /**< SQLite file path */
   char* pg_host;                                      /**< PostgreSQL host, or a Unix socket directory */
   int pg_port;                                        /**< PostgreSQL port, or the Unix socket number */
   char* pg_database;                                  /**< PostgreSQL database */
   char* pg_user;                                      /**< PostgreSQL role */
   char* pg_password;                                  /**< PostgreSQL password; empty for trust or peer */
   int pg_tls;                                         /**< PostgreSQL TLS mode (SERVER_TLS_*) */
   char* pg_tls_cert_file;                             /**< PostgreSQL client certificate */
   char* pg_tls_key_file;                              /**< PostgreSQL client key */
   char* pg_tls_ca_file;                               /**< PostgreSQL CA file */
   pgexporter_time_t interval;                         /**< Minimum time between snapshots */
   pgexporter_time_t retention;                        /**< How long records are kept */
   atomic_bool* running;                               /**< Snapshot single-flight flag */
   atomic_int_least64_t* last_store;                   /**< Time of the last snapshot */
   atomic_int* worker_pid;                             /**< PID of the snapshot worker */
   atomic_bool* retention_running;                     /**< Retention single-flight flag */
   atomic_int* retention_pid;                          /**< PID of the retention worker */
   void (*collect)(const struct history_store* store); /**< Scrape and store one snapshot in a worker */
};

/**
 * Virtual function table for a history storage backend.
 * Every backend must provide one static instance of this struct and expose it
 * so that history.c can register it in the backend registry.
 */
struct history_backend_ops
{
   int (*create)(const struct history_store* store);              /**< Create the store: schema or file. */
   int (*init)(const struct history_store* store);                /**< Open the store for this process. */
   int (*write_batch)(struct history_record* records, int count); /**< Persist a batch of history records. */
   int (*query_range)(const char* metric, time_t start, time_t end,
                      struct history_record** out, int* count_out); /**< Query records for a metric and time range. */
   int (*prune)(const struct history_store* store);                 /**< Remove records older than the store's retention. */
   int (*shutdown)(void);                                           /**< Release backend resources. */
};

/**
 * Create the store, meaning the schema or the file, if it does not exist yet.
 * Called once at startup, before any worker is forked, and leaves the store closed
 * unless it was already open.
 * @param store The store, may be NULL
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_create(const struct history_store* store);

/**
 * Open the backend connection/file. The store must already have been created.
 * Write, query and shutdown calls then act on this store.
 * @param store The store, may be NULL
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_init(const struct history_store* store);

/**
 * Write a batch of records inside a single transaction, and record the time as
 * the last snapshot of the store opened by pgexporter_history_init().
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
 * Delete records older than the store's retention.
 * @param store The store opened by pgexporter_history_init()
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_prune(const struct history_store* store);

/**
 * Close the backend connection/file.
 * @return 0 on success, 1 on failure
 */
int
pgexporter_history_shutdown(void);

/**
 * Claim the exclusive right to write a snapshot to a store.
 *
 * Fails if another writer already holds the slot, or if the current
 * interval bucket already contains a snapshot. On success the caller
 * must invoke pgexporter_history_release_slot() once it is done.
 *
 * @param store    The store
 * @param interval Minimum seconds between snapshots, or -1 to snapshot immediately
 * @return true if the slot was claimed, otherwise false
 */
bool
pgexporter_history_claim_slot(const struct history_store* store, int interval);

/**
 * Release a slot claimed by pgexporter_history_claim_slot().
 * @param store The store
 */
void
pgexporter_history_release_slot(const struct history_store* store);

/**
 * Fork a worker that runs the store's collect callback.
 * Skipped if a previous worker is still running or the interval bucket is taken.
 * @param store The store, may be NULL
 */
void
pgexporter_history_tick(const struct history_store* store);

/**
 * Fork a worker that prunes the store.
 * Skipped if a previous retention worker is still running.
 * @param store The store, may be NULL
 */
void
pgexporter_history_retention_tick(const struct history_store* store);

/**
 * Serve one request on the history JSON API of a store.
 *
 * Serves GET /history/<metric_name>?timestamp=<epoch>&duration=<seconds>.
 * Both query parameters are optional; timestamp defaults to now and duration
 * defaults to -3600. duration may be negative, in which case
 * the queried window ends at timestamp and starts duration seconds earlier.
 * Called in a forked child; exits.
 *
 * @param store The store, may be NULL
 * @param ssl   The SSL connection, or NULL for plain HTTP
 * @param fd    The client socket file descriptor
 */
void
pgexporter_history_serve(const struct history_store* store, SSL* ssl, int fd);

#ifdef __cplusplus
}
#endif

#endif
