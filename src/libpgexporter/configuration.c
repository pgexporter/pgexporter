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

/* pgexporter */
#include <pgexporter.h>
#include <aes.h>
#include <bridge.h>
#include <configuration.h>
#include <ext_query_alts.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <pg_query_alts.h>
#include <prometheus.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <yaml_configuration.h>

/* system */
#include <ctype.h>
#include <err.h>
#include <errno.h>
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
static unsigned int as_update_process_title(char* str, unsigned int default_policy);
static int as_logging_rotation_size(char* str, size_t* size);
static int as_logging_rotation_age(char* str, int* age);
static int as_seconds(char* str, int* age, int default_age);
static int as_bytes(char* str, long* bytes, long default_bytes);
static int as_endpoints(char* str, struct configuration* config, bool reload);
static bool transfer_configuration(struct configuration* config, struct configuration* reload);
static void copy_server(struct server* dst, struct server* src);
static void copy_user(struct user* dst, struct user* src);
static void copy_promethus(struct prometheus* dst, struct prometheus* src);
static void copy_endpoint(struct endpoint* dst, struct endpoint* src);
static int restart_int(char* name, int e, int n);
static int restart_string(char* name, char* e, char* n);

static bool is_empty_string(char* s);

static void add_configuration_response(struct json* res);
static void add_servers_configuration_response(struct json* res);

/**
 *
 */
