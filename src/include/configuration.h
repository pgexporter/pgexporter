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

#ifndef PGEXPORTER_CONFIGURATION_H
#define PGEXPORTER_CONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <json.h>

#include <stdlib.h>

#define CONFIGURATION_ARGUMENT_HOST                   "host"
#define CONFIGURATION_ARGUMENT_UNIX_SOCKET_DIR        "unix_socket_dir"
#define CONFIGURATION_ARGUMENT_METRICS                "metrics"
#define CONFIGURATION_ARGUMENT_METRICS_PATH           "metrics_path"
#define CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE  "metrics_cache_max_age"
#define CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE "metrics_cache_max_size"
#define CONFIGURATION_ARGUMENT_BRIDGE                 "bridge"
#define CONFIGURATION_ARGUMENT_BRIDGE_ENDPOINTS       "bridge_endpoints"
#define CONFIGURATION_ARGUMENT_BRIDGE_CACHE_MAX_AGE   "bridge_cache_max_age"
#define CONFIGURATION_ARGUMENT_BRIDGE_CACHE_MAX_SIZE  "bridge_cache_max_size"
#define CONFIGURATION_ARGUMENT_MANAGEMENT             "management"
#define CONFIGURATION_ARGUMENT_CACHE                  "cache"
#define CONFIGURATION_ARGUMENT_LOG_TYPE               "log_type"
#define CONFIGURATION_ARGUMENT_LOG_LEVEL              "log_level"
#define CONFIGURATION_ARGUMENT_LOG_PATH               "log_path"
#define CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE       "log_rotation_age"
#define CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE      "log_rotation_size"
#define CONFIGURATION_ARGUMENT_LOG_LINE_PREFIX        "log_line_prefix"
#define CONFIGURATION_ARGUMENT_LOG_MODE               "log_mode"
#define CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT       "blocking_timeout"
#define CONFIGURATION_ARGUMENT_TLS                    "tls"
#define CONFIGURATION_ARGUMENT_TLS_CERT_FILE          "tls_cert_file"
#define CONFIGURATION_ARGUMENT_TLS_KEY_FILE           "tls_key_file"
#define CONFIGURATION_ARGUMENT_TLS_CA_FILE            "tls_ca_file"
#define CONFIGURATION_ARGUMENT_LIBEV                  "libev"
#define CONFIGURATION_ARGUMENT_KEEP_ALIVE             "keep_alive"
#define CONFIGURATION_ARGUMENT_NODELAY                "nodelay"
#define CONFIGURATION_ARGUMENT_NON_BLOCKING           "non_blocking"
#define CONFIGURATION_ARGUMENT_BACKLOG                "backlog"
#define CONFIGURATION_ARGUMENT_HUGEPAGE               "hugepage"
#define CONFIGURATION_ARGUMENT_PIDFILE                "pidfile"
#define CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE   "update_process_title"
#define CONFIGURATION_ARGUMENT_PORT                   "port"
#define CONFIGURATION_ARGUMENT_USER                   "user"
#define CONFIGURATION_ARGUMENT_DATA_DIR               "data_dir"
#define CONFIGURATION_ARGUMENT_WAL_DIR                "wal_dir"
#define CONFIGURATION_ARGUMENT_MAIN_CONF_PATH         "main_configuration_path"
#define CONFIGURATION_ARGUMENT_USER_CONF_PATH         "users_configuration_path"
#define CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH        "admin_configuration_path"

/**
 * Initialize the configuration structure
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_init_configuration(void* shmem);

/**
 * Read the configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_configuration(void* shmem, char* filename);

/**
 * Validate the configuration
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_validate_configuration(void* shmem);

/**
 * Read the USERS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_users_configuration(void* shmem, char* filename);

/**
 * Validate the USERS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_validate_users_configuration(void* shmem);

/**
 * Read the ADMINS configuration from a file
 * @param shmem The shared memory segment
 * @param filename The file name
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_read_admins_configuration(void* shmem, char* filename);

/**
 * Validate the ADMINS configuration from a file
 * @param shmem The shared memory segment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_validate_admins_configuration(void* shmem);

/**
 * Reload the configuration
 * @param reload Should the server be reloaded
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_reload_configuration(bool* reload);

/**
 * Get a configuration parameter value
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgexporter_conf_get(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Set a configuration parameter value
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgexporter_conf_set(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

#ifdef __cplusplus
}
#endif

#endif
