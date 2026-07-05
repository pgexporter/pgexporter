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

/* pgexporter */
#include <pgexporter.h>
#include <aes.h>
#include <bridge.h>
#include <configuration.h>
#include <json.h>
#include <ext_query_alts.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <pg_query_alts.h>
#include <prometheus.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <utf8.h>
#include <value.h>
#include <yaml_configuration.h>

/* system */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define LINE_LENGTH 512

static int extract_syskey_value(char* str, char** key, char** value);
static void extract_key_value(char* str, char** key, char** value);
static int as_int(char* str, int* i);
static int as_long(char* str, long* l);
static int as_bool(char* str, bool* b);
static int as_logging_type(char* str);
static int as_logging_level(char* str);
static int as_logging_mode(char* str);
static int as_hugepage(char* str);
static int as_history_backend(char* str);
static ev_backend_t as_ev_backend(char* str);
static int as_server_type(char* str);
static int as_update_process_title(char* str);
static int as_logging_rotation_size(char* str, size_t* size);
static int as_logging_rotation_age(char* str, pgexporter_time_t* time);
static int as_milliseconds(char* str, pgexporter_time_t* time, pgexporter_time_t default_age);
static int as_bytes(char* str, long* bytes, long default_bytes);
static int as_endpoints(char* str, struct configuration* config, bool reload);
static bool check_restart_required(struct configuration* config, struct configuration* reload);
static int transfer_configuration(struct configuration* config, struct configuration* reload);
static void copy_server(struct server* dst, struct server* src);
static void copy_server_config(struct server* dst, struct server* src);
static void copy_user(struct user* dst, struct user* src);
static void copy_promethus(struct prometheus* dst, struct prometheus* src);
static void copy_endpoint(struct endpoint* dst, struct endpoint* src);
static int restart_int(char* name, int e, int n);
static int restart_string(char* name, char* e, char* n);
static bool is_supported_backend(ev_backend_t backend);
static void validate_event_backend(struct configuration* config);
static const char* ev_backend_to_string(ev_backend_t backend);

static bool is_empty_string(char* s);

static void add_configuration_response(struct json* res);
static void add_servers_configuration_response(struct json* res);

static int to_log_type(char* where, int value);
static int to_log_level(char* where, int value);
static int to_log_mode(char* where, int value);
static int to_server_tls_mode(char* where, int value);
static int to_ev_backend(char* where, int value);
static int to_hugepage(char* where, int value);
static int to_history_backend(char* where, int value);
static int to_update_process_title(char* where, int value);
static bool pgexporter_is_binary_file(const char* path);

/**
 *
 */
int
pgexporter_init_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   config->metrics = -1;
   config->metrics_query_timeout = PGEXPORTER_TIME_DISABLED;
   config->cache = true;
   config->alerts_enabled = false;
   config->number_of_metric_names = 0;
   memset(config->metric_names, 0, sizeof(config->metric_names));

   config->console = -1;

   config->history = -1;
   config->history_interval = PGEXPORTER_TIME_DISABLED;
   config->history_retention = PGEXPORTER_TIME_DISABLED;
   config->history_backend = HISTORY_BACKEND_SQLITE;
   memset(config->history_path, 0, MAX_PATH);
   memset(config->history_cert_file, 0, MAX_PATH);
   memset(config->history_key_file, 0, MAX_PATH);
   memset(config->history_ca_file, 0, MAX_PATH);

   config->bridge = -1;
   config->bridge_cache_max_age = PGEXPORTER_TIME_SEC(300);
   config->bridge_cache_max_size = PROMETHEUS_DEFAULT_BRIDGE_CACHE_SIZE;
   config->bridge_json = -1;
   config->bridge_json_cache_max_size = PROMETHEUS_DEFAULT_BRIDGE_JSON_CACHE_SIZE;
   config->bridge_history = -1;
   config->bridge_history_interval = PGEXPORTER_TIME_DISABLED;
   config->bridge_history_retention = PGEXPORTER_TIME_DISABLED;
   config->bridge_history_backend = HISTORY_BACKEND_SQLITE;
   memset(config->bridge_history_path, 0, MAX_PATH);

   memset(config->global_extensions, 0, MAX_EXTENSIONS_CONFIG_LENGTH);
   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      memset(config->servers[i].extensions_config, 0, MAX_EXTENSIONS_CONFIG_LENGTH);
   }
   config->tls = false;

   config->blocking_timeout = PGEXPORTER_TIME_SEC(30);
   config->authentication_timeout = PGEXPORTER_TIME_SEC(5);

   config->keep_alive = true;
   config->nodelay = true;
   config->non_blocking = true;
   config->backlog = 16;
   config->hugepage = HUGEPAGE_TRY;
   config->keep_running = true;
   config->ev_backend = PGEXPORTER_EVENT_BACKEND_AUTO;

   config->update_process_title = UPDATE_PROCESS_TITLE_VERBOSE;

   config->log_type = PGEXPORTER_LOGGING_TYPE_CONSOLE;
   config->log_level = PGEXPORTER_LOGGING_LEVEL_INFO;
   config->log_mode = PGEXPORTER_LOGGING_MODE_APPEND;
   atomic_init(&config->log_lock, STATE_FREE);

   atomic_init(&config->logging_info, 0);
   atomic_init(&config->logging_warn, 0);
   atomic_init(&config->logging_error, 0);
   atomic_init(&config->logging_fatal, 0);
   atomic_init(&config->query_executions_total, 0);
   atomic_init(&config->query_errors_total, 0);
   atomic_init(&config->query_timeouts_total, 0);

   for (int i = 0; i < NUMBER_OF_METRICS; i++)
   {
      config->prometheus[i].sort_type = SORT_NAME;
      config->prometheus[i].server_query_type = SERVER_QUERY_BOTH;
   }

   return 0;
}

/*
 *
 */
int
pgexporter_validate_config_file(char* path)
{
   if (path == NULL)
   {
      return EINVAL;
   }

   if (!pgexporter_exists(path))
   {
      return ENOENT;
   }

   if (!pgexporter_is_file(path))
   {
      return ENOENT;
   }

   if (access(path, R_OK) != 0)
   {
      return EACCES;
   }

   if (pgexporter_is_binary_file(path))
   {
      return EINVAL;
   }

   return 0;
}

/**
 *
 */
int
pgexporter_read_configuration(void* shm, char* filename)
{
   FILE* file;
   char section[LINE_LENGTH];
   char line[LINE_LENGTH];
   char* key = NULL;
   char* value = NULL;
   char* ptr = NULL;
   size_t max;
   int idx_server = 0;
   struct server srv;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      return 1;
   }

   memset(&section, 0, LINE_LENGTH);
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '[')
         {
            ptr = strchr(line, ']');
            if (ptr)
            {
               memset(&section, 0, LINE_LENGTH);
               max = ptr - line - 1;
               if (max > MISC_LENGTH - 1)
               {
                  max = MISC_LENGTH - 1;
               }
               memcpy(&section, line + 1, max);
               if (strcmp(section, "pgexporter"))
               {
                  if (idx_server > 0 && idx_server <= NUMBER_OF_SERVERS)
                  {
                     for (int j = 0; j < idx_server - 1; j++)
                     {
                        if (!strcmp(srv.name, config->servers[j].name))
                        {
                           warnx("Duplicate server name \"%s\"", srv.name);
                           fclose(file);
                           exit(1);
                        }
                     }

                     memcpy(&(config->servers[idx_server - 1]), &srv, sizeof(struct server));
                  }
                  else if (idx_server > NUMBER_OF_SERVERS)
                  {
                     warnx("Maximum number of servers exceeded");
                  }

                  memset(&srv, 0, sizeof(struct server));
                  pgexporter_snprintf(&srv.name[0], MISC_LENGTH, "%s", section);
                  srv.fd = -1;
                  srv.state = SERVER_UNKNOWN;
                  srv.type = SERVER_TYPE_POSTGRESQL;
                  srv.version = SERVER_UNDERTERMINED_VERSION;
                  srv.fips_enabled = SERVER_FIPS_UNKNOWN;
                  srv.tls_mode = SERVER_TLS_TRY;

                  idx_server++;
               }
            }
         }
         else if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            if (pgexporter_starts_with(line, "unix_socket_dir") || pgexporter_starts_with(line, "metrics_path") || pgexporter_starts_with(line, "alerts_path") || pgexporter_starts_with(line, "log_path") || pgexporter_starts_with(line, "tls_cert_file") || pgexporter_starts_with(line, "tls_key_file") || pgexporter_starts_with(line, "tls_ca_file") || pgexporter_starts_with(line, "metrics_cert_file") || pgexporter_starts_with(line, "metrics_key_file") || pgexporter_starts_with(line, "metrics_ca_file") || pgexporter_starts_with(line, "history_cert_file") || pgexporter_starts_with(line, "history_key_file") || pgexporter_starts_with(line, "history_ca_file"))
            {
               extract_syskey_value(line, &key, &value);
            }
            else
            {
               extract_key_value(line, &key, &value);
            }

            if (key && value)
            {
               bool unknown = false;

               /* printf("|%s|%s|\n", key, value); */

               if (!strcmp(key, "host"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->host, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.host, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "port"))
               {
                  if (strlen(section) > 0)
                  {
                     if (as_int(value, &srv.port))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "user"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_USERNAME_LENGTH - 1)
                     {
                        max = MAX_USERNAME_LENGTH - 1;
                     }
                     memcpy(&srv.username, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "type"))
               {
                  if (strlen(section) > 0)
                  {
                     srv.type = as_server_type(value);
                     if (srv.type < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->metrics))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_cache_max_size"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     long l = 0;
                     if (as_bytes(value, &l, 0))
                     {
                        unknown = true;
                     }

                     config->metrics_cache_max_size = (size_t)l;
                     if (config->metrics_cache_max_size > PROMETHEUS_MAX_CACHE_SIZE)
                     {
                        config->metrics_cache_max_size = PROMETHEUS_MAX_CACHE_SIZE;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_cache_max_age"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->metrics_cache_max_age, PGEXPORTER_TIME_DISABLED))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_query_timeout"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->metrics_query_timeout, PGEXPORTER_TIME_DISABLED))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->bridge))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_endpoints"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_endpoints(value, config, false))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_cache_max_size"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     long l = 0;

                     if (as_bytes(value, &l, PROMETHEUS_DEFAULT_BRIDGE_CACHE_SIZE))
                     {
                        unknown = true;
                     }

                     config->bridge_cache_max_size = (size_t)l;

                     if (config->bridge_cache_max_size > PROMETHEUS_MAX_BRIDGE_CACHE_SIZE)
                     {
                        config->bridge_cache_max_size = PROMETHEUS_MAX_BRIDGE_CACHE_SIZE;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_cache_max_age"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->bridge_cache_max_age, PGEXPORTER_TIME_SEC(300)))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_json"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->bridge_json))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_json_cache_max_size"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     long l = 0;

                     if (as_bytes(value, &l, PROMETHEUS_DEFAULT_BRIDGE_JSON_CACHE_SIZE))
                     {
                        unknown = true;
                     }

                     config->bridge_json_cache_max_size = (size_t)l;

                     if (config->bridge_json_cache_max_size > PROMETHEUS_MAX_BRIDGE_JSON_CACHE_SIZE)
                     {
                        config->bridge_json_cache_max_size = PROMETHEUS_MAX_BRIDGE_JSON_CACHE_SIZE;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "management"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->management))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "console"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->console))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->history))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_interval"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->history_interval, PGEXPORTER_TIME_DISABLED))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_retention"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->history_retention, PGEXPORTER_TIME_DISABLED))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_backend"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->history_backend = as_history_backend(value);
                     if (config->history_backend < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_path"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->history_path, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_cert_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->history_cert_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_key_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->history_key_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "history_ca_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->history_ca_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_history"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->bridge_history))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_history_interval"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->bridge_history_interval, PGEXPORTER_TIME_DISABLED))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_history_retention"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->bridge_history_retention, PGEXPORTER_TIME_DISABLED))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_history_backend"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->bridge_history_backend = as_history_backend(value);
                     if (config->bridge_history_backend < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "bridge_history_path"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->bridge_history_path, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "alerts"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_bool(value, &config->alerts_enabled))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "cache"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_bool(value, &config->cache))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "alerts_path"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->alerts_path, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_bool(value, &config->tls))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     if (!strcmp(value, "off"))
                     {
                        srv.tls_mode = SERVER_TLS_OFF;
                     }
                     else if (!strcmp(value, "try"))
                     {
                        srv.tls_mode = SERVER_TLS_TRY;
                     }
                     else if (!strcmp(value, "on"))
                     {
                        srv.tls_mode = SERVER_TLS_ON;
                     }
                     else
                     {
                        unknown = true;
                     }
                  }
               }
               else if (!strcmp(key, "tls_ca_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->tls_ca_file, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.tls_ca_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_cert_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->tls_cert_file, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.tls_cert_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "tls_key_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->tls_key_file, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(&srv.tls_key_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_ca_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->metrics_ca_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_cert_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->metrics_cert_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_key_file"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->metrics_key_file, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "blocking_timeout"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_milliseconds(value, &config->blocking_timeout, PGEXPORTER_TIME_SEC(30)))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "pidfile"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->pidfile, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "update_process_title"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     int t = as_update_process_title(value);
                     if (t < 0)
                     {
                        unknown = true;
                     }
                     else
                     {
                        config->update_process_title = t;
                     }
                  }
                  else
                  {
                     unknown = false;
                  }
               }
               else if (!strcmp(key, "log_type"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->log_type = as_logging_type(value);
                     if (config->log_type < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_level"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->log_level = as_logging_level(value);
                     if (config->log_level < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_path"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->log_path, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_rotation_size"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_logging_rotation_size(value, &config->log_rotation_size))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_rotation_age"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_logging_rotation_age(value, &config->log_rotation_age))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_line_prefix"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->log_line_prefix, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "log_mode"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->log_mode = as_logging_mode(value);
                     if (config->log_mode < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "unix_socket_dir"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->unix_socket_dir, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "ev_backend"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->ev_backend = as_ev_backend(value);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "keep_alive"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_bool(value, &config->keep_alive))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "nodelay"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_bool(value, &config->nodelay))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "non_blocking"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_bool(value, &config->non_blocking))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "backlog"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     if (as_int(value, &config->backlog))
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "hugepage"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     config->hugepage = as_hugepage(value);
                     if (config->hugepage < 0)
                     {
                        unknown = true;
                     }
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "data_dir"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.data, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "wal_dir"))
               {
                  if (strlen(section) > 0)
                  {
                     max = strlen(section);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(&srv.wal, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "metrics_path"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MAX_PATH - 1)
                     {
                        max = MAX_PATH - 1;
                     }
                     memcpy(config->metrics_path, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "extensions"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     // Store global extensions config
                     max = strlen(value);
                     if (max > MAX_EXTENSIONS_CONFIG_LENGTH - 1)
                     {
                        max = MAX_EXTENSIONS_CONFIG_LENGTH - 1;
                     }
                     memcpy(config->global_extensions, value, max);
                  }
                  else if (strlen(section) > 0)
                  {
                     // Store server-specific extensions config
                     max = strlen(section);
                     if (max > MAX_EXTENSIONS_CONFIG_LENGTH - 1)
                     {
                        max = MAX_EXTENSIONS_CONFIG_LENGTH - 1;
                     }
                     memcpy(&srv.name, section, max);
                     max = strlen(value);
                     if (max > MAX_EXTENSIONS_CONFIG_LENGTH - 1)
                     {
                        max = MAX_EXTENSIONS_CONFIG_LENGTH - 1;
                     }
                     memcpy(&srv.extensions_config, value, max);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "collectors"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     char* val_copy = strdup(value);
                     char* saveptr = NULL;
                     char* token = NULL;
                     int idx = 0;

                     memset(config->allowed_collectors, 0, sizeof(config->allowed_collectors));

                     token = strtok_r(val_copy, ",", &saveptr);
                     while (token != NULL && idx < NUMBER_OF_COLLECTORS)
                     {
                        while (*token == ' ')
                        {
                           token++;
                        }

                        pgexporter_snprintf(config->allowed_collectors[idx++], MAX_COLLECTOR_LENGTH, "%s", token);
                        token = strtok_r(NULL, ",", &saveptr);
                     }
                     config->number_of_allowed_collectors = idx;
                     free(val_copy);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else if (!strcmp(key, "exclude_collectors"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     char* val_copy = strdup(value);
                     char* saveptr = NULL;
                     char* token = NULL;
                     int idx = 0;

                     memset(config->excluded_collectors, 0, sizeof(config->excluded_collectors));

                     token = strtok_r(val_copy, ",", &saveptr);
                     while (token != NULL && idx < NUMBER_OF_COLLECTORS)
                     {
                        while (*token == ' ')
                        {
                           token++;
                        }

                        pgexporter_snprintf(config->excluded_collectors[idx++], MAX_COLLECTOR_LENGTH, "%s", token);
                        token = strtok_r(NULL, ",", &saveptr);
                     }
                     config->number_of_excluded_collectors = idx;
                     free(val_copy);
                  }
                  else
                  {
                     unknown = true;
                  }
               }
               else
               {
                  unknown = true;
               }

               if (unknown)
               {
                  pgexporter_log_warn("Configuration: Invalid or unknown value for key=<'%s'> in section=<'%s'>", key, strlen(section) > 0 ? section : "<unknown>");
               }

               free(key);
               free(value);
               key = NULL;
               value = NULL;
            }
            else
            {
               warnx("Configuration: Invalid line in section=<'%s': '%s'", strlen(section) > 0 ? section : "<unknown>", line);

               free(key);
               free(value);
               key = NULL;
               value = NULL;
            }
         }
      }
   }

   if (strlen(srv.name) > 0)
   {
      for (int j = 0; j < idx_server - 1; j++)
      {
         if (!strcmp(srv.name, config->servers[j].name))
         {
            warnx("Duplicate server name \"%s\"", srv.name);
            fclose(file);
            exit(1);
         }
      }

      memcpy(&(config->servers[idx_server - 1]), &srv, sizeof(struct server));
   }

   config->number_of_servers = idx_server;
   fclose(file);

   return 0;
}

