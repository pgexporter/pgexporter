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

#ifndef PGEXPORTER_H
#define PGEXPORTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
#if HAVE_OPENBSD
#include <sys/limits.h>
#endif
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/ssl.h>
#include <sys/types.h>

#define VERSION "0.8.0"

#define PGEXPORTER_HOMEPAGE "https://pgexporter.github.io/"
#define PGEXPORTER_ISSUES "https://github.com/pgexporter/pgexporter/issues"

#define MAIN_UDS     ".s.pgexporter"
#define TRANSFER_UDS ".s.pgexporter.tu"

#define MAX_NUMBER_OF_COLUMNS 32

#define MAX_PROCESS_TITLE_LENGTH 256

#define DEFAULT_BUFFER_SIZE 131072

#define MAX_USERNAME_LENGTH  128
#define MAX_PASSWORD_LENGTH 1024
#define MAX_PASSWORD_CHARS   256

#define MAX_PATH             1024
#define MISC_LENGTH           128
#define PROMETHEUS_LENGTH     256
#define DB_NAME_LENGTH         64
#define NUMBER_OF_SERVERS      64
#define NUMBER_OF_USERS        64
#define NUMBER_OF_ADMINS        8
#define NUMBER_OF_METRICS     256
#define NUMBER_OF_COLLECTORS  256
#define NUMBER_OF_ENDPOINTS    32
#define NUMBER_OF_EXTENSIONS   64
#define NUMBER_OF_DATABASES    64
#define NUMBER_OF_METRIC_NAMES 1024

#define STATE_FREE        0
#define STATE_IN_USE      1

#define SERVER_UNKNOWN 0
#define SERVER_PRIMARY 1
#define SERVER_REPLICA 2

#define SERVER_TYPE_POSTGRESQL 0
#define SERVER_TYPE_PROMETHEUS 1

#define AUTH_SUCCESS      0
#define AUTH_BAD_PASSWORD 1
#define AUTH_ERROR        2
#define AUTH_TIMEOUT      3

#define HUGEPAGE_OFF 0
#define HUGEPAGE_TRY 1
#define HUGEPAGE_ON  2

#define VERSION_GREATER  1
#define VERSION_EQUAL    0
#define VERSION_LESS    -1
#define VERSION_ERROR   -2

#define MAX_QUERY_LENGTH      2048
#define MAX_COLLECTOR_LENGTH  1024
#define MAX_EXTENSIONS_CONFIG_LENGTH  2048

#define LABEL_TYPE      0
#define COUNTER_TYPE    1
#define GAUGE_TYPE      2
#define HISTOGRAM_TYPE  3

#define SORT_NAME  0
#define SORT_DATA0 1

#define SERVER_QUERY_BOTH    0  /* Default */
#define SERVER_QUERY_PRIMARY 1
#define SERVER_QUERY_REPLICA 2

#define SERVER_UNDERTERMINED_VERSION 0

#define ENCRYPTION_NONE         0
#define ENCRYPTION_AES_256_CBC  1
#define ENCRYPTION_AES_192_CBC  2
#define ENCRYPTION_AES_128_CBC  3
#define ENCRYPTION_AES_256_CTR  4
#define ENCRYPTION_AES_192_CTR  5
#define ENCRYPTION_AES_128_CTR  6

#define COMPRESSION_NONE         0
#define COMPRESSION_CLIENT_GZIP  1
#define COMPRESSION_CLIENT_ZSTD  2
#define COMPRESSION_CLIENT_LZ4   3
#define COMPRESSION_CLIENT_BZIP2 4
#define COMPRESSION_SERVER_GZIP  5
#define COMPRESSION_SERVER_ZSTD  6
#define COMPRESSION_SERVER_LZ4   7

#define UPDATE_PROCESS_TITLE_NEVER   0
#define UPDATE_PROCESS_TITLE_STRICT  1
#define UPDATE_PROCESS_TITLE_MINIMAL 2
#define UPDATE_PROCESS_TITLE_VERBOSE 3

