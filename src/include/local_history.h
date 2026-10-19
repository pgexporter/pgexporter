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

#ifndef PGEXPORTER_LOCAL_HISTORY_H
#define PGEXPORTER_LOCAL_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <history.h>

#include <openssl/ssl.h>

struct prometheus_metrics_container;

/**
 * Describe the local history store from the current history_* configuration.
 * @return The store, or NULL without a configuration
 */
const struct history_store*
pgexporter_local_history_store(void);

/**
 * Persist a metrics container as one history snapshot.
 * The local history store must already be opened via pgexporter_history_init().
 *
 * @param container The metrics container
 * @return 0 on success, 1 on failure
 */
int
pgexporter_local_history_store_metrics(struct prometheus_metrics_container* container);

/**
 * Periodic callback: fork a worker to scrape local metrics and snapshot them.
 * Skipped if a previous worker is still running.
 */
void
pgexporter_local_history_tick_cb(void);

/**
 * Periodic callback: fork a retention worker to prune old local history records.
 */
void
pgexporter_local_history_retention_tick_cb(void);

/**
 * HTTP entry point for the local history JSON API.
 * Serves the same routes as pgexporter_bridge_history_http() from the local history store.
 *
 * @param ssl The SSL connection, or NULL for plain HTTP
 * @param fd  The client socket file descriptor
 */
void
pgexporter_local_history_http(SSL* ssl, int fd);

#ifdef __cplusplus
}
#endif

#endif