int
pgexporter_init_configuration(void* shm)
{
   struct configuration* config;

   config = (struct configuration*)shm;

   config->metrics = -1;
   config->cache = true;

   config->bridge = -1;
   config->bridge_cache_max_age = 300;
   config->bridge_cache_max_size = PROMETHEUS_DEFAULT_BRIDGE_CACHE_SIZE;
   config->bridge_json = -1;
   config->bridge_json_cache_max_size = PROMETHEUS_DEFAULT_BRIDGE_JSON_CACHE_SIZE;

   config->tls = false;

   config->blocking_timeout = 30;
   config->authentication_timeout = 5;

   config->keep_alive = true;
   config->nodelay = true;
   config->non_blocking = true;
   config->backlog = 16;
   config->hugepage = HUGEPAGE_TRY;

   config->update_process_title = UPDATE_PROCESS_TITLE_VERBOSE;

   config->log_type = PGEXPORTER_LOGGING_TYPE_CONSOLE;
   config->log_level = PGEXPORTER_LOGGING_LEVEL_INFO;
   config->log_mode = PGEXPORTER_LOGGING_MODE_APPEND;
   atomic_init(&config->log_lock, STATE_FREE);

   atomic_init(&config->logging_info, 0);
   atomic_init(&config->logging_warn, 0);
   atomic_init(&config->logging_error, 0);
   atomic_init(&config->logging_fatal, 0);

   for (int i = 0; i < NUMBER_OF_METRICS; i++)
   {
      config->prometheus[i].sort_type = SORT_NAME;
      config->prometheus[i].server_query_type = SERVER_QUERY_BOTH;
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
                  memcpy(&srv.name, &section, strlen(section));
                  srv.fd = -1;
                  srv.extension = true;
                  srv.state = SERVER_UNKNOWN;
                  srv.version = SERVER_UNDERTERMINED_VERSION;

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
            if (pgexporter_starts_with(line, "unix_socket_dir") || pgexporter_starts_with(line, "metrics_path")
                || pgexporter_starts_with(line, "log_path") || pgexporter_starts_with(line, "tls_cert_file")
                || pgexporter_starts_with(line, "tls_key_file") || pgexporter_starts_with(line, "tls_ca_file")
                || pgexporter_starts_with(line, "metrics_cert_file") || pgexporter_starts_with(line, "metrics_key_file")
                || pgexporter_starts_with(line, "metrics_ca_file"))
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
                     if (as_seconds(value, &config->metrics_cache_max_age, 0))
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
                     if (as_seconds(value, &config->bridge_cache_max_age, 300))
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
                     unknown = true;
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
                     if (as_int(value, &config->blocking_timeout))
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
                     config->update_process_title = as_update_process_title(value, UPDATE_PROCESS_TITLE_VERBOSE);
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
               else if (!strcmp(key, "libev"))
               {
                  if (!strcmp(section, "pgexporter"))
                  {
                     max = strlen(value);
                     if (max > MISC_LENGTH - 1)
                     {
                        max = MISC_LENGTH - 1;
                     }
                     memcpy(config->libev, value, max);
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
               else
               {
                  unknown = true;
               }

               if (unknown)
               {
                  warnx("Unknown: Section=%s, Key=%s, Value=%s", strlen(section) > 0 ? section : "<unknown>", key, value);
               }

               free(key);
               free(value);
               key = NULL;
               value = NULL;
            }
            else
            {
               warnx("Unknown: Section=%s, Line=%s", strlen(section) > 0 ? section : "<unknown>", line);

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

      if (strlen(config->servers[i].username) == 0)
      {
         pgexporter_log_fatal("pgexporter: No user defined for %s", config->servers[i].name);
         return 1;
      }
   }

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

   if (pgexporter_get_master_key(&master_key))
   {
      goto masterkey;
   }

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

            if (pgexporter_decrypt(decoded, decoded_length, master_key, &password, ENCRYPTION_AES_256_CBC))
            {
               goto error;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               memcpy(&config->users[index].username, username, strlen(username));
               memcpy(&config->users[index].password, password, strlen(password));
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

   free(master_key);

   fclose(file);

   return 0;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

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

   if (pgexporter_get_master_key(&master_key))
   {
      goto masterkey;
   }

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

            if (pgexporter_decrypt(decoded, decoded_length, master_key, &password, ENCRYPTION_AES_256_CBC))
            {
               goto error;
            }

            if (strlen(username) < MAX_USERNAME_LENGTH &&
                strlen(password) < MAX_PASSWORD_LENGTH)
            {
               memcpy(&config->admins[index].username, username, strlen(username));
               memcpy(&config->admins[index].password, password, strlen(password));
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
         }
      }
   }

   config->number_of_admins = index;

   if (config->number_of_admins > NUMBER_OF_ADMINS)
   {
      goto above;
   }

   free(master_key);

   fclose(file);

   return 0;

error:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 1;

masterkey:

   free(master_key);
   free(password);
   free(decoded);

   if (file)
   {
      fclose(file);
   }

   return 2;

above:

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

   *r = transfer_configuration(config, reload);

   /* Free Old Query Alts AVL Tree */
   for (int i = 0; reload != NULL && i < reload->number_of_metrics; i++)
   {
      pgexporter_free_pg_query_alts(reload);
   }
   pgexporter_free_extension_query_alts(reload);

   pgexporter_destroy_shared_memory((void*)reload, reload_size);

   pgexporter_log_debug("Reload: Success");

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

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();

   exit(0);
error:

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();

   exit(1);

}

void
pgexporter_conf_set(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
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
   struct configuration* config = NULL;
   struct json* server_j = NULL;
   size_t max;
   int server_index = -1;
   int begin = -1, end = -1;

   pgexporter_start_logging();

   start_time = time(NULL);

   config = (struct configuration*)shmem;
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

   // Modify
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

   if (strlen(key) && config_value)
   {
      bool unknown = false;
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
               unknown = true;
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
            pgexporter_json_put(server_j, key, (uintptr_t)config->servers[server_index].username, ValueString);
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
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->metrics, ValueInt64);
      }
      else if (!strcmp(key, "metrics_cache_max_size"))
      {
         long l = 0;

         if (as_bytes(config_value, &l, 0))
         {
            unknown = true;
         }

         config->metrics_cache_max_size = (size_t)l;

         pgexporter_json_put(response, key, (uintptr_t)config->metrics_cache_max_size, ValueInt64);
      }
      else if (!strcmp(key, "metrics_cache_max_age"))
      {
         if (as_seconds(config_value, &config->metrics_cache_max_age, 0))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_cache_max_age, ValueInt64);
      }
      else if (!strcmp(key, "metrics_path"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->metrics_path, config_value, max);
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_path, ValueString);
      }
      else if (!strcmp(key, "bridge"))
      {
         if (as_int(config_value, &config->bridge))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->bridge, ValueInt64);
      }
      else if (!strcmp(key, "bridge_endpoints"))
      {
         if (as_endpoints(config_value, config, true))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config_value, ValueString);
      }
      else if (!strcmp(key, "bridge_cache_max_size"))
      {
         long l = 0;

         if (as_bytes(config_value, &l, 0))
         {
            unknown = true;
         }

         config->bridge_cache_max_size = (size_t)l;

         pgexporter_json_put(response, key, (uintptr_t)config->bridge_cache_max_size, ValueInt64);
      }
      else if (!strcmp(key, "bridge_cache_max_age"))
      {
         if (as_seconds(config_value, &config->bridge_cache_max_age, 0))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->bridge_cache_max_age, ValueInt64);
      }
      else if (!strcmp(key, "bridge_json"))
      {
         if (as_int(config_value, &config->bridge_json))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->bridge_json,
                             ValueInt64);
      }
      else if (!strcmp(key, "bridge_json_cache_max_size"))
      {
         long l = 0;

         if (as_bytes(config_value, &l, 0))
         {
            unknown = true;
         }

         config->bridge_json_cache_max_size = (size_t)l;

         pgexporter_json_put(response, key,
                             (uintptr_t)config->bridge_json_cache_max_size,
                             ValueInt64);
      }
      else if (!strcmp(key, "management"))
      {
         if (as_int(config_value, &config->management))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->management, ValueInt64);
      }
      else if (!strcmp(key, "cache"))
      {
         if (as_bool(config_value, &config->cache))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->cache, ValueBool);
      }
      else if (!strcmp(key, "tls"))
      {
         if (as_bool(config_value, &config->tls))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->tls, ValueBool);
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
         pgexporter_json_put(response, key, (uintptr_t)config->metrics_key_file, ValueString);
      }
      else if (!strcmp(key, "blocking_timeout"))
      {
         if (as_int(config_value, &config->blocking_timeout))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->blocking_timeout, ValueInt64);
      }
      else if (!strcmp(key, "pidfile"))
      {
         max = strlen(config_value);
         if (max > MAX_PATH - 1)
         {
            max = MAX_PATH - 1;
         }
         memcpy(config->pidfile, config_value, max);
         pgexporter_json_put(response, key, (uintptr_t)config->pidfile, ValueString);
      }
      else if (!strcmp(key, "update_process_title"))
      {
         config->update_process_title = as_update_process_title(config_value, UPDATE_PROCESS_TITLE_VERBOSE);
         pgexporter_json_put(response, key, (uintptr_t)config->update_process_title, ValueUInt64);
      }
      else if (!strcmp(key, "log_type"))
      {
         config->log_type = as_logging_type(config_value);
         pgexporter_json_put(response, key, (uintptr_t)config->log_type, ValueInt32);
      }
      else if (!strcmp(key, "log_level"))
      {
         config->log_level = as_logging_level(config_value);
         pgexporter_json_put(response, key, (uintptr_t)config->log_level, ValueInt32);
      }
      else if (!strcmp(key, "log_path"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->log_path, config_value, max);
         pgexporter_json_put(response, key, (uintptr_t)config->log_path, ValueString);
      }
      else if (!strcmp(key, "log_rotation_size"))
      {
         if (as_logging_rotation_size(config_value, &config->log_rotation_size))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->log_rotation_size, ValueInt32);
      }
      else if (!strcmp(key, "log_rotation_age"))
      {
         if (as_logging_rotation_age(config_value, &config->log_rotation_age))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->log_rotation_age, ValueInt32);
      }
      else if (!strcmp(key, "log_line_prefix"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->log_line_prefix, config_value, max);
         pgexporter_json_put(response, key, (uintptr_t)config->log_line_prefix, ValueString);
      }
      else if (!strcmp(key, "log_mode"))
      {
         config->log_mode = as_logging_mode(config_value);
         pgexporter_json_put(response, key, (uintptr_t)config->log_mode, ValueInt32);
      }
      else if (!strcmp(key, "unix_socket_dir"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->unix_socket_dir, config_value, max);
         pgexporter_json_put(response, key, (uintptr_t)config->unix_socket_dir, ValueString);
      }
      else if (!strcmp(key, "libev"))
      {
         max = strlen(config_value);
         if (max > MISC_LENGTH - 1)
         {
            max = MISC_LENGTH - 1;
         }
         memcpy(config->libev, config_value, max);
         pgexporter_json_put(response, key, (uintptr_t)config->libev, ValueString);
      }
      else if (!strcmp(key, "keep_alive"))
      {
         if (as_bool(config_value, &config->keep_alive))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->keep_alive, ValueBool);
      }
      else if (!strcmp(key, "nodelay"))
      {
         if (as_bool(config_value, &config->nodelay))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->nodelay, ValueBool);
      }
      else if (!strcmp(key, "non_blocking"))
      {
         if (as_bool(config_value, &config->non_blocking))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->non_blocking, ValueBool);
      }
      else if (!strcmp(key, "backlog"))
      {
         if (as_int(config_value, &config->backlog))
         {
            unknown = true;
         }
         pgexporter_json_put(response, key, (uintptr_t)config->backlog, ValueInt32);
      }
      else if (!strcmp(key, "hugepage"))
      {
         config->hugepage = as_hugepage(config_value);
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

      if (unknown)
      {
         pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY, compression, encryption, payload);
         pgexporter_log_error("Conf Set: Unknown configuration key found (%d)", MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY);
         goto error;
      }
   }

   end_time = time(NULL);

   if (pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_NETWORK, compression, encryption, payload);
      pgexporter_log_error("Conf Set: Error sending response");
      goto error;
   }

   elapsed = pgexporter_get_timestamp_string(start_time, end_time, &total_seconds);

   pgexporter_log_info("Conf Set (Elapsed: %s)", elapsed);

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();

   exit(0);
