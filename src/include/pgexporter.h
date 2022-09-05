/*
 * Copyright (C) 2022 Red Hat
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
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <openssl/ssl.h>

#define VERSION "0.3.0"

#define PGEXPORTER_HOMEPAGE "https://pgexporter.github.io/"
#define PGEXPORTER_ISSUES "https://github.com/pgexporter/pgexporter/issues"

#define MAIN_UDS ".s.pgexporter"

#define MAX_NUMBER_OF_COLUMNS 32

#define MAX_BUFFER_SIZE      65535
#define DEFAULT_BUFFER_SIZE  65535

#define MAX_USERNAME_LENGTH  128
#define MAX_PASSWORD_LENGTH 1024

#define MAX_PATH 1024
#define MISC_LENGTH 128
#define NUMBER_OF_SERVERS 64
#define NUMBER_OF_USERS    64
#define NUMBER_OF_ADMINS    8
#define NUMBER_OF_METRICS 256

#define STATE_FREE        0
#define STATE_IN_USE      1

#define SERVER_UNKNOWN 0
#define SERVER_PRIMARY 1
#define SERVER_REPLICA 2

#define AUTH_SUCCESS      0
#define AUTH_BAD_PASSWORD 1
#define AUTH_ERROR        2
#define AUTH_TIMEOUT      3

#define HUGEPAGE_OFF 0
#define HUGEPAGE_TRY 1
#define HUGEPAGE_ON  2

#define MAX_QUERY_LENGTH 1024

#define LABEL_TYPE   0
#define COUNTER_TYPE 1
#define GAUGE_TYPE   2

#define SORT_NAME  0
#define SORT_DATA0 1

#define START_STATUS            0
#define SEQUENCE_STAUS          1
#define BLOCK_STAUS             2
#define BLOCK_MAPPING_STATUS    3
#define COLUMN_SEQUENCE_STATUS  4
#define COLUMN_BLOCK_STATUS     5
#define COLUMN_MAPPING_STATUS   6
#define KEY_STATUS              7
#define VALUE_STATUS            8
#define END_STATUS              9

#define likely(x)    __builtin_expect (!!(x), 1)
#define unlikely(x)  __builtin_expect (!!(x), 0)

#define MAX(a, b)               \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a > _b ? _a : _b; })

#define MIN(a, b)               \
   ({ __typeof__ (a) _a = (a);  \
      __typeof__ (b) _b = (b);  \
      _a < _b ? _a : _b; })

/**
 * The shared memory segment
 */
extern void* shmem;

/** @struct
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH];             /**< The name of the server */
   char host[MISC_LENGTH];             /**< The host name of the server */
   int port;                           /**< The port of the server */
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char data[MISC_LENGTH];             /**< The data directory */
   char wal[MISC_LENGTH];              /**< The WAL directory */
   int fd;                             /**< The socket descriptor */
   bool new;                           /**< Is the connection new */
   bool extension;                     /**< Is the pgexporter_ext extension installed */
   int state;                          /**< The state of the server */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
} __attribute__ ((aligned (64)));

/** @struct
 *  Define a column
 */
struct column
{
   int type;                        /*< Metrics type 0--label 1--counter 2--gauge */
   char name[MISC_LENGTH];          /*< Column name */
   char description[MISC_LENGTH];   /*< Description of column */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the Prometheus metrics
 */
struct prometheus
{
   char query[MAX_QUERY_LENGTH];                   /*< The query string of metric */
   char tag[MISC_LENGTH];                          /*< The metric name */
   int sort_type;                                  /*< Sorting type of multi queries 0--SORT_NAME 1--SORT_DATA0 */
   int number_of_columns;                          /*< The number of columns */
   struct column columns[MAX_NUMBER_OF_COLUMNS];   /*< Metric columns */
} __attribute__ ((aligned (64)));

/** @struct
 * Defines the configuration and state of pgexporter
 */
struct configuration
{
   char configuration_path[MAX_PATH]; /**< The configuration path */
   char users_path[MAX_PATH];         /**< The users path */
   char admins_path[MAX_PATH];        /**< The admins path */

   char host[MISC_LENGTH]; /**< The host */
   int metrics;            /**< The metrics port */
   int management;         /**< The management port */

   bool cache; /**< Cache connection */

   int log_type;               /**< The logging type */
   int log_level;              /**< The logging level */
   char log_path[MISC_LENGTH]; /**< The logging path */
   int log_mode;               /**< The logging mode */
   atomic_schar log_lock;      /**< The logging lock */

   bool tls;                        /**< Is TLS enabled */
   char tls_cert_file[MISC_LENGTH]; /**< TLS certificate path */
   char tls_key_file[MISC_LENGTH];  /**< TLS key path */
   char tls_ca_file[MISC_LENGTH];   /**< TLS CA certificate path */

   int blocking_timeout;       /**< The blocking timeout in seconds */
   int authentication_timeout; /**< The authentication timeout in seconds */
   char pidfile[MISC_LENGTH];  /**< File containing the PID */

   char libev[MISC_LENGTH]; /**< Name of libev mode */
   int buffer_size;         /**< Socket buffer size */
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

   char metrics_path[MISC_LENGTH]; /**< The metrics path */

   struct server servers[NUMBER_OF_SERVERS];       /**< The servers */
   struct user users[NUMBER_OF_USERS];             /**< The users */
   struct user admins[NUMBER_OF_ADMINS];           /**< The admins */
   struct prometheus prometheus[NUMBER_OF_METRICS];/**< The Prometheus metrics */
} __attribute__ ((aligned (64)));

#ifdef __cplusplus
}
#endif

#endif