#define INDENT_PER_LEVEL      2
#define FORMAT_JSON           0
#define FORMAT_TEXT           1
#define FORMAT_JSON_COMPACT   2
#define BULLET_POINT          "- "

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

#define EMPTY_STR(_s) (_s[0] == 0)

#define MAX(a, b)               \
        ({ __typeof__ (a) _a = (a);  \
           __typeof__ (b) _b = (b);  \
           _a > _b ? _a : _b; })

#define MIN(a, b)               \
        ({ __typeof__ (a) _a = (a);  \
           __typeof__ (b) _b = (b);  \
           _a < _b ? _a : _b; })

/*
 * Common piece of code to perform a sleeping.
 *
 * @param zzz the amount of time to
 * sleep, expressed as nanoseconds.
 *
 * Example
   SLEEP(5000000L)
 *
 */
#define SLEEP(zzz)                  \
        do                               \
        {                                \
           struct timespec ts_private;   \
           ts_private.tv_sec = 0;        \
           ts_private.tv_nsec = zzz;     \
           nanosleep(&ts_private, NULL); \
        } while (0);

/*
 * Commonly used block of code to sleep
 * for a specified amount of time and
 * then jump back to a specified label.
 *
 * @param zzz how much time to sleep (as long nanoseconds)
 * @param goto_to the label to which jump to
 *
 * Example:
 *
     ...
     else
       SLEEP_AND_GOTO(100000L, retry)
 */
#define SLEEP_AND_GOTO(zzz, goto_to)    \
        do                                   \
        {                                    \
           struct timespec ts_private;       \
           ts_private.tv_sec = 0;            \
           ts_private.tv_nsec = zzz;         \
           nanosleep(&ts_private, NULL);     \
           goto goto_to;                     \
        } while (0);

/**
 * The shared memory segment
 */
extern void* shmem;

/**
 * Shared memory used to contain the Prometheus
 * response cache.
 */
extern void* prometheus_cache_shmem;

/**
 * Shared memory used to contain the bridge
 * response cache.
 */
extern void* bridge_cache_shmem;

/**
 * Shared memory used to contain the bridge JSON
 * response cache.
 */
extern void* bridge_json_cache_shmem;

/**
 * @struct version
 * Semantic version structure for extensions (major.minor.patch format)
 */
struct version
{
   int major;     /**< Major version number */
   int minor;     /**< Minor version number (-1 if not specified) */
   int patch;     /**< Patch version number (-1 if not specified) */
} __attribute__ ((aligned (64)));

/** @struct extension_info
 * Defines information about a PostgreSQL extension
 */
struct extension_info
{
   char name[MISC_LENGTH];              /**< The extension name */
   char comment[MISC_LENGTH];           /**< The extension description/comment */
   int server;                          /**< The server index */
   bool enabled;                        /**< Is extension enabled */
   struct version installed_version;    /**< The installed version */
} __attribute__ ((aligned (64)));

/** @struct server
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH];                                      /**< The name of the server */
   char host[MISC_LENGTH];                                      /**< The host name of the server */
   int port;                                                    /**< The port of the server */
   int type;                                                    /**< The server type */
   char username[MAX_USERNAME_LENGTH];                          /**< The user name */
   char data[MISC_LENGTH];                                      /**< The data directory */
   char wal[MISC_LENGTH];                                       /**< The WAL directory */
   SSL* ssl;                                                    /**< The SSL structure */
   int fd;                                                      /**< The socket descriptor */
   bool new;                                                    /**< Is the connection new */
   int state;                                                   /**< The state of the server */
   int version;                                                 /**< The major version of the server*/
   int minor_version;                                           /**< The minor version of the server*/
   int number_of_databases;                                     /**< The number of databases */
   int number_of_extensions;                                    /**< The number of extensions */
   char tls_cert_file[MAX_PATH];                                /**< TLS certificate path */
   char tls_key_file[MAX_PATH];                                 /**< TLS key path */
   char tls_ca_file[MAX_PATH];                                  /**< TLS CA certificate path */
   char databases[NUMBER_OF_EXTENSIONS][DB_NAME_LENGTH];        /**< Databases in the server */
   struct extension_info extensions[NUMBER_OF_EXTENSIONS];      /**< The extensions */
   char extensions_config[MAX_EXTENSIONS_CONFIG_LENGTH];        /**< Server-specific extensions configuration */

} __attribute__ ((aligned (64)));