error:

   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   pgexporter_stop_logging();

   exit(1);

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
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, (uintptr_t)config->metrics_cache_max_age, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, (uintptr_t)config->metrics_cache_max_size, ValueInt64);
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
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_CACHE_MAX_AGE, (uintptr_t)config->bridge_cache_max_age, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_CACHE_MAX_SIZE, (uintptr_t)config->bridge_cache_max_size, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_JSON, (uintptr_t)config->bridge_json, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BRIDGE_JSON_CACHE_MAX_SIZE, (uintptr_t)config->bridge_json_cache_max_size, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_MANAGEMENT, (uintptr_t)config->management, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_CACHE, (uintptr_t)config->cache, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_TYPE, (uintptr_t)config->log_type, ValueInt32);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_LEVEL, (uintptr_t)config->log_level, ValueInt32);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_PATH, (uintptr_t)config->log_path, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, (uintptr_t)config->log_rotation_age, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, (uintptr_t)config->log_rotation_size, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_LINE_PREFIX, (uintptr_t)config->log_line_prefix, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LOG_MODE, (uintptr_t)config->log_mode, ValueInt32);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, (uintptr_t)config->blocking_timeout, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS, (uintptr_t)config->tls, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS_CERT_FILE, (uintptr_t)config->tls_cert_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS_CA_FILE, (uintptr_t)config->tls_ca_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_TLS_KEY_FILE, (uintptr_t)config->tls_key_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_CERT_FILE, (uintptr_t)config->metrics_cert_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_CA_FILE, (uintptr_t)config->metrics_ca_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_METRICS_KEY_FILE, (uintptr_t)config->metrics_key_file, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_LIBEV, (uintptr_t)config->libev, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_KEEP_ALIVE, (uintptr_t)config->keep_alive, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_NODELAY, (uintptr_t)config->nodelay, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_NON_BLOCKING, (uintptr_t)config->non_blocking, ValueBool);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_BACKLOG, (uintptr_t)config->backlog, ValueInt64);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_HUGEPAGE, (uintptr_t)config->hugepage, ValueChar);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_PIDFILE, (uintptr_t)config->pidfile, ValueString);
   pgexporter_json_put(res, CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, (uintptr_t)config->update_process_title, ValueUInt64);
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

      memcpy(k, left, strlen(left));
      memcpy(v, right, strlen(right));

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

   return 0;
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

   return PGEXPORTER_LOGGING_LEVEL_INFO;
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

   return PGEXPORTER_LOGGING_MODE_APPEND;
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