/**
 *
 */
int
pgexporter_validate_configuration(void* shm)
{
   struct stat st;
   struct configuration* config;

   config = (struct configuration*)shm;

   if (strlen(config->host) == 0)
   {
      pgexporter_log_fatal("pgexporter: No host defined");
      return 1;
   }

   if (strlen(config->unix_socket_dir) == 0)
   {
      pgexporter_log_fatal("pgexporter: No unix_socket_dir defined");
      return 1;
   }

   if (stat(config->unix_socket_dir, &st) == 0 && S_ISDIR(st.st_mode))
   {
      /* Ok */
   }
   else
   {
      pgexporter_log_fatal("pgexporter: unix_socket_dir is not a directory (%s)", config->unix_socket_dir);
      return 1;
   }

   if (config->metrics == -1 && config->bridge == -1)
   {
      pgexporter_log_fatal("pgexporter: No metrics nor bridge defined");
      return 1;
   }

   if (config->bridge == -1 && config->bridge_json != -1)
   {
      pgexporter_log_fatal("pgexporter: Bridge JSON defined, but bridge isn't enabled");
      return 1;
   }

   if (config->bridge_json != -1 && config->bridge_json_cache_max_size <= 0)
   {
      pgexporter_log_fatal("pgexporter: Bridge JSON requires a cache");
      return 1;
   }

   if (config->backlog < 16)
   {
      config->backlog = 16;
   }

   /* log_level is set to -1 by as_logging_level for invalid values */
   if (config->log_level < 0)
   {
      pgexporter_log_fatal("pgexporter: Invalid log_level");
      return 1;
   }

   /* log_type is set to -1 by as_logging_type for invalid values */
   if (config->log_type < 0)
   {
      pgexporter_log_fatal("pgexporter: Invalid log_type");
      return 1;
   }

   /* log_mode is set to -1 by as_logging_mode for invalid values */
   if (config->log_mode < 0)
   {
      pgexporter_log_fatal("pgexporter: Invalid log_mode");
      return 1;
   }

   /* hugepage is set to -1 by as_hugepage for invalid values */
   if (config->hugepage < 0)
   {
      pgexporter_log_fatal("pgexporter: Invalid hugepage");
      return 1;
   }

   /* history_backend is set to -1 by as_history_backend for invalid values */
   if (config->history_backend < 0)
   {
      pgexporter_log_fatal("pgexporter: Invalid history_backend");
      return 1;
   }

   /* bridge_history_backend is set to -1 by as_history_backend for invalid values */
   if (config->bridge_history_backend < 0)
   {
      pgexporter_log_fatal("pgexporter: Invalid bridge_history_backend");
      return 1;
   }

   if (strlen(config->metrics_cert_file) > 0)
   {
      if (!pgexporter_exists(config->metrics_cert_file))
      {
         pgexporter_log_error("metrics cert file does not exist, falling back to plain HTTP");
         memset(config->metrics_cert_file, 0, sizeof(config->metrics_cert_file));
         memset(config->metrics_key_file, 0, sizeof(config->metrics_key_file));
         memset(config->metrics_ca_file, 0, sizeof(config->metrics_ca_file));
      }
   }

   if (strlen(config->metrics_key_file) > 0)
   {
      if (!pgexporter_exists(config->metrics_key_file))
      {
         pgexporter_log_error("metrics key file does not exist, falling back to plain HTTP");
         memset(config->metrics_cert_file, 0, sizeof(config->metrics_cert_file));
         memset(config->metrics_key_file, 0, sizeof(config->metrics_key_file));
         memset(config->metrics_ca_file, 0, sizeof(config->metrics_ca_file));
      }
   }

   if (strlen(config->metrics_ca_file) > 0)
   {
      if (!pgexporter_exists(config->metrics_ca_file))
      {
         pgexporter_log_error("metrics ca file does not exist, falling back to plain HTTP");
         memset(config->metrics_cert_file, 0, sizeof(config->metrics_cert_file));
         memset(config->metrics_key_file, 0, sizeof(config->metrics_key_file));
         memset(config->metrics_ca_file, 0, sizeof(config->metrics_ca_file));
      }
   }

   if (strlen(config->history_cert_file) > 0)
   {
      if (!pgexporter_exists(config->history_cert_file))
      {
         pgexporter_log_error("history cert file does not exist, falling back to plain HTTP");
         memset(config->history_cert_file, 0, sizeof(config->history_cert_file));
         memset(config->history_key_file, 0, sizeof(config->history_key_file));
         memset(config->history_ca_file, 0, sizeof(config->history_ca_file));
      }
   }

   if (strlen(config->history_key_file) > 0)
   {
      if (!pgexporter_exists(config->history_key_file))
      {
         pgexporter_log_error("history key file does not exist, falling back to plain HTTP");
         memset(config->history_cert_file, 0, sizeof(config->history_cert_file));
         memset(config->history_key_file, 0, sizeof(config->history_key_file));
         memset(config->history_ca_file, 0, sizeof(config->history_ca_file));
      }
   }

   if (strlen(config->history_ca_file) > 0)
   {
      if (!pgexporter_exists(config->history_ca_file))
      {
         pgexporter_log_error("history ca file does not exist, falling back to plain HTTP");
         memset(config->history_cert_file, 0, sizeof(config->history_cert_file));
         memset(config->history_key_file, 0, sizeof(config->history_key_file));
         memset(config->history_ca_file, 0, sizeof(config->history_ca_file));
      }
   }

   if (config->number_of_servers <= 0)
   {
      pgexporter_log_fatal("pgexporter: No servers defined");
      return 1;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (!strcmp(config->servers[i].name, "pgexporter"))
      {
         pgexporter_log_fatal("pgexporter: pgexporter is a reserved word for a host");
         return 1;
      }

      if (!strcmp(config->servers[i].name, "all"))
      {
         pgexporter_log_fatal("pgexporter: all is a reserved word for a host");
         return 1;
      }

      if (strlen(config->servers[i].host) == 0)
      {
         pgexporter_log_fatal("pgexporter: No host defined for %s", config->servers[i].name);
         return 1;
      }

      if (config->servers[i].port == 0)
      {
         pgexporter_log_fatal("pgexporter: No port defined for %s", config->servers[i].name);
         return 1;
      }

      /* server type is set to -1 by as_server_type for invalid values */
      if (config->servers[i].type < 0)
      {
         pgexporter_log_fatal("pgexporter: Invalid type for %s", config->servers[i].name);
         return 1;
      }

      if (config->servers[i].type == SERVER_TYPE_POSTGRESQL && strlen(config->servers[i].username) == 0)
      {
         pgexporter_log_fatal("pgexporter: No user defined for %s", config->servers[i].name);
         return 1;
      }
   }

   if (pgexporter_time_is_valid(config->metrics_query_timeout) && pgexporter_time_convert(config->metrics_query_timeout, FORMAT_TIME_MS) < 50)
   {
      pgexporter_log_warn("metrics_query_timeout=%" PRId64 "ms is too low, using 50ms minimum", pgexporter_time_convert(config->metrics_query_timeout, FORMAT_TIME_MS));
      config->metrics_query_timeout = PGEXPORTER_TIME_MS(50);
   }

   validate_event_backend(config);

   return 0;
}

/**
 *
 */
int
pgexporter_read_users_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   size_t decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      goto error;
   }

   unsigned char* master_salt = NULL;

   if (pgexporter_get_master_key_and_salt(&master_key, &master_salt, NULL))
   {
      goto masterkey;
   }

   pgexporter_set_master_salt(master_salt);
   free(master_salt);

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            ptr = strtok(line, ":");

            username = ptr;

            ptr = strtok(NULL, ":");

            if (ptr == NULL)
            {
               goto error;
            }

            if (pgexporter_base64_decode(ptr, strlen(ptr), (void**)&decoded, &decoded_length))
            {
               goto error;
            }

            if (pgexporter_decrypt(decoded, decoded_length, master_key, &password, ENCRYPTION_AES_256_GCM))
            {
               goto error;
            }

            // Validate password is valid UTF-8
            if (!pgexporter_utf8_valid((unsigned char*)password, strlen(password)))
            {
               warnx("pgexporter: Invalid USER entry: invalid UTF-8 password for user '%s'", username);
               warnx("%s", line);
               free(password);
               free(decoded);
               password = NULL;
               decoded = NULL;
               continue;
            }

            // Check character length
            size_t char_count = pgexporter_utf8_char_length((unsigned char*)password, strlen(password));
            if (char_count == (size_t)-1)
            {
               warnx("pgexporter: Invalid USER entry: error counting UTF-8 characters for user '%s'", username);
               warnx("%s", line);
               free(password);
               free(decoded);
               password = NULL;
               decoded = NULL;
               continue;
            }
            if (char_count > MAX_PASSWORD_CHARS)
            {
               pgexporter_log_warn("Password too long for user '%s' (%zu characters)", username, char_count);
               warnx("pgexporter: Invalid USER entry");
               warnx("%s", line);
               free(password);
               free(decoded);
               password = NULL;
               decoded = NULL;
               continue;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               pgexporter_snprintf(&config->users[index].username[0], MAX_USERNAME_LENGTH, "%s", username);
               pgexporter_snprintf(&config->users[index].password[0], MAX_PASSWORD_LENGTH, "%s", password);
            }
            else
            {
               warnx("pgexporter: Invalid USER entry");
               warnx("%s\n", line);
            }

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;
         }
      }
   }

   config->number_of_users = index;

   if (config->number_of_users > NUMBER_OF_USERS)
   {
      goto above;
   }

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);

   fclose(file);

   return 0;

error:

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgexporter_validate_users_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   if (config->number_of_users <= 0)
   {
      pgexporter_log_fatal("pgexporter: No users defined");
      return 1;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      bool found = false;

      if (config->servers[i].type == SERVER_TYPE_PROMETHEUS)
      {
         continue;
      }

      for (int j = 0; !found && j < config->number_of_users; j++)
      {
         if (!strcmp(config->servers[i].username, config->users[j].username))
         {
            found = true;
         }
      }

      if (!found)
      {
         pgexporter_log_fatal("pgexporter: Unknown user (\'%s\') defined for %s", config->servers[i].username, config->servers[i].name);
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgexporter_read_admins_configuration(void* shm, char* filename)
{
   FILE* file;
   char line[LINE_LENGTH];
   int index;
   char* master_key = NULL;
   char* username = NULL;
   char* password = NULL;
   char* decoded = NULL;
   size_t decoded_length = 0;
   char* ptr = NULL;
   struct configuration* config;

   file = fopen(filename, "r");

   if (!file)
   {
      goto error;
   }

   unsigned char* master_salt = NULL;

   if (pgexporter_get_master_key_and_salt(&master_key, &master_salt, NULL))
   {
      goto masterkey;
   }

   pgexporter_set_master_salt(master_salt);
   free(master_salt);

   index = 0;
   config = (struct configuration*)shm;

   while (fgets(line, sizeof(line), file))
   {
      if (!is_empty_string(line))
      {
         if (line[0] == '#' || line[0] == ';')
         {
            /* Comment, so ignore */
         }
         else
         {
            ptr = strtok(line, ":");

            username = ptr;

            ptr = strtok(NULL, ":");

            if (ptr == NULL)
            {
               goto error;
            }

            if (pgexporter_base64_decode(ptr, strlen(ptr), (void**)&decoded, &decoded_length))
            {
               goto error;
            }

            if (pgexporter_decrypt(decoded, decoded_length, master_key, &password, ENCRYPTION_AES_256_GCM))
            {
               goto error;
            }

            // Validate password is valid UTF-8
            if (!pgexporter_utf8_valid((unsigned char*)password, strlen(password)))
            {
               warnx("pgexporter: Invalid ADMIN entry: invalid UTF-8 password for user '%s'", username);
               warnx("%s", line);
               free(password);
               free(decoded);
               password = NULL;
               decoded = NULL;
               continue;
            }

            // Check character length
            size_t char_count = pgexporter_utf8_char_length((unsigned char*)password, strlen(password));
            if (char_count == (size_t)-1)
            {
               warnx("pgexporter: Invalid ADMIN entry: error counting UTF-8 characters for user '%s'", username);
               warnx("%s", line);
               free(password);
               free(decoded);
               password = NULL;
               decoded = NULL;
               continue;
            }
            if (char_count > MAX_PASSWORD_CHARS)
            {
               pgexporter_log_warn("Password too long for user '%s' (%zu characters)", username, char_count);
               warnx("pgexporter: Invalid ADMIN entry");
               warnx("%s", line);
               free(password);
               free(decoded);
               password = NULL;
               decoded = NULL;
               continue;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               pgexporter_snprintf(&config->admins[index].username[0], MAX_USERNAME_LENGTH, "%s", username);
               pgexporter_snprintf(&config->admins[index].password[0], MAX_PASSWORD_LENGTH, "%s", password);
            }
            else
            {
               warnx("pgexporter: Invalid ADMIN entry");
               warnx("%s", line);
            }

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;

            free(password);
            free(decoded);

            password = NULL;
            decoded = NULL;

            index++;
         }
      }
   }

   config->number_of_admins = index;

   if (config->number_of_admins > NUMBER_OF_ADMINS)
   {
      goto above;
   }

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);

   fclose(file);

   return 0;

error:

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

   if (master_key != NULL)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
   }
   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 3;
}