/** @struct user
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
} __attribute__ ((aligned (64)));

/** @struct prometheus_cache
 * A structure to handle the Prometheus response
 * so that it is possible to serve the very same
 * response over and over depending on the cache
 * settings.
 *
 * The `valid_until` field stores the result
 * of `time(2)`.
 *
 * The cache is protected by the `lock` field.
 *
 * The `size` field stores the size of the allocated
 * `data` payload.
 */
struct prometheus_cache
{
   time_t valid_until;   /**< when the cache will become not valid */
   atomic_schar lock;    /**< lock to protect the cache */
   size_t size;          /**< size of the cache */
   char data[];          /**< the payload */
} __attribute__ ((aligned (64)));

/** @struct column
 *  Define a column
 */
struct column
{
   int type;                              /**< Metrics type 0--label 1--counter 2--gauge 3--histogram*/
   char name[PROMETHEUS_LENGTH];          /**< Column name */
   char description[PROMETHEUS_LENGTH];   /**< Description of column */
} __attribute__ ((aligned (64)));

/** @struct prometheus
 * Defines the Prometheus metrics
 */
struct prometheus
{
   char tag[PROMETHEUS_LENGTH];                    /**< The metric name */
   int sort_type;                                  /**< Sorting type of multi queries 0--SORT_NAME 1--SORT_DATA0 */
   int server_query_type;                          /**< Query type 0--SERVER_QUERY_BOTH 1--SERVER_QUERY_PRIMARY 2--SERVER_QUERY_REPLICA */
   bool exec_on_all_dbs;                           /**< Execute on all databases */
   char collector[MAX_COLLECTOR_LENGTH];           /**< Collector Tag for query */
   struct pg_query_alts* pg_root;                  /**< Root of the Query Alternatives' AVL Tree for PostgreSQL core queries*/
   struct ext_query_alts* ext_root;                /**< Root of the Query Alternatives' AVL Tree for PostgreSQL extension queries*/
} __attribute__ ((aligned (64)));

/** @struct extension_metrics
 * Metrics for a single extension
 */
struct extension_metrics
{
   char extension_name[PROMETHEUS_LENGTH];                      /**< Extension name (e.g., "pg_stat_statements") */
   int number_of_metrics;                                       /**< Number of metrics for this extension */
   struct prometheus metrics[NUMBER_OF_METRICS];                /**< The actual metrics for this extension */
} __attribute__ ((aligned (64)));

/** @struct endpoint
 * Defines a Prometheus endpoint
 */
struct endpoint
{
   char host[MISC_LENGTH]; /**< The host */
   int port;               /**< The port */
} __attribute__((aligned(64)));

/** @struct configuration
 * Defines the configuration and state of pgexporter
 */
struct configuration
{
   char configuration_path[MAX_PATH]; /**< The configuration path */
   char users_path[MAX_PATH];         /**< The users path */
   char admins_path[MAX_PATH];        /**< The admins path */
   char extensions_path[MAX_PATH];    /**< The extensions path, containing metric files */

   char host[MISC_LENGTH];        /**< The host */
   int metrics;                   /**< The metrics port */
   int metrics_cache_max_age;     /**< Number of seconds to cache the Prometheus response */
   size_t metrics_cache_max_size; /**< Number of bytes max to cache the Prometheus response */
   int management;                /**< The management port */