/**
 * Utility function to understand the setting for updating
 * the process title.
 *
 * @param str the value obtained by the configuration parsing
 * @param default_policy a value to set when the configuration cannot be
 * understood
 *
 * @return The policy
 */
static unsigned int
as_update_process_title(char* str, unsigned int default_policy)
{
   if (is_empty_string(str))
   {
      return default_policy;
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

   // not a valid setting
   return default_policy;
}

/**
 * Parses a string to see if it contains
 * a valid value for log rotation size.
 * Returns 0 if parsing ok, 1 otherwise.
 *
 */
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
 * The string accepts
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in seconds.
 * The function sets the number of rotationg age as minutes.
 * Returns 1 for errors, 0 for correct parsing.
 *
 */
static int
as_logging_rotation_age(char* str, int* age)
{
   return as_seconds(str, age, PGEXPORTER_LOGGING_ROTATION_DISABLED);
}

/**
 * Parses an age string, providing the resulting value as seconds.
 * An age string is expressed by a number and a suffix that indicates
 * the multiplier. Accepted suffixes, case insensitive, are:
 * - s for seconds
 * - m for minutes
 * - h for hours
 * - d for days
 * - w for weeks
 *
 * The default is expressed in seconds.
 *
 * @param str the value to parse as retrieved from the configuration
 * @param age a pointer to the value that is going to store
 *        the resulting number of seconds
 * @param default_age a value to set when the parsing is unsuccesful

 */
static int
as_seconds(char* str, int* age, int default_age)
{
   int multiplier = 1;
   int index;
   char value[MISC_LENGTH];
   bool multiplier_set = false;
   int i_value = default_age;

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
         if (str[i] == 's' || str[i] == 'S')
         {
            multiplier = 1;
            multiplier_set = true;
         }
         else if (str[i] == 'm' || str[i] == 'M')
         {
            multiplier = 60;
            multiplier_set = true;
         }
         else if (str[i] == 'h' || str[i] == 'H')
         {
            multiplier = 3600;
            multiplier_set = true;
         }
         else if (str[i] == 'd' || str[i] == 'D')
         {
            multiplier = 24 * 3600;
            multiplier_set = true;
         }
         else if (str[i] == 'w' || str[i] == 'W')
         {
            multiplier = 24 * 3600 * 7;
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
   if (!as_int(value, &i_value))
   {
      // sanity check: the value
      // must be a positive number!
      if (i_value >= 0)
      {
         *age = i_value * multiplier;
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
         if (multiplier == 1
             || (str[i] != 'b' && str[i] != 'B'))
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

   token = strtok((char*) str, ",");

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
            strncpy(config->endpoints[idx].host, host, MISC_LENGTH);
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

static bool
transfer_configuration(struct configuration* config, struct configuration* reload)
{
   char* old_endpoints = NULL;
   char* new_endpoints = NULL;
   bool changed = false;

#ifdef HAVE_SYSTEMD
   sd_notify(0, "RELOADING=1");
#endif

   memcpy(config->host, reload->host, MISC_LENGTH);
   config->metrics = reload->metrics;
   config->metrics_cache_max_age = reload->metrics_cache_max_age;
   if (restart_int("metrics_cache_max_size", config->metrics_cache_max_size, reload->metrics_cache_max_size))
   {
      changed = true;
   }
   if (restart_int("bridge", config->bridge, reload->bridge))
   {
      changed = true;
   }

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

   if (restart_string("bridge_endpoints", old_endpoints, new_endpoints))
   {
      changed = true;
   }

   config->bridge_cache_max_age = reload->bridge_cache_max_age;
   if (restart_int("bridge_cache_max_size", config->bridge_cache_max_size, reload->bridge_cache_max_size))
   {
      changed = true;
   }
   if (restart_int("bridge_json", config->bridge_json, reload->bridge_json))
   {
      changed = true;
   }
   if (restart_int("bridge_json_cache_max_size", config->bridge_json_cache_max_size, reload->bridge_json_cache_max_size))
   {
      changed = true;
   }
   config->management = reload->management;
   config->cache = reload->cache;

   /* log_type */
   if (restart_int("log_type", config->log_type, reload->log_type))
   {
      changed = true;
   }
   config->log_level = reload->log_level;
   // if the log main parameters have changed, we need
   // to restart the logging system
   if (strncmp(config->log_path, reload->log_path, MISC_LENGTH)
       || config->log_rotation_size != reload->log_rotation_size
       || config->log_rotation_age != reload->log_rotation_age
       || config->log_mode != reload->log_mode)
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
   /* log_lock */

   config->tls = reload->tls;
   memcpy(config->tls_cert_file, reload->tls_cert_file, MAX_PATH);
   memcpy(config->tls_key_file, reload->tls_key_file, MAX_PATH);
   memcpy(config->tls_ca_file, reload->tls_ca_file, MAX_PATH);
   memcpy(config->metrics_cert_file, reload->metrics_cert_file, MAX_PATH);
   memcpy(config->metrics_key_file, reload->metrics_key_file, MAX_PATH);
   memcpy(config->metrics_ca_file, reload->metrics_ca_file, MAX_PATH);

   config->blocking_timeout = reload->blocking_timeout;
   config->authentication_timeout = reload->authentication_timeout;
   /* pidfile */
   if (restart_string("pidfile", config->pidfile, reload->pidfile))
   {
      changed = true;
   }

   /* libev */
   restart_string("libev", config->libev, reload->libev);
   config->keep_alive = reload->keep_alive;
   config->nodelay = reload->nodelay;
   config->non_blocking = reload->non_blocking;
   config->backlog = reload->backlog;
   /* hugepage */
   if (restart_int("hugepage", config->hugepage, reload->hugepage))
   {
      changed = true;
   }

   /* update_process_title */
   if (restart_int("update_process_title", config->update_process_title, reload->update_process_title))
   {
      changed = true;
   }

   /* unix_socket_dir */
   if (restart_string("unix_socket_dir", config->unix_socket_dir, reload->unix_socket_dir))
   {
      changed = true;
   }

   memset(&config->servers[0], 0, sizeof(struct server) * NUMBER_OF_SERVERS);
   for (int i = 0; i < reload->number_of_servers; i++)
   {
      copy_server(&config->servers[i], &reload->servers[i]);
   }
   config->number_of_servers = reload->number_of_servers;

   memset(&config->users[0], 0, sizeof(struct user) * NUMBER_OF_USERS);
   for (int i = 0; i < reload->number_of_users; i++)
   {
      copy_user(&config->users[i], &reload->users[i]);
   }
   config->number_of_users = reload->number_of_users;

   memset(&config->admins[0], 0, sizeof(struct user) * NUMBER_OF_ADMINS);
   for (int i = 0; i < reload->number_of_admins; i++)
   {
      copy_user(&config->admins[i], &reload->admins[i]);
   }
   config->number_of_admins = reload->number_of_admins;

   /* prometheus */
   memcpy(config->metrics_path, reload->metrics_path, MAX_PATH);
   for (int i = 0; i < reload->number_of_metrics; i++)
   {
      copy_promethus(&config->prometheus[i], &reload->prometheus[i]);
   }
   config->number_of_metrics = reload->number_of_metrics;

   /* endpoint */
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

   return changed;
}

static void
copy_server(struct server* dst, struct server* src)
{
   memcpy(&dst->name[0], &src->name[0], MISC_LENGTH);
   memcpy(&dst->host[0], &src->host[0], MISC_LENGTH);
   dst->port = src->port;
   memcpy(&dst->username[0], &src->username[0], MAX_USERNAME_LENGTH);
   memcpy(&dst->data[0], &src->data[0], MISC_LENGTH);
   memcpy(&dst->wal[0], &src->wal[0], MISC_LENGTH);
   dst->fd = src->fd;
   dst->extension = true;
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

   pgexporter_copy_pg_query_alts(&dst->pg_root, src->pg_root);
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
      pgexporter_log_info("Restart required for %s - Existing %d New %d", name, e, n);
      return 1;
   }

   return 0;
}

static int
restart_string(char* name, char* e, char* n)
{
   if (strcmp(e, n))
   {
      pgexporter_log_info("Restart required for %s - Existing %s New %s", name, e, n);
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