/**
 *
 */
int
pgexporter_validate_admins_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   if (config->management > 0 && config->number_of_admins == 0)
   {
      pgexporter_log_warn("pgexporter: Remote management enabled, but no admins are defined");
   }
   else if (config->management == 0 && config->number_of_admins > 0)
   {
      pgexporter_log_warn("pgexporter: Remote management disabled, but admins are defined");
   }

   return 0;
}

int
pgexporter_reload_configuration(bool* r)
{
   size_t reload_size;
   struct configuration* reload = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   *r = false;

   pgexporter_log_trace("Configuration: %s", config->configuration_path);
   pgexporter_log_trace("Users: %s", config->users_path);
   pgexporter_log_trace("Admins: %s", config->admins_path);

   reload_size = sizeof(struct configuration);

   if (pgexporter_create_shared_memory(reload_size, HUGEPAGE_OFF, (void**)&reload))
   {
      goto error;
   }

   pgexporter_init_configuration((void*)reload);

   if (pgexporter_read_configuration((void*)reload, config->configuration_path))
   {
      goto error;
   }

   if (pgexporter_read_users_configuration((void*)reload, config->users_path))
   {
      goto error;
   }

   if (strcmp("", config->admins_path))
   {
      if (pgexporter_read_admins_configuration((void*)reload, config->admins_path))
      {
         goto error;
      }
   }

   if (pgexporter_read_internal_yaml_metrics(reload, true))
   {
      goto error;
   }

   if (strlen(reload->metrics_path) > 0)
   {
      if (pgexporter_read_metrics_configuration((void*)reload))
      {
         goto error;
      }
   }

   if (pgexporter_validate_configuration(reload))
   {
      goto error;
   }

   if (pgexporter_validate_users_configuration(reload))
   {
      goto error;
   }

   if (pgexporter_validate_admins_configuration(reload))
   {
      goto error;
   }

   int transfer_result = transfer_configuration(config, reload);
   *r = (transfer_result == 1);

   /* Free Old Query Alts AVL Tree */
   for (int i = 0; reload != NULL && i < reload->number_of_metrics; i++)
   {
      pgexporter_free_pg_query_alts(reload);
   }
   pgexporter_free_extension_query_alts(reload);

   pgexporter_destroy_shared_memory((void*)reload, reload_size);

   if (transfer_result == 1)
   {
      pgexporter_log_debug("Reload: Deferred (restart required)");
   }
   else
   {
      pgexporter_log_debug("Reload: Applied successfully");
   }

   return 0;

error:

   /* Free Old Query Alts AVL Tree */
   for (int i = 0; reload != NULL && i < reload->number_of_metrics; i++)
   {
      pgexporter_free_pg_query_alts(reload);
   }
   pgexporter_free_extension_query_alts(reload);

   pgexporter_destroy_shared_memory((void*)reload, reload_size);

   pgexporter_log_debug("Reload: Failure");

   return 1;
}

void
pgexporter_conf_get(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   struct json* response = NULL;
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;

   pgexporter_start_logging();
   pgexporter_memory_init();

   start_time = time(NULL);

   if (pgexporter_management_create_response(payload, -1, &response))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_GET_ERROR, compression, encryption, payload);
      pgexporter_log_error("Conf Get: Error creating json object (%d)", MANAGEMENT_ERROR_CONF_GET_ERROR);
      goto error;
   }

   add_configuration_response(response);
   add_servers_configuration_response(response);

   end_time = time(NULL);

   if (pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_GET_NETWORK, compression, encryption, payload);
      pgexporter_log_error("Conf Get: Error sending response");

      goto error;
   }

   elapsed = pgexporter_get_timestamp_string(start_time, end_time, &total_seconds);

   pgexporter_log_info("Conf Get (Elapsed: %s)", elapsed);

   free(elapsed);
   elapsed = NULL;

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);
error:

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(1);
}

static void
get_config_value_str(char* buf, size_t size, struct configuration* cfg, char* key, int server_index)
{
   buf[0] = '\0';
   if (!strcmp(key, "host"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].host);
      else
         pgexporter_snprintf(buf, size, "%s", cfg->host);
   }
   else if (!strcmp(key, "port"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%d", cfg->servers[server_index].port);
   }
   else if (!strcmp(key, "user"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].username);
   }
   else if (!strcmp(key, "type"))
   {
      if (server_index >= 0)
      {
         int t = cfg->servers[server_index].type;
         if (t == SERVER_TYPE_POSTGRESQL)
            pgexporter_snprintf(buf, size, "postgresql");
         else if (t == SERVER_TYPE_PROMETHEUS)
            pgexporter_snprintf(buf, size, "prometheus");
         else
            pgexporter_snprintf(buf, size, "unknown");
      }
   }
   else if (!strcmp(key, "metrics"))
      pgexporter_snprintf(buf, size, "%d", cfg->metrics);
   else if (!strcmp(key, "metrics_cache_max_size"))
      pgexporter_snprintf(buf, size, "%zu", cfg->metrics_cache_max_size);
   else if (!strcmp(key, "metrics_cache_max_age"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->metrics_cache_max_age, FORMAT_TIME_S));
   else if (!strcmp(key, "metrics_query_timeout"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->metrics_query_timeout, FORMAT_TIME_MS));
   else if (!strcmp(key, "metrics_path"))
      pgexporter_snprintf(buf, size, "%s", cfg->metrics_path);
   else if (!strcmp(key, "console"))
      pgexporter_snprintf(buf, size, "%d", cfg->console);
   else if (!strcmp(key, "management"))
      pgexporter_snprintf(buf, size, "%d", cfg->management);
   else if (!strcmp(key, "log_type"))
      to_log_type(buf, cfg->log_type);
   else if (!strcmp(key, "log_level"))
      to_log_level(buf, cfg->log_level);
   else if (!strcmp(key, "log_path"))
      pgexporter_snprintf(buf, size, "%s", cfg->log_path);
   else if (!strcmp(key, "log_line_prefix"))
      pgexporter_snprintf(buf, size, "%s", cfg->log_line_prefix);
   else if (!strcmp(key, "log_rotation_size"))
      pgexporter_snprintf(buf, size, "%zu", cfg->log_rotation_size);
   else if (!strcmp(key, "log_rotation_age"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->log_rotation_age, FORMAT_TIME_S));
   else if (!strcmp(key, "log_mode"))
      to_log_mode(buf, cfg->log_mode);
   else if (!strcmp(key, "cache"))
      pgexporter_snprintf(buf, size, "%s", cfg->cache ? "true" : "false");
   else if (!strcmp(key, "alerts_enabled"))
      pgexporter_snprintf(buf, size, "%s", cfg->alerts_enabled ? "true" : "false");
   else if (!strcmp(key, "alerts_path"))
      pgexporter_snprintf(buf, size, "%s", cfg->alerts_path);
   else if (!strcmp(key, "history"))
      pgexporter_snprintf(buf, size, "%d", cfg->history);
   else if (!strcmp(key, "history_interval"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->history_interval, FORMAT_TIME_S));
   else if (!strcmp(key, "history_retention"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->history_retention, FORMAT_TIME_S));
   else if (!strcmp(key, "history_backend"))
      to_history_backend(buf, cfg->history_backend);
   else if (!strcmp(key, "history_path"))
      pgexporter_snprintf(buf, size, "%s", cfg->history_path);
   else if (!strcmp(key, "bridge"))
      pgexporter_snprintf(buf, size, "%d", cfg->bridge);
   else if (!strcmp(key, "bridge_endpoints"))
      pgexporter_snprintf(buf, size, "%s", ""); // complex, skip detailed capture
   else if (!strcmp(key, "bridge_cache_max_size"))
      pgexporter_snprintf(buf, size, "%zu", cfg->bridge_cache_max_size);
   else if (!strcmp(key, "bridge_cache_max_age"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->bridge_cache_max_age, FORMAT_TIME_S));
   else if (!strcmp(key, "bridge_json"))
      pgexporter_snprintf(buf, size, "%d", cfg->bridge_json);
   else if (!strcmp(key, "bridge_json_cache_max_size"))
      pgexporter_snprintf(buf, size, "%zu", cfg->bridge_json_cache_max_size);
   else if (!strcmp(key, "bridge_history"))
      pgexporter_snprintf(buf, size, "%d", cfg->bridge_history);
   else if (!strcmp(key, "bridge_history_interval"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->bridge_history_interval, FORMAT_TIME_S));
   else if (!strcmp(key, "bridge_history_retention"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->bridge_history_retention, FORMAT_TIME_S));
   else if (!strcmp(key, "bridge_history_backend"))
      to_history_backend(buf, cfg->bridge_history_backend);
   else if (!strcmp(key, "bridge_history_path"))
      pgexporter_snprintf(buf, size, "%s", cfg->bridge_history_path);
   else if (!strcmp(key, "tls"))
   {
      if (server_index >= 0)
         to_server_tls_mode(buf, cfg->servers[server_index].tls_mode);
      else
         pgexporter_snprintf(buf, size, "%s", cfg->tls ? "true" : "false");
   }
   else if (!strcmp(key, "tls_ca_file"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].tls_ca_file);
      else
         pgexporter_snprintf(buf, size, "%s", cfg->tls_ca_file);
   }
   else if (!strcmp(key, "tls_cert_file"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].tls_cert_file);
      else
         pgexporter_snprintf(buf, size, "%s", cfg->tls_cert_file);
   }
   else if (!strcmp(key, "tls_key_file"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].tls_key_file);
      else
         pgexporter_snprintf(buf, size, "%s", cfg->tls_key_file);
   }
   else if (!strcmp(key, "metrics_ca_file"))
      pgexporter_snprintf(buf, size, "%s", cfg->metrics_ca_file);
   else if (!strcmp(key, "metrics_cert_file"))
      pgexporter_snprintf(buf, size, "%s", cfg->metrics_cert_file);
   else if (!strcmp(key, "metrics_key_file"))
      pgexporter_snprintf(buf, size, "%s", cfg->metrics_key_file);
   else if (!strcmp(key, "blocking_timeout"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->blocking_timeout, FORMAT_TIME_S));
   else if (!strcmp(key, "authentication_timeout"))
      pgexporter_snprintf(buf, size, "%lld", (long long)pgexporter_time_convert(cfg->authentication_timeout, FORMAT_TIME_S));
   else if (!strcmp(key, "keep_alive"))
      pgexporter_snprintf(buf, size, "%s", cfg->keep_alive ? "true" : "false");
   else if (!strcmp(key, "nodelay"))
      pgexporter_snprintf(buf, size, "%s", cfg->nodelay ? "true" : "false");
   else if (!strcmp(key, "non_blocking"))
      pgexporter_snprintf(buf, size, "%s", cfg->non_blocking ? "true" : "false");
   else if (!strcmp(key, "backlog"))
      pgexporter_snprintf(buf, size, "%d", cfg->backlog);
   else if (!strcmp(key, "hugepage"))
      to_hugepage(buf, cfg->hugepage);
   else if (!strcmp(key, "pidfile"))
      pgexporter_snprintf(buf, size, "%s", cfg->pidfile);
   else if (!strcmp(key, "unix_socket_dir"))
      pgexporter_snprintf(buf, size, "%s", cfg->unix_socket_dir);
   else if (!strcmp(key, "ev_backend"))
      to_ev_backend(buf, cfg->ev_backend);
   else if (!strcmp(key, "update_process_title"))
      to_update_process_title(buf, cfg->update_process_title);
   else if (!strcmp(key, "data_dir"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].data);
   }
   else if (!strcmp(key, "wal_dir"))
   {
      if (server_index >= 0)
         pgexporter_snprintf(buf, size, "%s", cfg->servers[server_index].wal);
   }
}

/**
  * Copy configuration values from src to dst, excluding runtime pointers.
  * Runtime fields (SSL*, fd, state, query_alts trees, atomics) are preserved in dst.
  */