   int bridge;                        /**< The bridge port */
   int bridge_cache_max_age;          /**< Number of seconds to cache the bridge response */
   size_t bridge_cache_max_size;      /**< Number of bytes max to cache the bridge response */
   int bridge_json;                   /**< The bridge port */
   size_t bridge_json_cache_max_size; /**< Number of bytes max to cache the bridge response */

   bool cache;  /**< Cache connection */

   int log_type;                      /**< The logging type */
   int log_level;                     /**< The logging level */
   char log_path[MISC_LENGTH];        /**< The logging path */
   int log_mode;                      /**< The logging mode */
   size_t log_rotation_size;          /**< bytes to force log rotation */
   int log_rotation_age;              /**< minutes for log rotation */
   char log_line_prefix[MISC_LENGTH]; /**< The logging prefix */
   atomic_schar log_lock;             /**< The logging lock */

   bool tls;                     /**< Is TLS enabled */
   char tls_cert_file[MAX_PATH]; /**< TLS certificate path */
   char tls_key_file[MAX_PATH];  /**< TLS key path */
   char tls_ca_file[MAX_PATH];   /**< TLS CA certificate path */

   char metrics_cert_file[MAX_PATH];  /**< Metrics TLS certificate path */
   char metrics_key_file[MAX_PATH];   /**< Metrics TLS key path */
   char metrics_ca_file[MAX_PATH];    /**< Metrics TLS CA certificate path */

   int blocking_timeout;       /**< The blocking timeout in seconds */
   int authentication_timeout; /**< The authentication timeout in seconds */
   char pidfile[MAX_PATH];     /**< File containing the PID */

   unsigned int update_process_title;  /**< Behaviour for updating the process title */

   char libev[MISC_LENGTH]; /**< Name of libev mode */
   bool keep_alive;         /**< Use keep alive */
   bool nodelay;            /**< Use NODELAY */
   bool non_blocking;       /**< Use non blocking */
   int backlog;             /**< The backlog for listen */
   unsigned char hugepage;  /**< Huge page support */

   char unix_socket_dir[MISC_LENGTH]; /**< The directory for the Unix Domain Socket */

   int number_of_servers;        /**< The number of servers */
   int number_of_users;          /**< The number of users */
   int number_of_admins;         /**< The number of admins */
   int number_of_metrics;        /**< The number of metrics*/
   int number_of_collectors;     /**< Number of total collectors */
   int number_of_endpoints;      /**< The number of endpoints */
   int number_of_extensions;     /**< Number of loaded extensions */
   int number_of_metric_names;   /**< Number of unique metric names */

   char metrics_path[MAX_PATH]; /**< The metrics path */

   atomic_ulong logging_info;  /**< Logging: INFO */
   atomic_ulong logging_warn;  /**< Logging: WARN */
   atomic_ulong logging_error; /**< Logging: ERROR */
   atomic_ulong logging_fatal; /**< Logging: FATAL */

   char collectors[NUMBER_OF_COLLECTORS][MAX_COLLECTOR_LENGTH];         /**< List of collectors in total */
   char global_extensions[MAX_EXTENSIONS_CONFIG_LENGTH];                /**< Global extensions configuration */
   char metric_names[NUMBER_OF_METRIC_NAMES][PROMETHEUS_LENGTH];        /**< List of all the metric names */
   struct server servers[NUMBER_OF_SERVERS];                            /**< The servers */
   struct user users[NUMBER_OF_USERS];                                  /**< The users */
   struct user admins[NUMBER_OF_ADMINS];                                /**< The admins */
   struct prometheus prometheus[NUMBER_OF_METRICS];                     /**< The Prometheus metrics */
   struct endpoint endpoints[NUMBER_OF_ENDPOINTS];                      /**< The Prometheus metrics */
   struct extension_metrics extensions[MAX_EXTENSIONS_CONFIG_LENGTH];   /**< Extension metrics by extension */
} __attribute__((aligned(64)));

#ifdef __cplusplus
}
#endif

#endif