static void
copy_configuration_values(struct configuration* dst, struct configuration* src)
{
   size_t config_size = sizeof(struct configuration);
   memset(dst, 0, config_size);

   /* Copy all scalar and array configuration values */
   memcpy(dst->configuration_path, src->configuration_path, MAX_PATH);
   memcpy(dst->users_path, src->users_path, MAX_PATH);
   memcpy(dst->admins_path, src->admins_path, MAX_PATH);
   memcpy(dst->extensions_path, src->extensions_path, MAX_PATH);
   memcpy(dst->alerts_path, src->alerts_path, MAX_PATH);

   memcpy(dst->host, src->host, MISC_LENGTH);
   dst->metrics = src->metrics;
   dst->metrics_cache_max_age = src->metrics_cache_max_age;
   dst->metrics_cache_max_size = src->metrics_cache_max_size;
   dst->metrics_query_timeout = src->metrics_query_timeout;
   dst->management = src->management;
   dst->console = src->console;

   dst->history = src->history;
   dst->history_interval = src->history_interval;
   dst->history_retention = src->history_retention;
   dst->history_backend = src->history_backend;
   memcpy(dst->history_path, src->history_path, MAX_PATH);

   dst->bridge = src->bridge;
   dst->bridge_cache_max_age = src->bridge_cache_max_age;
   dst->bridge_cache_max_size = src->bridge_cache_max_size;
   dst->bridge_json = src->bridge_json;
   dst->bridge_json_cache_max_size = src->bridge_json_cache_max_size;
   dst->bridge_history = src->bridge_history;
   dst->bridge_history_interval = src->bridge_history_interval;
   dst->bridge_history_retention = src->bridge_history_retention;
   dst->bridge_history_backend = src->bridge_history_backend;
   memcpy(dst->bridge_history_path, src->bridge_history_path, MAX_PATH);

   dst->cache = src->cache;
   dst->alerts_enabled = src->alerts_enabled;

   dst->log_type = src->log_type;
   dst->log_level = src->log_level;
   memcpy(dst->log_path, src->log_path, MISC_LENGTH);
   dst->log_mode = src->log_mode;
   dst->log_rotation_size = src->log_rotation_size;
   dst->log_rotation_age = src->log_rotation_age;
   memcpy(dst->log_line_prefix, src->log_line_prefix, MISC_LENGTH);

   dst->tls = src->tls;
   memcpy(dst->tls_cert_file, src->tls_cert_file, MAX_PATH);
   memcpy(dst->tls_key_file, src->tls_key_file, MAX_PATH);
   memcpy(dst->tls_ca_file, src->tls_ca_file, MAX_PATH);
   memcpy(dst->metrics_cert_file, src->metrics_cert_file, MAX_PATH);
   memcpy(dst->metrics_key_file, src->metrics_key_file, MAX_PATH);
   memcpy(dst->metrics_ca_file, src->metrics_ca_file, MAX_PATH);

   dst->blocking_timeout = src->blocking_timeout;
   dst->authentication_timeout = src->authentication_timeout;
   memcpy(dst->pidfile, src->pidfile, MAX_PATH);
   dst->ev_backend = src->ev_backend;
   dst->keep_alive = src->keep_alive;
   dst->nodelay = src->nodelay;
   dst->non_blocking = src->non_blocking;
   dst->backlog = src->backlog;
   dst->hugepage = src->hugepage;
   dst->update_process_title = src->update_process_title;
   memcpy(dst->unix_socket_dir, src->unix_socket_dir, MISC_LENGTH);

   memcpy(dst->allowed_collectors, src->allowed_collectors, sizeof(dst->allowed_collectors));
   dst->number_of_allowed_collectors = src->number_of_allowed_collectors;
   memcpy(dst->excluded_collectors, src->excluded_collectors, sizeof(dst->excluded_collectors));
   dst->number_of_excluded_collectors = src->number_of_excluded_collectors;

   memcpy(dst->global_extensions, src->global_extensions, MAX_EXTENSIONS_CONFIG_LENGTH);

   /* Copy servers (configuration values only, not runtime state) */
   for (int i = 0; i < src->number_of_servers; i++)
   {
      copy_server_config(dst->servers + i, src->servers + i);
   }
   dst->number_of_servers = src->number_of_servers;

   /* Copy users */
   for (int i = 0; i < src->number_of_users; i++)
   {
      copy_user(dst->users + i, src->users + i);
   }
   dst->number_of_users = src->number_of_users;

   /* Copy admins */
   for (int i = 0; i < src->number_of_admins; i++)
   {
      copy_user(dst->admins + i, src->admins + i);
   }
   dst->number_of_admins = src->number_of_admins;

   /* Copy prometheus metrics configuration */
   for (int i = 0; i < src->number_of_metrics; i++)
   {
      copy_promethus(dst->prometheus + i, src->prometheus + i);
   }
   dst->number_of_metrics = src->number_of_metrics;

   /* Copy alerts */
   for (int i = 0; i < src->number_of_alerts; i++)
   {
      memcpy(&dst->alerts[i], &src->alerts[i], sizeof(struct alert_definition));
   }
   dst->number_of_alerts = src->number_of_alerts;

   /* Copy endpoints */
   for (int i = 0; i < src->number_of_endpoints; i++)
   {
      copy_endpoint(dst->endpoints + i, src->endpoints + i);
   }
   dst->number_of_endpoints = src->number_of_endpoints;

   dst->metrics_path[0] = '\0';
   if (src->metrics_path[0])
   {
      memcpy(dst->metrics_path, src->metrics_path, MAX_PATH);
   }
}

void
pgexporter_conf_set(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload, bool* restart_required, bool* success)
{
   struct json* response = NULL;
   struct json* request = NULL;
   char* config_key = NULL;
   char* config_value = NULL;
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   char section[MISC_LENGTH];
   char key[MISC_LENGTH];
   struct configuration* current_config = NULL;
   struct configuration* temp_config = NULL;
   struct configuration* config = NULL;
   struct json* server_j = NULL;
   void* temp_shmem = NULL;
   size_t temp_size = sizeof(struct configuration);
   size_t max;
   int server_index = -1;
   int begin = -1, end = -1;
   char old_value_str[MISC_LENGTH];
   bool metrics_path_changed = false;

   pgexporter_start_logging();
   pgexporter_memory_init();

   start_time = time(NULL);

   // Initialize output parameters
   *restart_required = false;
   *success = false;

   current_config = (struct configuration*)shmem;
   // Extract config_key and config_value from request
   request = (struct json*)pgexporter_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   if (!request)
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_NOREQUEST, compression, encryption, payload);
      pgexporter_log_error("Conf Set: No request category found in payload (%d)", MANAGEMENT_ERROR_CONF_SET_NOREQUEST);
      goto error;
   }

   config_key = (char*)pgexporter_json_get(request, MANAGEMENT_ARGUMENT_CONFIG_KEY);
   config_value = (char*)pgexporter_json_get(request, MANAGEMENT_ARGUMENT_CONFIG_VALUE);

   if (!config_key || !config_value)
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_NOCONFIG_KEY_OR_VALUE, compression, encryption, payload);
      pgexporter_log_error("Conf Set: No config key or config value in request (%d)", MANAGEMENT_ERROR_CONF_SET_NOCONFIG_KEY_OR_VALUE);
      goto error;
   }

   // Create temporary shared memory for validation (avoids pointer sharing with live config)
   if (pgexporter_create_shared_memory(temp_size, HUGEPAGE_OFF, &temp_shmem))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_ERROR, compression, encryption, payload);
      pgexporter_log_error("Conf Set: Unable to allocate temporary configuration");
      goto error;
   }
   temp_config = (struct configuration*)temp_shmem;

   // Initialize temp_config with default values, then copy configuration values from current
   pgexporter_init_configuration((void*)temp_config);
   copy_configuration_values(temp_config, current_config);

   config = temp_config;

   // Modify the temporary configuration first. transfer_configuration() will
   // atomically decide whether the live configuration can accept the change.
   memset(section, 0, MISC_LENGTH);
   memset(key, 0, MISC_LENGTH);

   for (size_t i = 0; i < strlen(config_key); i++)
   {
      if (config_key[i] == '.')
      {
         if (!strlen(section))
         {
            memcpy(section, &config_key[begin], end - begin + 1);
            section[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }
      }

      if (begin < 0)
      {
         begin = i;
      }

      end = i;
   }
   // if the key has not been found, since there is no ending dot,
   // try to extract it from the string
   if (!strlen(key))
   {
      memcpy(key, &config_key[begin], end - begin + 1);
      key[end - begin + 1] = '\0';
   }

   if (strlen(section) > 0)
   {
      if (pgexporter_json_create(&server_j))
      {
         pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_ERROR, compression, encryption, payload);
         pgexporter_log_error("Conf Set: Error creating json object (%d)", MANAGEMENT_ERROR_CONF_SET_ERROR);
         goto error;
      }

      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, section))
         {
            server_index = i;
            break;
         }
      }
      if (server_index == -1)
      {
         pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_UNKNOWN_SERVER, compression, encryption, payload);
         pgexporter_log_error("Conf Set: Unknown server value parsed (%d)", MANAGEMENT_ERROR_CONF_SET_UNKNOWN_SERVER);
         goto error;
      }
   }

   if (pgexporter_management_create_response(payload, -1, &response))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_ERROR, compression, encryption, payload);
      pgexporter_log_error("Conf Set: Error creating json object (%d)", MANAGEMENT_ERROR_CONF_SET_ERROR);
      goto error;
   }

   // Capture old value before modification
   memset(old_value_str, 0, MISC_LENGTH);
   get_config_value_str(old_value_str, sizeof(old_value_str), current_config, key, server_index);

   if (strlen(key) && config_value)
   {
      bool unknown = false;
      bool invalid_value = false;
      if (!strcmp(key, "host"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MISC_LENGTH - 1)
            {
               max = MISC_LENGTH - 1;
            }
            memcpy(&config->servers[server_index].host, config_value, max);
            config->servers[server_index].host[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config_value, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            max = strlen(config_value);
            if (max > MISC_LENGTH - 1)
            {
               max = MISC_LENGTH - 1;
            }
            memcpy(config->host, config_value, max);
            config->host[max] = '\0';
            pgexporter_json_put(response, key, (uintptr_t)config_value, ValueString);
         }
      }
      else if (!strcmp(key, "port"))
      {
         if (strlen(section) > 0)
         {
            if (as_int(config_value, &config->servers[server_index].port))
            {
               invalid_value = true;
            }
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].port, ValueInt64);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            unknown = true;
         }
      }
      else if (!strcmp(key, "user"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MAX_USERNAME_LENGTH - 1)
            {
               max = MAX_USERNAME_LENGTH - 1;
            }
            memcpy(&config->servers[server_index].username, config_value, max);
            config->servers[server_index].username[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].username, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            unknown = true;
         }
      }
      else if (!strcmp(key, "type"))
      {
         if (strlen(section) > 0)
         {
            {
               int t = as_server_type(config_value);
               if (t < 0)
               {
                  invalid_value = true;
               }
               else
               {
                  config->servers[server_index].type = t;
               }
            }
            pgexporter_json_put(server_j, key, (uintptr_t)config_value, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            unknown = true;
         }
      }
      else if (!strcmp(key, "metrics"))
      {
         if (as_int(config_value, &config->metrics))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->metrics, ValueInt64);
      }
      else if (!strcmp(key, "metrics_cache_max_size"))
      {
         long l = 0;

         if (as_bytes(config_value, &l, 0))
         {
            invalid_value = true;
         }

         config->metrics_cache_max_size = (size_t)l;

         pgexporter_json_put(response, key, (uintptr_t)config->metrics_cache_max_size, ValueInt64);
      }
      else if (!strcmp(key, "metrics_cache_max_age"))
      {
         if (as_milliseconds(config_value, &config->metrics_cache_max_age, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->metrics_cache_max_age, FORMAT_TIME_S), ValueInt64);
      }
      else if (!strcmp(key, "metrics_query_timeout"))
      {
         if (as_milliseconds(config_value, &config->metrics_query_timeout, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->metrics_query_timeout, FORMAT_TIME_MS), ValueInt64);
      }
      else if (!strcmp(key, "metrics_path"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->metrics_path, config_value, max);
         config->metrics_path[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_path, ValueString);
         metrics_path_changed = true;
      }
      else if (!strcmp(key, "console"))
      {
         if (as_int(config_value, &config->console))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->console, ValueInt64);
      }
      else if (!strcmp(key, "bridge"))
      {
         if (as_int(config_value, &config->bridge))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->bridge, ValueInt64);
      }
      else if (!strcmp(key, "bridge_endpoints"))
      {
         if (as_endpoints(config_value, config, true))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config_value, ValueString);
      }
      else if (!strcmp(key, "bridge_cache_max_size"))
      {
         long l = 0;

         if (as_bytes(config_value, &l, 0))
         {
            invalid_value = true;
         }

         config->bridge_cache_max_size = (size_t)l;

         pgexporter_json_put(response, key, (uintptr_t)config->bridge_cache_max_size, ValueInt64);
      }
      else if (!strcmp(key, "bridge_cache_max_age"))
      {
         if (as_milliseconds(config_value, &config->bridge_cache_max_age, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->bridge_cache_max_age, FORMAT_TIME_S), ValueInt64);
      }
      else if (!strcmp(key, "bridge_json"))
      {
         if (as_int(config_value, &config->bridge_json))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->bridge_json,
                             ValueInt64);
      }
      else if (!strcmp(key, "bridge_json_cache_max_size"))
      {
         long l = 0;

         if (as_bytes(config_value, &l, 0))
         {
            invalid_value = true;
         }

         config->bridge_json_cache_max_size = (size_t)l;

         pgexporter_json_put(response, key,
                             (uintptr_t)config->bridge_json_cache_max_size,
                             ValueInt64);
      }
      else if (!strcmp(key, "history"))
      {
         if (as_int(config_value, &config->history))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->history, ValueInt64);
      }
      else if (!strcmp(key, "history_interval"))
      {
         if (as_milliseconds(config_value, &config->history_interval, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->history_interval, FORMAT_TIME_S), ValueInt64);
      }
      else if (!strcmp(key, "history_retention"))
      {
         if (as_milliseconds(config_value, &config->history_retention, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->history_retention, FORMAT_TIME_S), ValueInt64);
      }
      else if (!strcmp(key, "history_backend"))
      {
         {
            int t = as_history_backend(config_value);
            if (t < 0)
            {
               invalid_value = true;
            }
            else
            {
               config->history_backend = t;
            }
            pgexporter_json_put(response, key, (uintptr_t)config->history_backend, ValueInt32);
         }
      }
      else if (!strcmp(key, "history_path"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->history_path, config_value, max);
         config->history_path[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->history_path, ValueString);
      }
      else if (!strcmp(key, "bridge_history"))
      {
         if (as_int(config_value, &config->bridge_history))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->bridge_history, ValueInt64);
      }
      else if (!strcmp(key, "bridge_history_interval"))
      {
         if (as_milliseconds(config_value, &config->bridge_history_interval, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put_time_value(response, key, config->bridge_history_interval, FORMAT_TIME_S);
      }
      else if (!strcmp(key, "bridge_history_retention"))
      {
         if (as_milliseconds(config_value, &config->bridge_history_retention, PGEXPORTER_TIME_DISABLED))
         {
            invalid_value = true;
         }
         pgexporter_json_put_time_value(response, key, config->bridge_history_retention, FORMAT_TIME_S);
      }
      else if (!strcmp(key, "bridge_history_backend"))
      {
         {
            int t = as_history_backend(config_value);
            if (t < 0)
            {
               invalid_value = true;
            }
            else
            {
               config->bridge_history_backend = t;
            }
            pgexporter_json_put_enum_value(response, key, config->bridge_history_backend, to_history_backend);
         }
      }
      else if (!strcmp(key, "bridge_history_path"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->bridge_history_path, config_value, max);
         config->bridge_history_path[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->bridge_history_path, ValueString);
      }
      else if (!strcmp(key, "management"))
      {
         if (as_int(config_value, &config->management))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->management, ValueInt64);
      }
      else if (!strcmp(key, "cache"))
      {
         if (as_bool(config_value, &config->cache))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->cache, ValueBool);
      }
      else if (!strcmp(key, "tls"))
      {
         if (strlen(section) > 0)
         {
            if (!strcmp(config_value, "off"))
            {
               config->servers[server_index].tls_mode = SERVER_TLS_OFF;
            }
            else if (!strcmp(config_value, "try"))
            {
               config->servers[server_index].tls_mode = SERVER_TLS_TRY;
            }
            else if (!strcmp(config_value, "on"))
            {
               config->servers[server_index].tls_mode = SERVER_TLS_ON;
            }
            else
            {
               invalid_value = true;
            }

            if (!unknown)
            {
               pgexporter_json_put_enum_value(server_j, key, config->servers[server_index].tls_mode, to_server_tls_mode);
               pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
            }
         }
         else
         {
            if (as_bool(config_value, &config->tls))
            {
               invalid_value = true;
            }
            pgexporter_json_put(response, key, (uintptr_t)config->tls, ValueBool);
         }
      }
      else if (!strcmp(key, "tls_ca_file"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MAX_PATH - 1)
            {
               max = MAX_PATH - 1;
            }
            memcpy(&config->servers[server_index].tls_ca_file, config_value, max);
            config->servers[server_index].tls_ca_file[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].tls_ca_file, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            max = strlen(config_value);
            if (max > MAX_PATH - 1)
            {
               max = MAX_PATH - 1;
            }
            memcpy(config->tls_ca_file, config_value, max);
            config->tls_ca_file[max] = '\0';
            pgexporter_json_put(response, key, (uintptr_t)config->tls_ca_file, ValueString);
         }
      }
      else if (!strcmp(key, "tls_cert_file"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MAX_PATH - 1)
            {
               max = MAX_PATH - 1;
            }
            memcpy(&config->servers[server_index].tls_cert_file, config_value, max);
            config->servers[server_index].tls_cert_file[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].tls_cert_file, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            max = strlen(config_value);
            if (max > MAX_PATH - 1)
            {
               max = MAX_PATH - 1;
            }
            memcpy(config->tls_cert_file, config_value, max);
            config->tls_cert_file[max] = '\0';
            pgexporter_json_put(response, key, (uintptr_t)config->tls_cert_file, ValueString);
         }
      }
      else if (!strcmp(key, "tls_key_file"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MAX_PATH - 1)
            {
               max = MAX_PATH - 1;
            }
            memcpy(&config->servers[server_index].tls_key_file, config_value, max);
            config->servers[server_index].tls_key_file[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].tls_key_file, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            max = strlen(config_value);
            if (max > MAX_PATH - 1)
            {
               max = MAX_PATH - 1;
            }
            memcpy(config->tls_key_file, config_value, max);
            config->tls_key_file[max] = '\0';
            pgexporter_json_put(response, key, (uintptr_t)config->tls_key_file, ValueString);
         }
      }
      else if (!strcmp(key, "metrics_ca_file"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->metrics_ca_file, config_value, max);
         config->metrics_ca_file[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_ca_file, ValueString);
      }
      else if (!strcmp(key, "metrics_cert_file"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->metrics_cert_file, config_value, max);
         config->metrics_cert_file[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_cert_file, ValueString);
      }
      else if (!strcmp(key, "metrics_key_file"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->metrics_key_file, config_value, max);
         config->metrics_key_file[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_key_file, ValueString);
      }
      else if (!strcmp(key, "blocking_timeout"))
      {
         if (as_milliseconds(config_value, &config->blocking_timeout, PGEXPORTER_TIME_SEC(30)))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->blocking_timeout, FORMAT_TIME_S), ValueInt64);
      }
      else if (!strcmp(key, "pidfile"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->pidfile, config_value, max);
         config->pidfile[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->pidfile, ValueString);
      }
      else if (!strcmp(key, "update_process_title"))
      {
         int t = as_update_process_title(config_value);
         if (t < 0)
         {
            invalid_value = true;
         }
         else
         {
            config->update_process_title = t;
            pgexporter_json_put_enum_value(response, key, config->update_process_title, to_update_process_title);
         }
      }
      else if (!strcmp(key, "log_type"))
      {
         {
            int t = as_logging_type(config_value);
            if (t < 0)
            {
               invalid_value = true;
            }
            else
            {
               config->log_type = t;
            }
            pgexporter_json_put(response, key, (uintptr_t)config->log_type, ValueInt32);
         }
      }
      else if (!strcmp(key, "log_level"))
      {
         int level = as_logging_level(config_value);
         if (level < 0)
         {
            invalid_value = true;
         }
         else
         {
            config->log_level = level;
            pgexporter_json_put(response, key, (uintptr_t)config->log_level, ValueInt32);
         }
      }
      else if (!strcmp(key, "log_path"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->log_path, config_value, max);
         config->log_path[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->log_path, ValueString);
      }
      else if (!strcmp(key, "log_rotation_size"))
      {
         if (as_logging_rotation_size(config_value, &config->log_rotation_size))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->log_rotation_size, ValueInt32);
      }
      else if (!strcmp(key, "log_rotation_age"))
      {
         if (as_logging_rotation_age(config_value, &config->log_rotation_age))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)pgexporter_time_convert(config->log_rotation_age, FORMAT_TIME_S), ValueInt32);
      }
      else if (!strcmp(key, "log_line_prefix"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->log_line_prefix, config_value, max);
         config->log_line_prefix[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->log_line_prefix, ValueString);
      }
      else if (!strcmp(key, "log_mode"))
      {
         {
            int t = as_logging_mode(config_value);
            if (t < 0)
            {
               invalid_value = true;
            }
            else
            {
               config->log_mode = t;
            }
            pgexporter_json_put(response, key, (uintptr_t)config->log_mode, ValueInt32);
         }
      }
      else if (!strcmp(key, "unix_socket_dir"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->unix_socket_dir, config_value, max);
         config->unix_socket_dir[max] = '\0';
         pgexporter_json_put(response, key, (uintptr_t)config->unix_socket_dir, ValueString);
      }
      else if (!strcmp(key, "ev_backend"))
      {
         {
            int t = as_ev_backend(config_value);
            if (t < 0)
            {
               invalid_value = true;
            }
            else
            {
               config->ev_backend = t;
            }
            pgexporter_json_put_enum_value(response, key, config->ev_backend, to_ev_backend);
         }
      }
      else if (!strcmp(key, "keep_alive"))
      {
         if (as_bool(config_value, &config->keep_alive))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->keep_alive, ValueBool);
      }
      else if (!strcmp(key, "nodelay"))
      {
         if (as_bool(config_value, &config->nodelay))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->nodelay, ValueBool);
      }
      else if (!strcmp(key, "non_blocking"))
      {
         if (as_bool(config_value, &config->non_blocking))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->non_blocking, ValueBool);
      }
      else if (!strcmp(key, "backlog"))
      {
         if (as_int(config_value, &config->backlog))
         {
            invalid_value = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->backlog, ValueInt32);
      }
      else if (!strcmp(key, "hugepage"))
      {
         int t = as_hugepage(config_value);
         if (t < 0)
         {
            invalid_value = true;
         }
         else
         {
            config->hugepage = t;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->hugepage, ValueChar);
      }
      else if (!strcmp(key, "data_dir"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MAX_USERNAME_LENGTH - 1)
            {
               max = MAX_USERNAME_LENGTH - 1;
            }
            memcpy(&config->servers[server_index].data, config_value, max);
            config->servers[server_index].data[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].data, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            unknown = true;
         }
      }
      else if (!strcmp(key, "wal_dir"))
      {
         if (strlen(section) > 0)
         {
            max = strlen(config_value);
            if (max > MAX_USERNAME_LENGTH - 1)
            {
               max = MAX_USERNAME_LENGTH - 1;
            }
            memcpy(&config->servers[server_index].wal, config_value, max);
            config->servers[server_index].wal[max] = '\0';
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].wal, ValueString);
            pgexporter_json_put(response, config->servers[server_index].name, (uintptr_t)server_j, ValueJSON);
         }
         else
         {
            unknown = true;
         }
      }
      else
      {
         unknown = true;
      }

      if (invalid_value)
      {
         pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_INVALID_VALUE, compression, encryption, payload);
         pgexporter_log_error("Conf Set: Invalid value for configuration key '%s': %s", config_key, config_value);
         goto error;
      }

      if (unknown)
      {
         pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY, compression, encryption, payload);
         pgexporter_log_error("Conf Set: Unknown configuration key found (%d)", MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY);
         goto error;
      }
   }

   if (metrics_path_changed)
   {
      if (pgexporter_read_metrics_configuration((void*)temp_config))
      {
         pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_ERROR, compression, encryption, payload);
         pgexporter_log_error("Conf Set: Failed to reload metrics from %s", temp_config->metrics_path);
         goto error;
      }
   }

   if (pgexporter_validate_configuration(temp_config) ||
       pgexporter_validate_users_configuration(temp_config) ||
       pgexporter_validate_admins_configuration(temp_config))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_ERROR, compression, encryption, payload);
      pgexporter_log_error("Conf Set: Validation failed for %s=%s", config_key, config_value);
      goto error;
   }

   // Atomically check restart requirement and apply changes via transfer_configuration
   int transfer_result = transfer_configuration(current_config, temp_config);
   *restart_required = (transfer_result == 1);

   end_time = time(NULL);

   *success = true;

   // Check for no-change and build response metadata
   {
      char new_value_str[MISC_LENGTH] = "";
      bool no_change = false;

      get_config_value_str(new_value_str, sizeof(new_value_str), current_config, key, server_index);
      if (strlen(old_value_str) > 0 && strlen(new_value_str) > 0)
      {
         no_change = !strcmp(old_value_str, new_value_str);
      }

      const char* status = CONFIGURATION_STATUS_SUCCESS;
      const char* message = CONFIGURATION_MESSAGE_SUCCESS;
      if (*restart_required)
      {
         status = CONFIGURATION_STATUS_RESTART_REQUIRED;
         message = CONFIGURATION_MESSAGE_RESTART_REQUIRED;
      }
      else if (no_change)
      {
         status = CONFIGURATION_STATUS_NO_CHANGE;
         message = CONFIGURATION_MESSAGE_NO_CHANGE;
      }

      if (!*restart_required && no_change)
      {
         pgexporter_log_info("Conf Set: %s is already %s", config_key, old_value_str);
      }
      else if (*restart_required)
      {
         pgexporter_log_info("Conf Set: %s=%s requires restart - running configuration preserved", config_key, config_value);
      }
      else
      {
         pgexporter_log_info("Conf Set: Changed %s from '%s' to '%s'", config_key, old_value_str, new_value_str);
      }

      pgexporter_json_put(response, CONFIGURATION_RESPONSE_STATUS, (uintptr_t)status, ValueString);
      pgexporter_json_put(response, CONFIGURATION_RESPONSE_MESSAGE, (uintptr_t)message, ValueString);
      pgexporter_json_put(response, CONFIGURATION_RESPONSE_RESTART_REQUIRED, (uintptr_t)*restart_required, ValueBool);
      pgexporter_json_put(response, CONFIGURATION_RESPONSE_CONFIG_KEY, (uintptr_t)config_key, ValueString);
      pgexporter_json_put(response, CONFIGURATION_RESPONSE_REQUESTED_VALUE, (uintptr_t)config_value, ValueString);
      if (strlen(old_value_str) > 0)
      {
         pgexporter_json_put(response, CONFIGURATION_RESPONSE_OLD_VALUE, (uintptr_t)old_value_str, ValueString);
      }
      if (strlen(new_value_str) > 0 && !*restart_required)
      {
         pgexporter_json_put(response, CONFIGURATION_RESPONSE_NEW_VALUE, (uintptr_t)new_value_str, ValueString);
      }
   }

   if (pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_NETWORK, compression, encryption, payload);
      pgexporter_log_error("Conf Set: Error sending response");
      goto error;
   }

   elapsed = pgexporter_get_timestamp_string(start_time, end_time, &total_seconds);

   pgexporter_log_info("Conf Set (Elapsed: %s)", elapsed);

   free(elapsed);
   elapsed = NULL;
   if (temp_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(temp_shmem, temp_size);
      temp_shmem = NULL;
      temp_config = NULL;
   }
   return;
error:

   free(elapsed);
   if (temp_shmem != NULL)
   {
      pgexporter_destroy_shared_memory(temp_shmem, temp_size);
      temp_shmem = NULL;
      temp_config = NULL;
   }
   return;
}

static void
add_configuration_response(struct json* res)
{
   char* data = NULL;
   struct configuration* config = NULL;

   config = (struct configuration*)shmem;
   // JSON of main configuration
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_HOST, (uintptr_t)config->host, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_UNIX_SOCKET_DIR, (uintptr_t)config->unix_socket_dir, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS, (uintptr_t)config->metrics, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_PATH, (uintptr_t)config->metrics_path, ValueString);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, config->metrics_cache_max_age, FORMAT_TIME_S);
   pgexporter_json_put_size_value(res, CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, config->metrics_cache_max_size);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_METRICS_QUERY_TIMEOUT, config->metrics_query_timeout, FORMAT_TIME_MS);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE, (uintptr_t)config->bridge, ValueInt64);

   if (config->number_of_endpoints > 0)
   {
      for (int i = 0; i < config->number_of_endpoints; i++)
      {
         data = pgexporter_append(data, config->endpoints[i].host);
         data = pgexporter_append_char(data, ':');
         data = pgexporter_append_int(data, config->endpoints[i].port);

         if (i < config->number_of_endpoints - 1)
         {
            data = pgexporter_append_char(data, ',');
         }
      }
   }
   else
   {
      data = pgexporter_append(data, "");
   }

   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_ENDPOINTS, (uintptr_t)data, ValueString);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_BRIDGE_CACHE_MAX_AGE, config->bridge_cache_max_age, FORMAT_TIME_S);
   pgexporter_json_put_size_value(res, CONFIGURATION_ARGUMENT_BRIDGE_CACHE_MAX_SIZE, config->bridge_cache_max_size);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_JSON, (uintptr_t)config->bridge_json, ValueInt64);
   pgexporter_json_put_size_value(res, CONFIGURATION_ARGUMENT_BRIDGE_JSON_CACHE_MAX_SIZE, config->bridge_json_cache_max_size);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_HISTORY, (uintptr_t)config->history, ValueInt64);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_HISTORY_INTERVAL, config->history_interval, FORMAT_TIME_S);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_HISTORY_RETENTION, config->history_retention, FORMAT_TIME_S);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_HISTORY_BACKEND, config->history_backend, to_history_backend);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_HISTORY_PATH, (uintptr_t)config->history_path, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_HISTORY, (uintptr_t)config->bridge_history, ValueInt64);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_BRIDGE_HISTORY_INTERVAL, config->bridge_history_interval, FORMAT_TIME_S);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_BRIDGE_HISTORY_RETENTION, config->bridge_history_retention, FORMAT_TIME_S);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_BRIDGE_HISTORY_BACKEND, config->bridge_history_backend, to_history_backend);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_HISTORY_PATH, (uintptr_t)config->bridge_history_path, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_MANAGEMENT, (uintptr_t)config->management, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_ALERTS, (uintptr_t)config->alerts_enabled, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_CACHE, (uintptr_t)config->cache, ValueBool);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_LOG_TYPE, config->log_type, to_log_type);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_LOG_LEVEL, config->log_level, to_log_level);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_PATH, (uintptr_t)config->log_path, ValueString);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, config->log_rotation_age, FORMAT_TIME_S);
   pgexporter_json_put_size_value(res, CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, config->log_rotation_size);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_LINE_PREFIX, (uintptr_t)config->log_line_prefix, ValueString);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_LOG_MODE, config->log_mode, to_log_mode);
   pgexporter_json_put_time_value(res, CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, config->blocking_timeout, FORMAT_TIME_S);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS, (uintptr_t)config->tls, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS_CERT_FILE, (uintptr_t)config->tls_cert_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS_CA_FILE, (uintptr_t)config->tls_ca_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS_KEY_FILE, (uintptr_t)config->tls_key_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_CERT_FILE, (uintptr_t)config->metrics_cert_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_CA_FILE, (uintptr_t)config->metrics_ca_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_KEY_FILE, (uintptr_t)config->metrics_key_file, ValueString);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_EV_BACKEND, config->ev_backend, to_ev_backend);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_KEEP_ALIVE, (uintptr_t)config->keep_alive, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_NODELAY, (uintptr_t)config->nodelay, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_NON_BLOCKING, (uintptr_t)config->non_blocking, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BACKLOG, (uintptr_t)config->backlog, ValueInt64);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_HUGEPAGE, config->hugepage, to_hugepage);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_PIDFILE, (uintptr_t)config->pidfile, ValueString);
   pgexporter_json_put_enum_value(res, CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, config->update_process_title, to_update_process_title);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)config->configuration_path, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)config->users_path, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)config->admins_path, ValueString);

   free(data);
}

static void
add_servers_configuration_response(struct json* res)
{
   struct configuration* config = (struct configuration*)shmem;
   struct json* server_section = NULL;
   struct json* server_conf = NULL;

   // Create a server section to hold all server configurations
   if (pgexporter_json_create(&server_section))
   {
      pgexporter_log_error("Failed to create server section JSON");
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (pgexporter_json_create(&server_conf))
      {
         pgexporter_log_error("Failed to create server configuration JSON for %s",
                              config->servers[i].name);
         goto error;
      }

      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_HOST, (uintptr_t)config->servers[i].host, ValueString);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_PORT, (uintptr_t)config->servers[i].port, ValueInt64);
      pgexporter_json_put_enum_value(server_conf, CONFIGURATION_ARGUMENT_TLS, config->servers[i].tls_mode, to_server_tls_mode);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_TLS_CERT_FILE, (uintptr_t)config->servers[i].tls_cert_file, ValueString);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_TLS_KEY_FILE, (uintptr_t)config->servers[i].tls_key_file, ValueString);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_TLS_CA_FILE, (uintptr_t)config->servers[i].tls_ca_file, ValueString);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_USER, (uintptr_t)config->servers[i].username, ValueString);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_DATA_DIR, (uintptr_t)config->servers[i].data, ValueString);
      pgexporter_json_put(server_conf, CONFIGURATION_ARGUMENT_WAL_DIR, (uintptr_t)config->servers[i].wal, ValueString);

      // Add this server to the server section using server name as key
      pgexporter_json_put(server_section, config->servers[i].name, (uintptr_t)server_conf, ValueJSON);
      server_conf = NULL; // Prevent double free
   }

   // Add the server section to the main response
   pgexporter_json_put(res, "server", (uintptr_t)server_section, ValueJSON);
   return;

error:
   pgexporter_json_destroy(server_conf);
   pgexporter_json_destroy(server_section);
   return;
}

static void
extract_key_value(char* str, char** key, char** value)
{
   char* equal = NULL;
   char* end = NULL;
   char* ptr = NULL;
   char left[MISC_LENGTH];
   char right[MISC_LENGTH];
   bool start_left = false;
   bool start_right = false;
   int idx = 0;
   int i = 0;
   char c = 0;
   char* k = NULL;
   char* v = NULL;

   *key = NULL;
   *value = NULL;

   equal = strchr(str, '=');

   if (equal != NULL)
   {
      memset(&left[0], 0, sizeof(left));
      memset(&right[0], 0, sizeof(right));

      i = 0;
      while (true)
      {
         ptr = str + i;
         if (ptr != equal)
         {
            c = *(str + i);
            if (c == '\t' || c == ' ' || c == '\"' || c == '\'')
            {
               /* Skip */
            }
            else
            {
               start_left = true;
            }

            if (start_left)
            {
               left[idx] = c;
               idx++;
            }
         }
         else
         {
            break;
         }
         i++;
      }

      end = strchr(str, '\n');
      idx = 0;

      for (size_t i = 0; i < strlen(equal); i++)
      {
         ptr = equal + i;
         if (ptr != end)
         {
            c = *(ptr);
            if (c == '=' || c == ' ' || c == '\t' || c == '\"' || c == '\'')
            {
               /* Skip */
            }
            else
            {
               start_right = true;
            }

            if (start_right)
            {
               if (c != '#')
               {
                  right[idx] = c;
                  idx++;
               }
               else
               {
                  break;
               }
            }
         }
         else
         {
            break;
         }
      }

      for (int i = strlen(left); i >= 0; i--)
      {
         if (left[i] == '\t' || left[i] == ' ' || left[i] == '\0' || left[i] == '\"' || left[i] == '\'')
         {
            left[i] = '\0';
         }
         else
         {
            break;
         }
      }

      for (int i = strlen(right); i >= 0; i--)
      {
         if (right[i] == '\t' || right[i] == ' ' || right[i] == '\0' || right[i] == '\r' || right[i] == '\"' || right[i] == '\'')
         {
            right[i] = '\0';
         }
         else
         {
            break;
         }
      }

      k = calloc(1, strlen(left) + 1);
      v = calloc(1, strlen(right) + 1);

      pgexporter_snprintf(k, strlen(left) + 1, "%s", left);
      pgexporter_snprintf(v, strlen(right) + 1, "%s", right);

      *key = k;
      *value = v;
   }
}

/**
 * Given a line of text extracts the key part and the value
 * and expands environment variables in the value (like $HOME).
 * Valid lines must have the form <key> = <value>.
 *
 * The key must be unquoted and cannot have any spaces
 * in front of it.
 *
 * The value will be extracted as it is without trailing and leading spaces.
 *
 * Comments on the right side of a value are allowed.
 *
 * Example of valid lines are:
 * <code>
 * foo = bar
 * foo=bar
 * foo=  bar
 * foo = "bar"
 * foo = 'bar'
 * foo = "#bar"
 * foo = '#bar'
 * foo = bar # bar set!
 * foo = bar# bar set!
 * </code>
 *
 * @param str the line of text incoming from the configuration file
 * @param key the pointer to where to store the key extracted from the line
 * @param value the pointer to where to store the value (as it is)
 * @returns 1 if unable to parse the line, 0 if everything is ok
 */
static int
extract_syskey_value(char* str, char** key, char** value)
{
   int c = 0;
   int offset = 0;
   int length = strlen(str);
   int d = length - 1;
   char* k = NULL;
   char* v = NULL;

   // the key does not allow spaces and is whatever is
   // on the left of the '='
   while (str[c] != ' ' && str[c] != '=' && c < length)
   {
      c++;
   }

   if (c >= length)
   {
      goto error;
   }

   for (int i = 0; i < c; i++)
   {
      k = pgexporter_append_char(k, str[i]);
   }

   while (c < length && (str[c] == ' ' || str[c] == '\t' || str[c] == '=' || str[c] == '\r' || str[c] == '\n'))
   {
      c++;
   }

   if (c == length)
   {
      v = calloc(1, 1); // empty string
      *key = k;
      *value = v;
      return 0;
   }

   offset = c;

   while ((str[d] == ' ' || str[d] == '\t' || str[d] == '\r' || str[d] == '\n') && d > c)
   {
      d--;
   }

   for (int i = offset; i <= d; i++)
   {
      v = pgexporter_append_char(v, str[i]);
   }

   char* resolved_path = NULL;

   if (pgexporter_resolve_path(v, &resolved_path))
   {
      free(k);
      free(v);
      free(resolved_path);
      k = NULL;
      v = NULL;
      resolved_path = NULL;
      goto error;
   }

   free(v);
   v = resolved_path;

   *key = k;
   *value = v;
   return 0;

error:
   return 1;
}

static int
as_int(char* str, int* i)
{
   char* endptr;
   long val;

   errno = 0;
   val = strtol(str, &endptr, 10);

   if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
   {
      goto error;
   }

   if (str == endptr)
   {
      goto error;
   }

   if (*endptr != '\0')
   {
      goto error;
   }

   *i = (int)val;

   return 0;

error:

   errno = 0;

   return 1;
}

static int
as_long(char* str, long* l)
{
   char* endptr;
   long val;

   errno = 0;
   val = strtol(str, &endptr, 10);

   if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
       (errno != 0 && val == 0))
   {
      goto error;
   }

   if (str == endptr)
   {
      goto error;
   }

   if (*endptr != '\0')
   {
      goto error;
   }

   *l = val;

   return 0;

error:

   errno = 0;

   return 1;
}

static int
as_bool(char* str, bool* b)
{
   if (!strcasecmp(str, "true") || !strcasecmp(str, "on") || !strcasecmp(str, "yes") || !strcasecmp(str, "1"))
   {
      *b = true;
      return 0;
   }

   if (!strcasecmp(str, "false") || !strcasecmp(str, "off") || !strcasecmp(str, "no") || !strcasecmp(str, "0"))
   {
      *b = false;
      return 0;
   }

   return 1;
}

static int
as_logging_type(char* str)
{
   if (!strcasecmp(str, "console"))
   {
      return PGEXPORTER_LOGGING_TYPE_CONSOLE;
   }

   if (!strcasecmp(str, "file"))
   {
      return PGEXPORTER_LOGGING_TYPE_FILE;
   }

   if (!strcasecmp(str, "syslog"))
   {
      return PGEXPORTER_LOGGING_TYPE_SYSLOG;
   }

   return -1;
}

static int
as_server_type(char* str)
{
   if (!strcasecmp(str, "prometheus"))
   {
      return SERVER_TYPE_PROMETHEUS;
   }

   if (!strcasecmp(str, "postgresql"))
   {
      return SERVER_TYPE_POSTGRESQL;
   }

   return -1;
}

static int
as_logging_level(char* str)
{
   size_t size = 0;
   int debug_level = 1;
   char* debug_value = NULL;

   if (!strncasecmp(str, "debug", strlen("debug")))
   {
      if (strlen(str) > strlen("debug"))
      {
         size = strlen(str) - strlen("debug");
         debug_value = (char*)malloc(size + 1);
         memset(debug_value, 0, size + 1);
         memcpy(debug_value, str + 5, size);
         if (as_int(debug_value, &debug_level))
         {
            // cannot parse, set it to 1
            debug_level = 1;
         }
         free(debug_value);
      }

      if (debug_level <= 1)
      {
         return PGEXPORTER_LOGGING_LEVEL_DEBUG1;
      }
      else if (debug_level == 2)
      {
         return PGEXPORTER_LOGGING_LEVEL_DEBUG2;
      }
      else if (debug_level == 3)
      {
         return PGEXPORTER_LOGGING_LEVEL_DEBUG3;
      }
      else if (debug_level == 4)
      {
         return PGEXPORTER_LOGGING_LEVEL_DEBUG4;
      }
      else if (debug_level >= 5)
      {
         return PGEXPORTER_LOGGING_LEVEL_DEBUG5;
      }
   }

   if (!strcasecmp(str, "info"))
   {
      return PGEXPORTER_LOGGING_LEVEL_INFO;
   }

   if (!strcasecmp(str, "warn"))
   {
      return PGEXPORTER_LOGGING_LEVEL_WARN;
   }

   if (!strcasecmp(str, "error"))
   {
      return PGEXPORTER_LOGGING_LEVEL_ERROR;
   }

   if (!strcasecmp(str, "fatal"))
   {
      return PGEXPORTER_LOGGING_LEVEL_FATAL;
   }

   return -1;
}

static int
as_logging_mode(char* str)
{
   if (!strcasecmp(str, "a") || !strcasecmp(str, "append"))
   {
      return PGEXPORTER_LOGGING_MODE_APPEND;
   }

   if (!strcasecmp(str, "c") || !strcasecmp(str, "create"))
   {
      return PGEXPORTER_LOGGING_MODE_CREATE;
   }

   return -1;
}

static int
as_hugepage(char* str)
{
   if (!strcasecmp(str, "off"))
   {
      return HUGEPAGE_OFF;
   }

   if (!strcasecmp(str, "try"))
   {
      return HUGEPAGE_TRY;
   }

   if (!strcasecmp(str, "on"))
   {
      return HUGEPAGE_ON;
   }

   return HUGEPAGE_OFF;
}

static int
as_history_backend(char* str)
{
   if (!strcasecmp(str, "sqlite"))
   {
      return HISTORY_BACKEND_SQLITE;
   }

   return HISTORY_BACKEND_SQLITE;
}

static ev_backend_t
as_ev_backend(char* str)
{
   if (is_empty_string(str))
   {
      return PGEXPORTER_EVENT_BACKEND_EMPTY;
   }

   if (!strncasecmp(str, "auto", MISC_LENGTH))
   {
      return PGEXPORTER_EVENT_BACKEND_AUTO;
   }

   if (!strncasecmp(str, "io_uring", MISC_LENGTH))
   {
      return PGEXPORTER_EVENT_BACKEND_IO_URING;
   }

   if (!strncasecmp(str, "epoll", MISC_LENGTH))
   {
      return PGEXPORTER_EVENT_BACKEND_EPOLL;
   }

   if (!strncasecmp(str, "kqueue", MISC_LENGTH))
   {
      return PGEXPORTER_EVENT_BACKEND_KQUEUE;
   }

   return PGEXPORTER_EVENT_BACKEND_INVALID;
}

static bool
is_supported_backend(ev_backend_t backend)
{
   ev_backend_t supported_backends[] = {
#if HAVE_LINUX
#if HAVE_IO_URING
      PGEXPORTER_EVENT_BACKEND_IO_URING,
#endif
      PGEXPORTER_EVENT_BACKEND_EPOLL,
#else
      PGEXPORTER_EVENT_BACKEND_KQUEUE,
#endif
   };

   for (size_t i = 0; i < sizeof(supported_backends) / sizeof(supported_backends[0]); i++)
   {
      if (backend == supported_backends[i])
      {
         return true;
      }
   }

   return false;
}

static void
validate_event_backend(struct configuration* config)
{
   if (config->ev_backend == PGEXPORTER_EVENT_BACKEND_INVALID)
   {
      pgexporter_log_warn("Configured event backend is invalid. Default to 'auto'");
      config->ev_backend = PGEXPORTER_EVENT_BACKEND_AUTO;
   }

   if (config->ev_backend == PGEXPORTER_EVENT_BACKEND_EMPTY)
   {
      pgexporter_log_warn("ev_backend configuration is empty. Default to 'auto'");
      config->ev_backend = PGEXPORTER_EVENT_BACKEND_AUTO;
   }

   if (config->ev_backend == PGEXPORTER_EVENT_BACKEND_AUTO || !is_supported_backend(config->ev_backend))
   {
      if (config->ev_backend != PGEXPORTER_EVENT_BACKEND_AUTO)
      {
         pgexporter_log_warn("Configured backend '%s' is unsupported", ev_backend_to_string(config->ev_backend));
      }
      config->ev_backend = DEFAULT_EVENT_BACKEND;
   }

#if HAVE_LINUX && HAVE_IO_URING
   if (config->ev_backend == PGEXPORTER_EVENT_BACKEND_IO_URING)
   {
      FILE* fp;
      int rval;

      fp = fopen("/proc/sys/kernel/io_uring_disabled", "r");
      if (fp == NULL)
      {
         pgexporter_log_debug("Failed to open /proc/sys/kernel/io_uring_disabled: %s", strerror(errno));
         config->ev_backend = PGEXPORTER_EVENT_BACKEND_EPOLL;
         return;
      }

      rval = fgetc(fp);
      if (fclose(fp) != 0)
      {
         pgexporter_log_warn("Failed to close /proc/sys/kernel/io_uring_disabled: %s", strerror(errno));
      }

      if (rval == '1' || rval == '2')
      {
         pgexporter_log_warn("io_uring supported but disabled by kernel; falling back to epoll");
         config->ev_backend = PGEXPORTER_EVENT_BACKEND_EPOLL;
      }
   }
#endif

   pgexporter_log_debug("Selected backend '%s'", ev_backend_to_string(config->ev_backend));
}

static const char*
ev_backend_to_string(ev_backend_t backend)
{
   switch (backend)
   {
      case PGEXPORTER_EVENT_BACKEND_AUTO:
         return "auto";
      case PGEXPORTER_EVENT_BACKEND_IO_URING:
         return "io_uring";
      case PGEXPORTER_EVENT_BACKEND_EPOLL:
         return "epoll";
      case PGEXPORTER_EVENT_BACKEND_KQUEUE:
         return "kqueue";
      case PGEXPORTER_EVENT_BACKEND_EMPTY:
         return "";
      case PGEXPORTER_EVENT_BACKEND_INVALID:
      default:
         return "unknown";
   }
}

/**
 * Parses a string to see if it contains
 * a valid value for update_process_title.
 *
 * @return The policy value, or -1 on error
 */
static int
as_update_process_title(char* str)
{
   if (is_empty_string(str))
   {
      return -1;
   }

   if (!strncmp(str, "never", MISC_LENGTH) || !strncmp(str, "off", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_NEVER;
   }
   else if (!strncmp(str, "strict", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_STRICT;
   }
   else if (!strncmp(str, "minimal", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_MINIMAL;
   }
   else if (!strncmp(str, "verbose", MISC_LENGTH) || !strncmp(str, "full", MISC_LENGTH))
   {
      return UPDATE_PROCESS_TITLE_VERBOSE;
   }

   return -1;
}

static int
as_logging_rotation_size(char* str, size_t* size)
{
   long l = 0;
   int ret;

   ret = as_bytes(str, &l, PGEXPORTER_LOGGING_ROTATION_DISABLED);

   *size = (size_t)l;

   return ret;
}

/**
 * Parses the log_rotation_age string.
 * The string accepts:
 * - ms for milliseconds
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in milliseconds.
 * Returns 1 for errors, 0 for correct parsing.
 *
 * @param str the value to parse as retrieved from the configuration
 * @param time a pointer to the value that is going to store
 *        the resulting number of milliseconds
 * @return 0 on success, 1 on error
 */
static int
as_logging_rotation_age(char* str, pgexporter_time_t* time)
{
   return as_milliseconds(str, time, PGEXPORTER_TIME_DISABLED);
}

/**
 * Parses an age string, providing the resulting value as milliseconds.
 * An age string is expressed by a number and a suffix that indicates
 * the multiplier. Accepted suffixes, case insensitive, are:
 * - ms for milliseconds
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in milliseconds.
 *
 * @param str the value to parse as retrieved from the configuration
 * @param age a pointer to the value that is going to store
 *        the resulting number of milliseconds
 * @param default_age a value to set when the parsing is unsuccessful
 *
 */
static int
as_milliseconds(char* str, pgexporter_time_t* age, pgexporter_time_t default_age)
{
   int64_t multiplier = 1000;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   int i_value = 0;

   if (is_empty_string(str))
   {
      *age = default_age;
      return 0;
   }

   index = 0;
   for (size_t i = 0; i < strlen(str); i++)
   {
      if (isdigit(str[i]))
      {
         value[index++] = str[i];
      }
      else if (isalpha(str[i]) && multiplier_set)
      {
         // another extra char not allowed
         goto error;
      }
      else if (isalpha(str[i]) && !multiplier_set)
      {
         // Check for 'ms'
         if ((str[i] == 'm' || str[i] == 'M') &&
             i + 1 < strlen(str) &&
             (str[i + 1] == 's' || str[i + 1] == 'S'))
         {
            multiplier = 1;
            multiplier_set = true;
            i++; // Skip the 's' in 'ms'
         }
         else if (str[i] == 's' || str[i] == 'S')
         {
            multiplier = 1000;
            multiplier_set = true;
         }
         else if (str[i] == 'm' || str[i] == 'M')
         {
            multiplier = 60 * 1000;
            multiplier_set = true;
         }
         else if (str[i] == 'h' || str[i] == 'H')
         {
            multiplier = 3600 * 1000;
            multiplier_set = true;
         }
         else if (str[i] == 'd' || str[i] == 'D')
         {
            multiplier = 24 * 3600 * 1000;
            multiplier_set = true;
         }
         else if (str[i] == 'w' || str[i] == 'W')
         {
            multiplier = 7 * 24 * 3600 * 1000;
            multiplier_set = true;
         }
         else
         {
            goto error;
         }
      }
      else
      {
         // do not allow alien chars
         goto error;
      }
   }

   value[index] = '\0';
   if (!as_int(value, &i_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (i_value >= 0)
      {
         *age = PGEXPORTER_TIME_MS(i_value * multiplier);
      }
      else
      {
         goto error;
      }

      return 0;
   }
   else
   {
error:
      *age = default_age;
      return 1;
   }
}

/**
 * Converts a "size string" into the number of bytes.
 *
 * Valid strings have one of the suffixes:
 * - b for bytes (default)
 * - k for kilobytes
 * - m for megabytes
 * - g for gigabytes
 *
 * The default is expressed always as bytes.
 * Uppercase letters work too.
 * If no suffix is specified, the value is expressed as bytes.
 *
 * @param str the string to parse (e.g., "2M")
 * @param bytes the value to set as result of the parsing stage
 * @param default_bytes the default value to set when the parsing cannot proceed
 * @return 1 if parsing is unable to understand the string, 0 is parsing is
 *         performed correctly (or almost correctly, e.g., empty string)
 */
static int
as_bytes(char* str, long* bytes, long default_bytes)
{
   int multiplier = 1;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   long l_value = default_bytes;

   if (is_empty_string(str))
   {
      *bytes = default_bytes;
      return 0;
   }

   index = 0;
   for (size_t i = 0; i < strlen(str); i++)
   {
      if (isdigit(str[i]))
      {
         value[index++] = str[i];
      }
      else if (isalpha(str[i]) && multiplier_set)
      {
         // allow a 'B' suffix on a multiplier
         // like for instance 'MB', but don't allow it
         // for bytes themselves ('BB')
         if (multiplier == 1 || (str[i] != 'b' && str[i] != 'B'))
         {
            // another non-digit char not allowed
            goto error;
         }
      }
      else if (isalpha(str[i]) && !multiplier_set)
      {
         if (str[i] == 'M' || str[i] == 'm')
         {
            multiplier = 1024 * 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'G' || str[i] == 'g')
         {
            multiplier = 1024 * 1024 * 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'K' || str[i] == 'k')
         {
            multiplier = 1024;
            multiplier_set = true;
         }
         else if (str[i] == 'B' || str[i] == 'b')
         {
            multiplier = 1;
            multiplier_set = true;
         }
      }
      else
      {
         // do not allow alien chars
         goto error;
      }
   }

   value[index] = '\0';
   if (!as_long(value, &l_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (l_value >= 0)
      {
         *bytes = l_value * multiplier;
      }
      else
      {
         goto error;
      }

      return 0;
   }
   else
   {
error:
      *bytes = default_bytes;
      return 1;
   }
}

static int
as_endpoints(char* str, struct configuration* config, bool reload)
{
   int idx = 0;
   char* token = NULL;
   char host[MISC_LENGTH] = {0};
   char port[6] = {0};

   token = strtok((char*)str, ",");

   while (token != NULL && idx < NUMBER_OF_ENDPOINTS)
   {
      char* t = token;
      char* n = NULL;

      n = pgexporter_remove_whitespace(t);
      t = n;

      n = pgexporter_remove_prefix(t, "https://");
      free(t);
      t = n;

      n = pgexporter_remove_prefix(t, "http://");
      free(t);
      t = n;

      n = pgexporter_remove_suffix(t, "/metrics");
      free(t);
      t = n;

      n = pgexporter_remove_suffix(t, "/");
      free(t);
      t = n;

      /*
       * Each endpoint is host:port.
       * Host is of length [0, 127].
       * Port is of length [0, 5] (16-bit unsigned integer).
       */
      if (sscanf(t, "%127[^:]:%5s", host, port) == 2)
      {
         bool found = false;

         if (!reload)
         {
            for (int i = 0; i <= idx; i++)
            {
               if (!strcmp(config->endpoints[i].host, host) && config->endpoints[i].port == atoi(port))
               {
                  found = true;
               }
            }
         }

         if (!found)
         {
            pgexporter_snprintf(config->endpoints[idx].host, MISC_LENGTH, "%s", host);
            config->endpoints[idx].port = atoi(port);

            pgexporter_log_trace("Bridge Endpoint %d | Host: %s, Port: %s", idx, host, port);

            idx++;
         }
         else
         {
            pgexporter_log_warn("Duplicated endpoint: %s:%s", host, port);
         }

         memset(host, 0, sizeof(host));
         memset(port, 0, sizeof(port));
      }
      else
      {
         pgexporter_log_error("Error parsing endpoint: %s", token);
         goto error;
      }

      free(t);

      token = strtok(NULL, ",");
   }

   config->number_of_endpoints = idx;

   return 0;

error:

   memset(config->endpoints, 0, sizeof(config->endpoints));
   config->number_of_endpoints = 0;

   return 1;
}

/**
 * Check if two servers have the same host and port
 */
static bool
is_same_server(struct server* s1, struct server* s2)
{
   if (strcmp(s1->host, s2->host))
   {
      return false;
   }
   if (s1->port != s2->port)
   {
      return false;
   }
   return true;
}

/**
 * Check if a server configuration change requires restart
 */
static int
restart_server(struct server* new_srv, struct server* current_srv)
{
   char restart_message[2 * MISC_LENGTH];

   if (!is_same_server(new_srv, current_srv))
   {
      pgexporter_snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <host>", new_srv->name);
      restart_string(restart_message, current_srv->host, new_srv->host);
      pgexporter_snprintf(restart_message, sizeof(restart_message), "Server <%s>, parameter <port>", new_srv->name);
      restart_int(restart_message, current_srv->port, new_srv->port);
      return 1;
   }

   return 0;
}

/**
 * Check if any structural parameter requires a restart.
 * This function checks ALL structural parameters before any changes are applied.
 * 
 * @param config The current running configuration
 * @param reload The new configuration to be applied
 * @return True if restart is required, false otherwise
 */
static bool
check_restart_required(struct configuration* config, struct configuration* reload)
{
   bool restart = false;

   /* Network binding - all ports require restart */
   if (restart_string("host", config->host, reload->host))
   {
      restart = true;
   }
   if (restart_int("metrics", config->metrics, reload->metrics))
   {
      restart = true;
   }
   if (restart_int("management", config->management, reload->management))
   {
      restart = true;
   }
   if (restart_int("console", config->console, reload->console))
   {
      restart = true;
   }
   if (restart_int("history", config->history, reload->history))
   {
      restart = true;
   }
   if (restart_int("bridge", config->bridge, reload->bridge))
   {
      restart = true;
   }
   if (restart_int("bridge_json", config->bridge_json, reload->bridge_json))
   {
      restart = true;
   }
   if (restart_int("bridge_history", config->bridge_history, reload->bridge_history))
   {
      restart = true;
   }

   /* Logging infrastructure */
   if (restart_int("log_type", config->log_type, reload->log_type))
   {
      restart = true;
   }

   /* System configuration */
   if (restart_string("unix_socket_dir", config->unix_socket_dir, reload->unix_socket_dir))
   {
      restart = true;
   }
   if (restart_string("pidfile", config->pidfile, reload->pidfile))
   {
      restart = true;
   }
   if (restart_int("ev_backend", config->ev_backend, reload->ev_backend))
   {
      restart = true;
   }
   if (restart_int("hugepage", config->hugepage, reload->hugepage))
   {
      restart = true;
   }

   /* Cache infrastructure */
   if (restart_int("metrics_cache_max_size", config->metrics_cache_max_size, reload->metrics_cache_max_size))
   {
      restart = true;
   }

   /* Check if number of servers decreased */
   if (config->number_of_servers > reload->number_of_servers)
   {
      if (restart_int("decreasing number of servers", config->number_of_servers, reload->number_of_servers))
      {
         restart = true;
      }
   }

   /* Check each server for host/port/TLS changes */
   for (int i = 0; i < reload->number_of_servers; i++)
   {
      if (i < config->number_of_servers)
      {
         if (restart_server(&reload->servers[i], &config->servers[i]))
         {
            restart = true;
         }
      }
   }

   return restart;
}

static int
transfer_configuration(struct configuration* config, struct configuration* reload)
{
   char* old_endpoints = NULL;
   char* new_endpoints = NULL;

#ifdef HAVE_SYSTEMD
   sd_notify(0, "RELOADING=1");
#endif

   /* Check if any parameter requires a restart before applying any changes */
   if (check_restart_required(config, reload))
   {
      pgexporter_log_warn("Configuration reload denied: restart required for one or more structural parameters. Running state preserved.");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "READY=1");
#endif
      return 1;
   }

   /* No restart required: apply all changes to the shared memory.
    * Structural parameters (host, metrics, console, management, etc.) are safe
    * to copy here because check_restart_required() above verified they are unchanged.
    */

   /* Network resources */
   memcpy(config->host, reload->host, MISC_LENGTH);
   config->metrics = reload->metrics;
   config->metrics_cache_max_age = reload->metrics_cache_max_age;
   config->metrics_cache_max_size = reload->metrics_cache_max_size;
   config->metrics_query_timeout = reload->metrics_query_timeout;
   config->console = reload->console;
   config->management = reload->management;

   /* Bridge configuration */
   config->bridge = reload->bridge;

   if (config->number_of_endpoints > 0)
   {
      for (int i = 0; i < config->number_of_endpoints; i++)
      {
         old_endpoints = pgexporter_append(old_endpoints, config->endpoints[i].host);
         old_endpoints = pgexporter_append_char(old_endpoints, ':');
         old_endpoints = pgexporter_append_int(old_endpoints, config->endpoints[i].port);

         if (i < config->number_of_endpoints - 1)
         {
            old_endpoints = pgexporter_append_char(old_endpoints, ',');
         }
      }
   }
   else
   {
      old_endpoints = pgexporter_append(old_endpoints, "");
   }

   if (reload->number_of_endpoints > 0)
   {
      for (int i = 0; i < reload->number_of_endpoints; i++)
      {
         new_endpoints = pgexporter_append(new_endpoints, reload->endpoints[i].host);
         new_endpoints = pgexporter_append_char(new_endpoints, ':');
         new_endpoints = pgexporter_append_int(new_endpoints, reload->endpoints[i].port);

         if (i < reload->number_of_endpoints - 1)
         {
            new_endpoints = pgexporter_append_char(new_endpoints, ',');
         }
      }
   }
   else
   {
      new_endpoints = pgexporter_append(new_endpoints, "");
   }

   config->bridge_cache_max_age = reload->bridge_cache_max_age;
   config->bridge_cache_max_size = reload->bridge_cache_max_size;
   config->bridge_json = reload->bridge_json;
   config->bridge_json_cache_max_size = reload->bridge_json_cache_max_size;

   /* History */
   config->history = reload->history;
   config->history_interval = reload->history_interval;
   config->history_retention = reload->history_retention;
   config->history_backend = reload->history_backend;
   memcpy(config->history_path, reload->history_path, MAX_PATH);

   /* Bridge history */
   config->bridge_history = reload->bridge_history;
   config->bridge_history_interval = reload->bridge_history_interval;
   config->bridge_history_retention = reload->bridge_history_retention;
   config->bridge_history_backend = reload->bridge_history_backend;
   memcpy(config->bridge_history_path, reload->bridge_history_path, MAX_PATH);

   config->cache = reload->cache;
   config->alerts_enabled = reload->alerts_enabled;
   config->tls = reload->tls;

   /* Logging */
   config->log_type = reload->log_type;
   config->log_level = reload->log_level;

   /* If the log main parameters have changed, restart the logging system */
   if (strncmp(config->log_path, reload->log_path, MISC_LENGTH) ||
       config->log_rotation_size != reload->log_rotation_size ||
       pgexporter_time_convert(config->log_rotation_age, FORMAT_TIME_MS) != pgexporter_time_convert(reload->log_rotation_age, FORMAT_TIME_MS) ||
       config->log_mode != reload->log_mode)
   {
      pgexporter_log_debug("Log restart triggered!");
      pgexporter_stop_logging();
      config->log_rotation_size = reload->log_rotation_size;
      config->log_rotation_age = reload->log_rotation_age;
      config->log_mode = reload->log_mode;
      memcpy(config->log_line_prefix, reload->log_line_prefix, MISC_LENGTH);
      memcpy(config->log_path, reload->log_path, MISC_LENGTH);
      pgexporter_start_logging();
   }

   /* TLS - changes apply to new connections immediately */
   memcpy(config->tls_cert_file, reload->tls_cert_file, MAX_PATH);
   memcpy(config->tls_key_file, reload->tls_key_file, MAX_PATH);
   memcpy(config->tls_ca_file, reload->tls_ca_file, MAX_PATH);
   memcpy(config->metrics_cert_file, reload->metrics_cert_file, MAX_PATH);
   memcpy(config->metrics_key_file, reload->metrics_key_file, MAX_PATH);
   memcpy(config->metrics_ca_file, reload->metrics_ca_file, MAX_PATH);

   /* Timeouts */
   config->blocking_timeout = reload->blocking_timeout;
   config->authentication_timeout = reload->authentication_timeout;

   /* System */
   memcpy(config->pidfile, reload->pidfile, MAX_PATH);
   config->ev_backend = reload->ev_backend;
   config->keep_alive = reload->keep_alive;
   config->nodelay = reload->nodelay;
   config->non_blocking = reload->non_blocking;
   config->backlog = reload->backlog;
   config->hugepage = reload->hugepage;
   config->update_process_title = reload->update_process_title;
   memcpy(config->unix_socket_dir, reload->unix_socket_dir, MISC_LENGTH);

   /* Collectors */
   memcpy(config->allowed_collectors, reload->allowed_collectors, sizeof(config->allowed_collectors));
   config->number_of_allowed_collectors = reload->number_of_allowed_collectors;
   memcpy(config->excluded_collectors, reload->excluded_collectors, sizeof(config->excluded_collectors));
   config->number_of_excluded_collectors = reload->number_of_excluded_collectors;

   /* Servers */
   for (int i = 0; i < reload->number_of_servers; i++)
   {
      copy_server(&config->servers[i], &reload->servers[i]);
   }
   memset(&config->servers[reload->number_of_servers], 0, sizeof(struct server) * (NUMBER_OF_SERVERS - reload->number_of_servers));
   config->number_of_servers = reload->number_of_servers;

   /* Users */
   memset(&config->users[0], 0, sizeof(struct user) * NUMBER_OF_USERS);
   for (int i = 0; i < reload->number_of_users; i++)
   {
      copy_user(&config->users[i], &reload->users[i]);
   }
   config->number_of_users = reload->number_of_users;

   /* Admins */
   memset(&config->admins[0], 0, sizeof(struct user) * NUMBER_OF_ADMINS);
   for (int i = 0; i < reload->number_of_admins; i++)
   {
      copy_user(&config->admins[i], &reload->admins[i]);
   }
   config->number_of_admins = reload->number_of_admins;

   /* Prometheus */
   memcpy(config->metrics_path, reload->metrics_path, MAX_PATH);
   for (int i = 0; i < reload->number_of_metrics; i++)
   {
      copy_promethus(&config->prometheus[i], &reload->prometheus[i]);
   }
   config->number_of_metrics = reload->number_of_metrics;

   /* Metric names list */
   memcpy(config->metric_names, reload->metric_names, sizeof(config->metric_names));
   config->number_of_metric_names = reload->number_of_metric_names;

   /* Alerts */
   memcpy(config->alerts_path, reload->alerts_path, MAX_PATH);
   for (int i = 0; i < reload->number_of_alerts; i++)
   {
      memcpy(&config->alerts[i], &reload->alerts[i], sizeof(struct alert_definition));
   }
   config->number_of_alerts = reload->number_of_alerts;

   /* Endpoints */
   for (int i = 0; i < reload->number_of_endpoints; i++)
   {
      copy_endpoint(&config->endpoints[i], &reload->endpoints[i]);
   }
   config->number_of_endpoints = reload->number_of_endpoints;

#ifdef HAVE_SYSTEMD
   sd_notify(0, "READY=1");
#endif

   free(old_endpoints);
   free(new_endpoints);

   return 0;
}

static void
copy_server(struct server* dst, struct server* src)
{
   SSL* ssl = NULL;
   int fd = -1;
   bool is_new = false;
   int state = SERVER_UNKNOWN;
   int version = SERVER_UNDERTERMINED_VERSION;
   int minor_version = 0;
   int number_of_databases = 0;
   int number_of_extensions = 0;
   int fips_enabled = SERVER_FIPS_UNKNOWN;
   char databases[NUMBER_OF_EXTENSIONS][DB_NAME_LENGTH];
   struct extension_info extensions[NUMBER_OF_EXTENSIONS];
   bool preserve_runtime = is_same_server(dst, src);

   memset(databases, 0, sizeof(databases));
   memset(extensions, 0, sizeof(extensions));

   if (preserve_runtime)
   {
      ssl = dst->ssl;
      fd = dst->fd;
      is_new = dst->new;
      state = dst->state;
      version = dst->version;
      minor_version = dst->minor_version;
      number_of_databases = dst->number_of_databases;
      number_of_extensions = dst->number_of_extensions;
      fips_enabled = dst->fips_enabled;
      memcpy(databases, dst->databases, sizeof(databases));
      memcpy(extensions, dst->extensions, sizeof(extensions));
   }

   memset(dst, 0, sizeof(struct server));
   memcpy(&dst->name[0], &src->name[0], MISC_LENGTH);
   memcpy(&dst->host[0], &src->host[0], MISC_LENGTH);
   dst->port = src->port;
   dst->type = src->type;
   dst->tls_mode = src->tls_mode;
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->data[0], &src->data[0], MISC_LENGTH);
   memcpy(&dst->wal[0], &src->wal[0], MISC_LENGTH);
   memcpy(&dst->tls_cert_file[0], &src->tls_cert_file[0], MAX_PATH);
   memcpy(&dst->tls_key_file[0], &src->tls_key_file[0], MAX_PATH);
   memcpy(&dst->tls_ca_file[0], &src->tls_ca_file[0], MAX_PATH);
   memcpy(&dst->extensions_config[0], &src->extensions_config[0], MAX_EXTENSIONS_CONFIG_LENGTH);
   dst->ssl = ssl;
   dst->fd = fd;
   dst->new = is_new;
   dst->state = state;
   dst->version = version;
   dst->minor_version = minor_version;
   dst->number_of_databases = number_of_databases;
   dst->number_of_extensions = number_of_extensions;
   dst->fips_enabled = fips_enabled;
   memcpy(dst->databases, databases, sizeof(databases));
   memcpy(dst->extensions, extensions, sizeof(extensions));
}

static void
copy_server_config(struct server* dst, struct server* src)
{
   memset(dst, 0, sizeof(struct server));
   memcpy(&dst->name[0], &src->name[0], MISC_LENGTH);
   memcpy(&dst->host[0], &src->host[0], MISC_LENGTH);
   dst->port = src->port;
   dst->type = src->type;
   dst->tls_mode = src->tls_mode;
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->data[0], &src->data[0], MISC_LENGTH);
   memcpy(&dst->wal[0], &src->wal[0], MISC_LENGTH);
   memcpy(&dst->tls_cert_file[0], &src->tls_cert_file[0], MAX_PATH);
   memcpy(&dst->tls_key_file[0], &src->tls_key_file[0], MAX_PATH);
   memcpy(&dst->tls_ca_file[0], &src->tls_ca_file[0], MAX_PATH);
   memcpy(&dst->extensions_config[0], &src->extensions_config[0], MAX_EXTENSIONS_CONFIG_LENGTH);
   memcpy(dst->databases, src->databases, sizeof(dst->databases));
   memcpy(dst->extensions, src->extensions, sizeof(dst->extensions));
   /* Runtime fields (ssl, fd, state, version, etc.) remain zero/NULL */
}

static void
copy_user(struct user* dst, struct user* src)
{
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->password[0], &src->password[0], MAX_PASSWORD_LENGTH);
}

static void
copy_promethus(struct prometheus* dst, struct prometheus* src)
{
   memcpy(dst->tag, src->tag, MISC_LENGTH);
   memcpy(dst->collector, src->collector, MAX_COLLECTOR_LENGTH);
   dst->sort_type = src->sort_type;
   dst->server_query_type = src->server_query_type;

   // Always free dst's tree if it exists before copying
   if (dst->pg_root != NULL)
   {
      pgexporter_free_pg_node_avl(&dst->pg_root);
      dst->pg_root = NULL;
   }

   // Copy src tree to dst
   pgexporter_copy_pg_query_alts(&dst->pg_root, src->pg_root);

   // Same for extension tree
   if (dst->ext_root != NULL)
   {
      pgexporter_free_extension_node_avl(&dst->ext_root);
      dst->ext_root = NULL;
   }

   pgexporter_copy_extension_query_alts(src->ext_root, &dst->ext_root);
}

static void
copy_endpoint(struct endpoint* dst, struct endpoint* src)
{
   memcpy(dst->host, src->host, MISC_LENGTH);
   dst->port = src->port;
}

static int
restart_int(char* name, int e, int n)
{
   if (e != n)
   {
      pgexporter_log_warn("Restart required for %s - Existing %d New %d", name, e, n);
      return 1;
   }

   return 0;
}

static int
restart_string(char* name, char* e, char* n)
{
   if (strcmp(e, n))
   {
      pgexporter_log_warn("Restart required for %s - Existing %s New %s", name, e, n);
      return 1;
   }

   return 0;
}

static bool
is_empty_string(char* s)
{
   if (s == NULL)
   {
      return true;
   }

   if (!strcmp(s, ""))
   {
      return true;
   }

   for (size_t i = 0; i < strlen(s); i++)
   {
      if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
      {
         /* Ok */
      }
      else
      {
         return false;
      }
   }

   return true;
}

static bool
pgexporter_is_binary_file(const char* path)
{
   FILE* fp = NULL;
   unsigned char buffer[1024];
   size_t bytes;
   int error;

   fp = fopen(path, "rb");
   if (fp == NULL)
   {
      goto error;
   }

   while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
   {
      for (size_t i = 0; i < bytes; i++)
      {
         if (buffer[i] == '\0' || buffer[i] > 0X7F)
         {
            fclose(fp);
            goto error;
         }
      }
   }

   error = ferror(fp);
   fclose(fp);

   return error != 0;

error:
   return true;
}

static int
to_log_type(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case PGEXPORTER_LOGGING_TYPE_FILE:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "file");
         break;
      case PGEXPORTER_LOGGING_TYPE_CONSOLE:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "console");
         break;
      case PGEXPORTER_LOGGING_TYPE_SYSLOG:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "syslog");
         break;
      default:
         return 1;
   }
   return 0;
}

static int
to_log_level(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case PGEXPORTER_LOGGING_LEVEL_DEBUG1:
      case PGEXPORTER_LOGGING_LEVEL_DEBUG2:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "debug");
         break;
      case PGEXPORTER_LOGGING_LEVEL_INFO:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "info");
         break;
      case PGEXPORTER_LOGGING_LEVEL_WARN:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "warn");
         break;
      case PGEXPORTER_LOGGING_LEVEL_ERROR:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "error");
         break;
      case PGEXPORTER_LOGGING_LEVEL_FATAL:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "fatal");
         break;
      default:
         return 1;
   }
   return 0;
}

static int
to_log_mode(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case PGEXPORTER_LOGGING_MODE_CREATE:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "create");
         break;
      case PGEXPORTER_LOGGING_MODE_APPEND:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "append");
         break;
      default:
         return 1;
   }
   return 0;
}

static int
to_ev_backend(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case PGEXPORTER_EVENT_BACKEND_AUTO:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "auto");
         break;
      case PGEXPORTER_EVENT_BACKEND_IO_URING:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "io_uring");
         break;
      case PGEXPORTER_EVENT_BACKEND_EPOLL:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "epoll");
         break;
      case PGEXPORTER_EVENT_BACKEND_KQUEUE:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "kqueue");
         break;
      default:
         return 1;
   }
   return 0;
}

static int
to_server_tls_mode(char* where, int value)
{
   if (!where)
   {
      return 1;
   }

   switch (value)
   {
      case SERVER_TLS_OFF:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "off");
         break;
      case SERVER_TLS_TRY:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "try");
         break;
      case SERVER_TLS_ON:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "on");
         break;
      default:
         return 1;
   }

   return 0;
}

static int
to_hugepage(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case HUGEPAGE_OFF:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "off");
         break;
      case HUGEPAGE_TRY:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "try");
         break;
      case HUGEPAGE_ON:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "on");
         break;
      default:
         return 1;
   }
   return 0;
}

static int
to_history_backend(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case HISTORY_BACKEND_SQLITE:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "sqlite");
         break;
      default:
         return 1;
   }
   return 0;
}

static int
to_update_process_title(char* where, int value)
{
   if (!where)
   {
      return 1;
   }
   switch (value)
   {
      case UPDATE_PROCESS_TITLE_NEVER:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "never");
         break;
      case UPDATE_PROCESS_TITLE_STRICT:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "strict");
         break;
      case UPDATE_PROCESS_TITLE_MINIMAL:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "minimal");
         break;
      case UPDATE_PROCESS_TITLE_VERBOSE:
         pgexporter_snprintf(where, MISC_LENGTH, "%s", "verbose");
         break;
      default:
         return 1;
   }
   return 0;
}
