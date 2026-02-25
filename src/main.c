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
#include <art.h>
#include <bridge.h>
#include <cmd.h>
#include <console.h>
#include <configuration.h>
#include <connection.h>
#include <extension.h>
#include <ext_query_alts.h>
#include <internal.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <prometheus.h>
#include <pg_query_alts.h>
#include <queries.h>
#include <remote.h>
#include <security.h>
#include <server.h>
#include <shmem.h>
#include <status.h>
#include <utils.h>
#include <yaml_configuration.h>
#include <json_configuration.h>

/* system */
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <openssl/crypto.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define MAX_FDS 64

static void accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_transfer_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_console_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_bridge_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_bridge_json_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void reload_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void coredump_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void sigchld_cb(struct ev_loop* loop, ev_signal* w, int revents);
static bool accept_fatal(int error);
static bool reload_configuration(void);
static int create_pidfile(void);
static void remove_pidfile(void);
static int create_lockfile(int port);
static void remove_lockfile(int port);
static void shutdown_ports(void);

struct accept_io
{
   struct ev_io io;
   int socket;
   char** argv;
};

static volatile int keep_running = 1;
static volatile int stop = 0;
static char** argv_ptr;
static struct ev_loop* main_loop = NULL;
static struct accept_io io_mgt;
static int unix_management_socket = -1;
static int unix_transfer_socket = -1;
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static struct accept_io io_console[MAX_FDS];
static int* console_fds = NULL;
static int console_fds_length = -1;
static struct accept_io io_bridge[MAX_FDS];
static int* bridge_fds = NULL;
static int bridge_fds_length = -1;
static struct accept_io io_bridge_json[MAX_FDS];
static int* bridge_json_fds = NULL;
static int bridge_json_fds_length = -1;
static struct accept_io io_management[MAX_FDS];
static int* management_fds = NULL;
static int management_fds_length = -1;
static struct accept_io io_transfer;

static void
start_mgt(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics != -1)
   {
      memset(&io_mgt, 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_management_socket, EV_READ);
      io_mgt.socket = unix_management_socket;
      io_mgt.argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_mgt);
   }
}

static void
shutdown_mgt(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics != -1)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_mgt);
      pgexporter_disconnect(unix_management_socket);
      errno = 0;
      pgexporter_remove_unix_socket(config->unix_socket_dir, MAIN_UDS);
      errno = 0;
   }
}

static void
start_transfer(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics != -1)
   {
      memset(&io_transfer, 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_transfer, accept_transfer_cb, unix_transfer_socket, EV_READ);
      io_transfer.socket = unix_transfer_socket;
      io_transfer.argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_transfer);
   }
}

static void
shutdown_transfer(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics != -1)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_transfer);
      pgexporter_disconnect(unix_transfer_socket);
      errno = 0;
      pgexporter_remove_unix_socket(config->unix_socket_dir, TRANSFER_UDS);
      errno = 0;
   }
}

static void
start_metrics(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics != -1)
   {
      for (int i = 0; i < metrics_fds_length; i++)
      {
         int sockfd = *(metrics_fds + i);

         memset(&io_metrics[i], 0, sizeof(struct accept_io));
         ev_io_init((struct ev_io*)&io_metrics[i], accept_metrics_cb, sockfd, EV_READ);
         io_metrics[i].socket = sockfd;
         io_metrics[i].argv = argv_ptr;
         ev_io_start(main_loop, (struct ev_io*)&io_metrics[i]);
      }
   }
}

static void
shutdown_metrics(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics != -1)
   {
      for (int i = 0; i < metrics_fds_length; i++)
      {
         ev_io_stop(main_loop, (struct ev_io*)&io_metrics[i]);
         pgexporter_disconnect(io_metrics[i].socket);
         errno = 0;
      }
   }
}

static void
start_console(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->console != -1)
   {
      for (int i = 0; i < console_fds_length; i++)
      {
         int sockfd = *(console_fds + i);

         memset(&io_console[i], 0, sizeof(struct accept_io));
         ev_io_init((struct ev_io*)&io_console[i], accept_console_cb, sockfd, EV_READ);
         io_console[i].socket = sockfd;
         io_console[i].argv = argv_ptr;
         ev_io_start(main_loop, (struct ev_io*)&io_console[i]);
      }
   }
}

static void
shutdown_console(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->console != -1)
   {
      for (int i = 0; i < console_fds_length; i++)
      {
         ev_io_stop(main_loop, (struct ev_io*)&io_console[i]);
         pgexporter_disconnect(io_console[i].socket);
         errno = 0;
      }
   }
}

static void
start_bridge(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->bridge != -1)
   {
      for (int i = 0; i < bridge_fds_length; i++)
      {
         int sockfd = *(bridge_fds + i);

         memset(&io_bridge[i], 0, sizeof(struct accept_io));
         ev_io_init((struct ev_io*)&io_bridge[i], accept_bridge_cb, sockfd, EV_READ);
         io_bridge[i].socket = sockfd;
         io_bridge[i].argv = argv_ptr;
         ev_io_start(main_loop, (struct ev_io*)&io_bridge[i]);
      }
   }
}

static void
shutdown_bridge(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->bridge != -1)
   {
      for (int i = 0; i < bridge_fds_length; i++)
      {
         ev_io_stop(main_loop, (struct ev_io*)&io_bridge[i]);
         pgexporter_disconnect(io_bridge[i].socket);
         errno = 0;
      }
   }
}

static void
start_bridge_json(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->bridge != -1 && config->bridge_json != -1)
   {
      for (int i = 0; i < bridge_json_fds_length; i++)
      {
         int sockfd = *(bridge_json_fds + i);

         memset(&io_bridge_json[i], 0, sizeof(struct accept_io));
         ev_io_init((struct ev_io*)&io_bridge_json[i], accept_bridge_json_cb, sockfd, EV_READ);
         io_bridge_json[i].socket = sockfd;
         io_bridge_json[i].argv = argv_ptr;
         ev_io_start(main_loop, (struct ev_io*)&io_bridge_json[i]);
      }
   }
}

static void
shutdown_bridge_json(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->bridge != -1 && config->bridge_json != -1)
   {
      for (int i = 0; i < bridge_json_fds_length; i++)
      {
         ev_io_stop(main_loop, (struct ev_io*)&io_bridge_json[i]);
         pgexporter_disconnect(io_bridge_json[i].socket);
         errno = 0;
      }
   }
}

static void
start_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      int sockfd = *(management_fds + i);

      memset(&io_management[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_management[i], accept_management_cb, sockfd, EV_READ);
      io_management[i].socket = sockfd;
      io_management[i].argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_management[i]);
   }
}

static void
shutdown_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_management[i]);
      pgexporter_disconnect(io_management[i].socket);
      errno = 0;
   }
}

static void
version(void)
{
   printf("pgexporter %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgexporter %s\n", VERSION);
   printf("  Prometheus exporter for PostgreSQL\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgexporter [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE                    Set the path to the pgexporter.conf file\n");
   printf("  -u, --users USERS_FILE                      Set the path to the pgexporter_users.conf file\n");
   printf("  -A, --admins ADMINS_FILE                    Set the path to the pgexporter_admins.conf file\n");
   printf("  -Y, --yaml METRICS_FILE_DIR                 Set the path to YAML file/directory\n");
   printf("  -J, --json METRICS_FILE_DIR                 Set the path to JSON file/directory\n");
   printf("  -D, --directory DIRECTORY                   Set the configuration directory path\n");
   printf("                                              Can also be set via PGEXPORTER_CONFIG_DIR environment variable\n");
   printf("  -d, --daemon                                Run as a daemon\n");
   printf("  -C, --collectors NAME_1,NAME_2,...,NAME_N   Enable only specific collectors\n");
   printf("  -V, --version                               Display version information\n");
   printf("  -?, --help                                  Display help\n");
   printf("\n");
   printf("pgexporter: %s\n", PGEXPORTER_HOMEPAGE);
   printf("Report bugs: %s\n", PGEXPORTER_ISSUES);
}

int
main(int argc, char** argv)
{
   char* configuration_path = NULL;
   char* users_path = NULL;
   char* admins_path = NULL;
   char* yaml_path = NULL;
   char* json_path = NULL;
   char* bin_path = NULL;
   char* collector = NULL;
   char* directory_path = NULL;
   char collectors[NUMBER_OF_COLLECTORS][MAX_COLLECTOR_LENGTH];
   bool daemon = false;
   pid_t pid, sid;
   struct signal_info signal_watcher[6];
   size_t shmem_size;
   size_t prometheus_cache_shmem_size = 0;
   size_t bridge_cache_shmem_size = 0;
   size_t bridge_json_cache_shmem_size = 0;
   struct configuration* config = NULL;
   int ret;
   int collector_idx = 0;
   char* os = NULL;

   int kernel_major, kernel_minor, kernel_patch;

   argv_ptr = argv;
   char config_path_buffer[MAX_PATH];
   char users_path_buffer[MAX_PATH];
   char admins_path_buffer[MAX_PATH];
   struct stat path_stat = {0};
   char* adjusted_dir_path = NULL;
   char* filepath = NULL;
   int optind = 0;
   int num_options = 0;
   int num_results = 0;
   cli_option options[] = {
      {"c", "config", true},
      {"u", "users", true},
      {"A", "admins", true},
      {"Y", "yaml", true},
      {"J", "json", true},
      {"d", "daemon", false},
      {"V", "version", false},
      {"?", "help", false},
      {"C", "collectors", true},
      {"D", "directory", true},
   };

   num_options = sizeof(options) / sizeof(options[0]);
   cli_result results[num_options];

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, false, &filepath, &optind);

   if (num_results < 0)
   {
      errx(1, "Error parsing command line\n");
      return 1;
   }

   for (int i = 0; i < num_results; i++)
   {
      char* optname = results[i].option_name;
      char* optarg = results[i].argument;

      if (optname == NULL)
      {
         break;
      }
      else if (!strcmp(optname, "config") || !strcmp(optname, "c"))
      {
         configuration_path = optarg;
      }
      else if (!strcmp(optname, "users") || !strcmp(optname, "u"))
      {
         users_path = optarg;
      }
      else if (!strcmp(optname, "admins") || !strcmp(optname, "A"))
      {
         admins_path = optarg;
      }
      else if (!strcmp(optname, "yaml") || !strcmp(optname, "Y"))
      {
         yaml_path = optarg;
      }
      else if (!strcmp(optname, "json") || !strcmp(optname, "J"))
      {
         json_path = optarg;
      }
      else if (!strcmp(optname, "daemon") || !strcmp(optname, "d"))
      {
         daemon = true;
      }
      else if (!strcmp(optname, "version") || !strcmp(optname, "V"))
      {
         version();
      }
      else if (!strcmp(optname, "collectors") || !strcmp(optname, "C"))
      {
         memset(collectors, 0, (NUMBER_OF_COLLECTORS * MAX_COLLECTOR_LENGTH) * sizeof(char));

         collector_idx = 0;
         collector = optarg;
         while (*collector)
         {
            if (*collector == ',')
            {
               collector_idx++;
            }
            collector++;
         }
         collector_idx++;

         if (collector_idx > NUMBER_OF_COLLECTORS)
         {
            warnx("pgexporter: Too many collectors specified.");
#ifdef HAVE_SYSTEMD
            sd_notify(0, "STATUS=pgexporter: Too many collectors specified.");
#endif
            exit(1);
         }

         collector_idx = 0;
         while ((collector = strtok_r(optarg, ",", &optarg)))
         {
            bool found = false;

            for (int i = 0; i < collector_idx; i++)
            {
               if (!strncmp(collector, collectors[i], MAX_COLLECTOR_LENGTH - 1))
               {
                  found = true;
                  break;
               }
            }

            if (!found)
            {
               pgexporter_snprintf(collectors[collector_idx++], MAX_COLLECTOR_LENGTH, "%s", collector);
            }
         }
      }
      else if (!strcmp(optname, "directory") || !strcmp(optname, "D"))
      {
         directory_path = optarg;
      }
      else if (!strcmp(optname, "help") || !strcmp(optname, "?"))
      {
         usage();
         exit(1);
      }
   }

   if (getuid() == 0)
   {
      warnx("pgexporter: Using the root account is not allowed");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      exit(1);
   }

   pgexporter_memory_init();

   shmem_size = sizeof(struct configuration);
   if (pgexporter_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgexporter: Error in creating shared memory");
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      exit(1);
   }

   pgexporter_init_configuration(shmem);
   config = (struct configuration*)shmem;
   memcpy(config->collectors, collectors, (NUMBER_OF_COLLECTORS * MAX_COLLECTOR_LENGTH) * sizeof(char));
   config->number_of_collectors = collector_idx;

   /* Directory processing */
   if (directory_path == NULL)
   {
      // Check for environment variable if no -D flag provided
      directory_path = getenv("PGEXPORTER_CONFIG_DIR");
      if (directory_path != NULL)
      {
         pgexporter_log_info("Configuration directory set via PGEXPORTER_CONFIG_DIR environment variable: %s", directory_path);
      }
   }

   if (directory_path != NULL)
   {
      if (!strcmp(directory_path, PGEXPORTER_DEFAULT_CONFIGURATION_PATH))
      {
         pgexporter_log_warn("Using the default configuration directory %s, -D can be omitted.", directory_path);
      }

      if (access(directory_path, F_OK) != 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Configuration directory not found: %s", directory_path);
#endif
         pgexporter_log_error("Configuration directory not found: %s", directory_path);
         exit(1);
      }

      if (stat(directory_path, &path_stat) == 0)
      {
         if (!S_ISDIR(path_stat.st_mode))
         {
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Path is not a directory: %s", directory_path);
#endif
            pgexporter_log_error("Path is not a directory: %s", directory_path);
            exit(1);
         }
      }

      if (access(directory_path, R_OK | X_OK) != 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Insufficient permissions for directory: %s", directory_path);
#endif
         pgexporter_log_error("Insufficient permissions for directory: %s", directory_path);
         exit(1);
      }

      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         adjusted_dir_path = pgexporter_append(strdup(directory_path), "/");
      }
      else
      {
         adjusted_dir_path = strdup(directory_path);
      }

      if (adjusted_dir_path == NULL)
      {
         pgexporter_log_error("Memory allocation failed while copying directory path.");
         exit(1);
      }

      if (!configuration_path && pgexporter_normalize_path(adjusted_dir_path, PGEXPORTER_CONF_FILENAME, PGEXPORTER_DEFAULT_CONF_FILE, config_path_buffer, sizeof(config_path_buffer)) == 0 && strlen(config_path_buffer) > 0)
      {
         configuration_path = config_path_buffer;
      }

      if (!users_path && pgexporter_normalize_path(adjusted_dir_path, PGEXPORTER_USERS_FILENAME, PGEXPORTER_DEFAULT_USERS_FILE, users_path_buffer, sizeof(users_path_buffer)) == 0 && strlen(users_path_buffer) > 0)
      {
         users_path = users_path_buffer;
      }

      if (!admins_path && pgexporter_normalize_path(adjusted_dir_path, PGEXPORTER_ADMINS_FILENAME, PGEXPORTER_DEFAULT_ADMINS_FILE, admins_path_buffer, sizeof(admins_path_buffer)) == 0 && strlen(admins_path_buffer) > 0)
      {
         admins_path = admins_path_buffer;
      }

      free(adjusted_dir_path);
   }

   /* Configuration File */
   if (configuration_path != NULL)
   {
      int cfg_ret = pgexporter_validate_config_file(configuration_path);

      if (cfg_ret)
      {
         switch (cfg_ret)
         {
            case ENOENT:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file not found or not a regular file: %s", configuration_path);
#endif
               errx(1, "Configuration file not found or not a regular file: %s", configuration_path);
               break;

            case EACCES:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Can't read configuration file: %s", configuration_path);
#endif
               errx(1, "Can't read configuration file: %s", configuration_path);
               break;

            case EINVAL:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file contains binary data or invalid path: %s", configuration_path);
#endif
               errx(1, "Configuration file contains binary data or invalid path: %s", configuration_path);
               break;

            default:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file validation failed: %s", configuration_path);
#endif
               errx(1, "Configuration file validation failed: %s", configuration_path);
         }
      }

      if (pgexporter_read_configuration(shmem, configuration_path))
      {
         warnx("pgexporter: Configuration not found: %s", configuration_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Configuration not found: %s", configuration_path);
#endif
         exit(1);
      }
   }
   else
   {
      if (pgexporter_read_configuration(shmem, "/etc/pgexporter/pgexporter.conf"))
      {
         warnx("pgexporter: Configuration not found: /etc/pgexporter/pgexporter.conf");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Configuration not found: /etc/pgexporter/pgexporter.conf");
#endif
         exit(1);
      }
      configuration_path = "/etc/pgexporter/pgexporter.conf";
   }
   pgexporter_snprintf(&config->configuration_path[0], MAX_PATH, "%s", configuration_path);

   /* Users Configuration File */
   if (users_path != NULL)
   {
      ret = pgexporter_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         warnx("pgexporter: USERS configuration not found: %s", users_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=USERS configuration not found: %s", users_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         warnx("pgexporter: Invalid master key file");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         warnx("pgexporter: USERS: Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_USERS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=USERS: Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_USERS);
#endif
         exit(1);
      }
      pgexporter_snprintf(&config->users_path[0], MAX_PATH, "%s", users_path);
   }
   else
   {
      users_path = "/etc/pgexporter/pgexporter_users.conf";
      ret = pgexporter_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         pgexporter_snprintf(&config->users_path[0], MAX_PATH, "%s", users_path);
      }
   }

   /* Admins Configuration File */
   if (admins_path != NULL)
   {
      ret = pgexporter_read_admins_configuration(shmem, admins_path);
      if (ret == 1)
      {
         warnx("pgexporter: ADMINS configuration not found: %s", admins_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=ADMINS configuration not found: %s", admins_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         warnx("pgexporter: Invalid master key file");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         warnx("pgexporter: ADMINS: Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=ADMINS: Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#endif
         exit(1);
      }
      pgexporter_snprintf(&config->admins_path[0], MAX_PATH, "%s", admins_path);
   }
   else
   {
      admins_path = "/etc/pgexporter/pgexporter_admins.conf";
      ret = pgexporter_read_admins_configuration(shmem, admins_path);
      if (ret == 0)
      {
         pgexporter_snprintf(&config->admins_path[0], MAX_PATH, "%s", admins_path);
      }
   }

   if (pgexporter_start_logging())
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      exit(1);
   }

   /* Internal Metrics Collectors YAML File, not to be used with YAML/JSON  */
   if (json_path == NULL && yaml_path == NULL)
   {
      if (pgexporter_read_internal_yaml_metrics(config, true))
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid core metrics");
#endif
         exit(1);
      }
   }

   if (pgexporter_validate_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      exit(1);
   }
   if (pgexporter_validate_users_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      exit(1);
   }
   if (pgexporter_validate_admins_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      exit(1);
   }

   config = (struct configuration*)shmem;

   if (create_pidfile())
   {
      exit(1);
   }

   if (config->metrics != -1)
   {
      if (create_lockfile(config->metrics))
      {
         exit(1);
      }
   }

   if (config->bridge != -1)
   {
      if (create_lockfile(config->bridge))
      {
         exit(1);
      }

      if (config->bridge_json != -1)
      {
         if (create_lockfile(config->bridge_json))
         {
            exit(1);
         }
      }
   }

   if (yaml_path != NULL && json_path != NULL)
   {
      warnx("Both YAML and JSON paths cannot be specified at the same time");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Both YAML and JSON paths cannot be specified at the same time");
#endif
      exit(1);
   }

   if (yaml_path != NULL)
   {
      pgexporter_snprintf(config->metrics_path, MAX_PATH, "%s", yaml_path);

      if (pgexporter_read_metrics_configuration(shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid metrics YAML");
#endif
         exit(1);
      }
   }
   else if (json_path != NULL)
   {
      pgexporter_snprintf(config->metrics_path, MAX_PATH, "%s", json_path);

      if (pgexporter_read_json_metrics_configuration(shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid metrics JSON");
#endif
         exit(1);
      }
   }
   if (yaml_path != NULL || json_path != NULL)
   {
      pgexporter_log_debug("Reading : %d metrics from path", config->number_of_metrics);
   }

   /* Extension path */
   if (pgexporter_setup_extensions_path(config, argv[0], &bin_path))
   {
      warnx("pgexporter: Failed to setup extensions path");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to setup extensions path");
#endif
      exit(1);
   }

   if (daemon)
   {
      if (config->log_type == PGEXPORTER_LOGGING_TYPE_CONSOLE)
      {
         warnx("pgexporter: Daemon mode can't be used with console logging");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         exit(1);
      }

      pid = fork();

      if (pid < 0)
      {
         warnx("pgexporter: Daemon mode failed");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Daemon mode failed");
#endif
         exit(1);
      }

      if (pid > 0)
      {
         exit(0);
      }

      /* We are a daemon now */
      umask(0);
      sid = setsid();

      if (sid < 0)
      {
         exit(1);
      }
   }

   pgexporter_set_proc_title(argc, argv, "main", NULL);

   if (pgexporter_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
      errx(1, "Error in creating and initializing prometheus cache shared memory");
   }

   if (config->bridge > 0 && pgexporter_time_is_valid(config->bridge_cache_max_age) && config->bridge_cache_max_size > 0)
   {
      if (pgexporter_bridge_init_cache(&bridge_cache_shmem_size, &bridge_cache_shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
         errx(1, "Error in creating and initializing prometheus cache shared memory");
      }
   }

   if (config->bridge_json > 0)
   {
      if (pgexporter_bridge_json_init_cache(&bridge_json_cache_shmem_size, &bridge_json_cache_shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Error in creating and initializing prometheus JSON cache shared memory");
#endif
         errx(1, "Error in creating and initializing prometheus JSON cache shared memory");
      }
   }

   /* Bind Unix Domain Socket: Main */
   if (pgexporter_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgexporter_log_fatal("pgexporter: Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#endif
      exit(1);
   }

   /* Bind Unix Domain Socket: Transfer */
   if (pgexporter_bind_unix_socket(config->unix_socket_dir, TRANSFER_UDS, &unix_transfer_socket))
   {
      pgexporter_log_fatal("pgexporter: Could not bind to %s/%s", config->unix_socket_dir, TRANSFER_UDS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, TRANSFER_UDS);
#endif
      exit(1);
   }

   /* libev */
   main_loop = ev_default_loop(pgexporter_libev(config->libev));
   if (!main_loop)
   {
      pgexporter_log_fatal("pgexporter: No loop implementation (%x) (%x)",
                           pgexporter_libev(config->libev), ev_supported_backends());
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=No loop implementation (%x) (%x)", pgexporter_libev(config->libev), ev_supported_backends());
#endif
      exit(1);
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);
   ev_signal_init((struct ev_signal*)&signal_watcher[1], reload_cb, SIGHUP);
   ev_signal_init((struct ev_signal*)&signal_watcher[2], shutdown_cb, SIGINT);
   ev_signal_init((struct ev_signal*)&signal_watcher[3], coredump_cb, SIGABRT);
   ev_signal_init((struct ev_signal*)&signal_watcher[4], shutdown_cb, SIGALRM);
   ev_signal_init((struct ev_signal*)&signal_watcher[5], sigchld_cb, SIGCHLD);

   for (int i = 0; i < 6; i++)
   {
      signal_watcher[i].slot = -1;
      ev_signal_start(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   if (pgexporter_tls_valid())
   {
      pgexporter_log_fatal("pgexporter: Invalid TLS configuration");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
      exit(1);
   }

   if (config->metrics > 0)
   {
      start_transfer();
      start_mgt();

      /* Bind metrics socket */
      if (pgexporter_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
      {
         pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->metrics);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->metrics);
#endif
         exit(1);
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgexporter_log_fatal("pgexporter: Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         exit(1);
      }

      start_metrics();
   }

   if (config->console > 0)
   {
      /* Bind console socket */
      if (pgexporter_bind(config->host, config->console, &console_fds, &console_fds_length))
      {
         pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->console);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->console);
#endif
         exit(1);
      }

      if (console_fds_length > MAX_FDS)
      {
         pgexporter_log_fatal("pgexporter: Too many descriptors %d", console_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", console_fds_length);
#endif
         exit(1);
      }

      start_console();
   }

   if (config->bridge > 0)
   {
      /* Bind bridge socket */
      if (pgexporter_bind(config->host, config->bridge, &bridge_fds, &bridge_fds_length))
      {
         pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->bridge);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->bridge);
#endif
         exit(1);
      }

      if (bridge_fds_length > MAX_FDS)
      {
         pgexporter_log_fatal("pgexporter: Too many descriptors %d", bridge_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", bridge_fds_length);
#endif
         exit(1);
      }

      start_bridge();

      if (config->bridge_json > 0)
      {
         /* Bind bridge socket */
         if (pgexporter_bind(config->host, config->bridge_json, &bridge_json_fds, &bridge_json_fds_length))
         {
            pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->bridge_json);
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->bridge_json);
#endif
            exit(1);
         }

         if (bridge_json_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("pgexporter: Too many descriptors %d", bridge_json_fds_length);
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Too many descriptors %d", bridge_json_fds_length);
#endif
            exit(1);
         }

         start_bridge_json();
      }
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgexporter_bind(config->host, config->management, &management_fds, &management_fds_length))
      {
         pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->management);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->management);
#endif
         exit(1);
      }

      if (management_fds_length > MAX_FDS)
      {
         pgexporter_log_fatal("pgexporter: Too many descriptors %d", management_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         exit(1);
      }

      start_management();
   }

   pgexporter_log_info("pgexporter: started on %s", config->host);
   pgexporter_log_debug("Management: %d", unix_management_socket);
   pgexporter_log_debug("Transfer: %d", unix_transfer_socket);
   pgexporter_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch);

   free(os);

   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgexporter_log_debug("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < console_fds_length; i++)
   {
      pgexporter_log_debug("Console: %d", *(console_fds + i));
   }
   for (int i = 0; i < bridge_fds_length; i++)
   {
      pgexporter_log_debug("Bridge: %d", *(bridge_fds + i));
   }
   for (int i = 0; i < bridge_json_fds_length; i++)
   {
      pgexporter_log_debug("Bridge JSON: %d", *(bridge_json_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgexporter_log_debug("Remote management: %d", *(management_fds + i));
   }
   pgexporter_libev_engines();
   pgexporter_log_debug("libev engine: %s", pgexporter_libev_engine(ev_backend(main_loop)));
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   pgexporter_log_debug("%s", SSLeay_version(SSLEAY_VERSION));
#else
   pgexporter_log_debug("%s", OpenSSL_version(OPENSSL_VERSION));
#endif
   pgexporter_log_debug("Configuration size: %lu", shmem_size);
   pgexporter_log_debug("Known users: %d", config->number_of_users);
   pgexporter_log_debug("Known admins: %d", config->number_of_admins);

#ifdef HAVE_SYSTEMD
   sd_notifyf(0,
              "READY=1\n"
              "STATUS=Running\n"
              "MAINPID=%lu",
              (unsigned long)getpid());
#endif

   pgexporter_open_connections();
   for (int i = 0; i < config->number_of_servers; i++)
   {
      pgexporter_log_trace("Server: %s/%d.%d -> %s", config->servers[i].name,
                           config->servers[i].version, config->servers[i].minor_version,
                           config->servers[i].fd != -1 ? "true" : "false");
   }

   if (pgexporter_load_extension_yamls(config))
   {
      warnx("pgexporter: Failed to load extension YAMLs");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to load extension YAMLs");
#endif
      exit(1);
   }

   /* Close connections after validation  and loading extensions - child processes will create their own.
    * SSL objects cannot be shared across fork(), so keeping them open here
    * would just cause memory leaks when children reset the shared memory pointers. */

   pgexporter_close_connections();

   while (keep_running)
   {
      ev_loop(main_loop, 0);
   }

   pgexporter_log_info("pgexporter: shutdown");
#ifdef HAVE_SYSTEMD
   sd_notify(0, "STOPPING=1");
#endif

   pgexporter_close_connections();

   shutdown_management();
   if (config->metrics != -1)
   {
      shutdown_metrics();
      shutdown_mgt();
      shutdown_transfer();
   }

   if (config->bridge != -1)
   {
      shutdown_bridge();

      if (config->bridge_json != -1)
      {
         shutdown_bridge_json();
      }
   }

   for (int i = 0; i < 6; i++)
   {
      ev_signal_stop(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(main_loop);

   free(metrics_fds);
   free(console_fds);
   free(bridge_fds);
   free(bridge_json_fds);
   free(management_fds);

   free(bin_path);

   remove_pidfile();
   remove_lockfile(config->metrics);
   remove_lockfile(config->console);
   remove_lockfile(config->bridge);
   remove_lockfile(config->bridge_json);

   pgexporter_stop_logging();

   pgexporter_free_pg_query_alts(config);
   pgexporter_free_extension_query_alts(config);

   pgexporter_destroy_shared_memory(shmem, shmem_size);
   pgexporter_destroy_shared_memory(prometheus_cache_shmem,
                                    prometheus_cache_shmem_size);

   pgexporter_memory_destroy();

   OPENSSL_cleanup();

   if (daemon || stop)
   {
      kill(0, SIGTERM);
   }

   return 0;
}

static void
accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   int32_t id;
   char* server = NULL;
   pid_t pid;
   char* str = NULL;
   time_t start_time;
   time_t end_time;
   struct accept_io* ai;
   struct json* payload = NULL;
   struct json* header = NULL;
   struct configuration* config;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;

   if (EV_ERROR & revents)
   {
      pgexporter_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting management due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgexporter_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
         {
            pgexporter_log_fatal("Could not bind to %s", config->unix_socket_dir);
            exit(1);
         }

         start_mgt();

         pgexporter_log_debug("Management: %d", unix_management_socket);
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   /* Process internal management request */
   if (pgexporter_management_read_json(NULL, client_fd, &compression, &encryption, &payload))
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, compression, encryption, NULL);
      pgexporter_log_error("Management: Bad payload (%d)", MANAGEMENT_ERROR_BAD_PAYLOAD);
      goto error;
   }

   header = (struct json*)pgexporter_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   id = (int32_t)pgexporter_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);

   str = pgexporter_json_to_string(payload, FORMAT_JSON, NULL, 0);
   pgexporter_log_debug("Management %d: %s", id, str);

   if (id == MANAGEMENT_SHUTDOWN)
   {
      start_time = time(NULL);

      end_time = time(NULL);

      pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);

      ev_break(loop, EVBREAK_ALL);
      keep_running = 0;
      stop = 1;
   }
   else if (id == MANAGEMENT_PING)
   {
      struct json* response = NULL;

      start_time = time(NULL);

      pgexporter_management_create_response(payload, -1, &response);

      end_time = time(NULL);

      pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_RESET)
   {
      start_time = time(NULL);

      pgexporter_prometheus_reset();

      end_time = time(NULL);

      pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_RELOAD)
   {
      bool restart = false;
      struct json* response = NULL;

      start_time = time(NULL);

      restart = reload_configuration();

      pgexporter_management_create_response(payload, -1, &response);

      pgexporter_json_put(response, MANAGEMENT_ARGUMENT_RESTART, (uintptr_t)restart, ValueBool);

      end_time = time(NULL);

      pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_STATUS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgexporter_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_STATUS_NOFORK, compression, encryption, payload);
         pgexporter_log_error("Status: No fork %s (%d)", server, MANAGEMENT_ERROR_STATUS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgexporter_json_clone(payload, &pyl);

         pgexporter_set_proc_title(1, ai->argv, "status", NULL);
         pgexporter_status(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_STATUS_DETAILS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgexporter_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK, compression, encryption, payload);
         pgexporter_log_error("Details: No fork %s (%d)", server, MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgexporter_json_clone(payload, &pyl);

         pgexporter_set_proc_title(1, ai->argv, "details", NULL);
         pgexporter_status_details(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_CONF_LS)
   {
      struct json* response = NULL;

      start_time = time(NULL);

      pgexporter_management_create_response(payload, -1, &response);

      pgexporter_json_put(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)config->configuration_path, ValueString);
      pgexporter_json_put(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)config->users_path, ValueString);
      pgexporter_json_put(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)config->admins_path, ValueString);

      end_time = time(NULL);

      pgexporter_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CONF_GET)
   {
      pid = fork();
      if (pid == -1)
      {
         pgexporter_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_CONF_GET_NOFORK, compression, encryption, payload);
         pgexporter_log_error("Conf Get: No fork %s (%d)", server, MANAGEMENT_ERROR_CONF_GET_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgexporter_json_clone(payload, &pyl);

         pgexporter_set_proc_title(1, ai->argv, "conf get", NULL);
         pgexporter_conf_get(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_CONF_SET)
   {
      pid = fork();
      if (pid == -1)
      {
         pgexporter_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_CONF_SET_NOFORK, compression, encryption, payload);
         pgexporter_log_error("Conf Set: No fork %s (%d)", server, MANAGEMENT_ERROR_CONF_SET_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgexporter_json_clone(payload, &pyl);

         pgexporter_set_proc_title(1, ai->argv, "conf set", NULL);
         pgexporter_conf_set(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else
   {
      pgexporter_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_UNKNOWN_COMMAND, compression, encryption, payload);
      pgexporter_log_error("Unknown: %s (%d)", pgexporter_json_to_string(payload, FORMAT_JSON, NULL, 0), MANAGEMENT_ERROR_UNKNOWN_COMMAND);
      goto error;
   }

   free(str);
   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);

   return;

error:

   free(str);
   pgexporter_json_destroy(payload);

   pgexporter_disconnect(client_fd);
}

static void
accept_transfer_cb(struct ev_loop* loop __attribute__((unused)), struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   int srv = -1;
   int fd = -1;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct configuration*)shmem;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting transfer due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgexporter_bind_unix_socket(config->unix_socket_dir, TRANSFER_UDS, &unix_transfer_socket))
         {
            pgexporter_log_fatal("pgexporter: Could not bind to %s", config->unix_socket_dir);
            exit(1);
         }

         start_mgt();

         pgexporter_log_debug("Transfer: %d", unix_transfer_socket);
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   /* Process internal transfer request */
   if (pgexporter_transfer_connection_read(client_fd, &srv, &fd))
   {
      pgexporter_log_error("Transfer: Bad payload (%d)", MANAGEMENT_ERROR_BAD_PAYLOAD);
      goto error;
   }

   pgexporter_log_debug("pgexporter: Transfer connection: Server %d FD %d", srv, fd);
   config->servers[srv].fd = fd;

   pgexporter_disconnect(client_fd);

   return;

error:

   pgexporter_disconnect(client_fd);
}

static void
accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   pid_t pid;
   struct accept_io* ai;
   struct configuration* config;
   SSL_CTX* ctx = NULL;
   SSL* client_ssl = NULL;

   if (EV_ERROR & revents)
   {
      pgexporter_log_debug("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_metrics();

         free(metrics_fds);
         metrics_fds = NULL;
         metrics_fds_length = 0;

         if (pgexporter_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
         {
            pgexporter_log_fatal("Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("Too many descriptors %d", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            pgexporter_log_debug("Metrics: %d", *(metrics_fds + i));
         }
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pid = fork();
   if (pid == -1)
   {
      pgexporter_log_error("Metrics: No fork (%d)", MANAGEMENT_ERROR_METRICS_NOFORK);
      goto error;
   }
   else if (pid == 0)
   {
      ev_loop_fork(loop);

      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      shutdown_ports();
      if (strlen(config->metrics_cert_file) > 0 && strlen(config->metrics_key_file) > 0)
      {
         if (pgexporter_create_ssl_ctx(false, &ctx))
         {
            pgexporter_log_error("Could not create metrics SSL context");
            return;
         }

         if (pgexporter_create_ssl_server(ctx, config->metrics_key_file, config->metrics_cert_file, config->metrics_ca_file, client_fd, &client_ssl))
         {
            pgexporter_log_error("Could not create metrics SSL server");
            return;
         }
      }
      pgexporter_set_proc_title(1, ai->argv, "metrics", NULL);
      pgexporter_prometheus(client_ssl, client_fd);
   }

   pgexporter_close_ssl(client_ssl);
   pgexporter_disconnect(client_fd);

   return;

error:

   pgexporter_close_ssl(client_ssl);
   pgexporter_disconnect(client_fd);
}

static void
accept_console_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   pid_t pid;
   struct accept_io* ai;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_debug("accept_console_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_console();

         free(console_fds);
         console_fds = NULL;
         console_fds_length = 0;

         if (pgexporter_bind(config->host, config->console, &console_fds, &console_fds_length))
         {
            pgexporter_log_fatal("Could not bind to %s:%d", config->host, config->console);
            exit(1);
         }

         if (console_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("Too many descriptors %d", console_fds_length);
            exit(1);
         }

         start_console();

         for (int i = 0; i < console_fds_length; i++)
         {
            pgexporter_log_debug("Console: %d", *(console_fds + i));
         }
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pid = fork();
   if (pid == -1)
   {
      pgexporter_log_error("Console: No fork (%d)", MANAGEMENT_ERROR_CONSOLE_NOFORK);
      goto error;
   }
   else if (pid == 0)
   {
      ev_loop_fork(loop);

      shutdown_ports();
      pgexporter_set_proc_title(1, ai->argv, "console", NULL);
      pgexporter_console(0, client_fd);
   }

   pgexporter_disconnect(client_fd);

   return;

error:

   pgexporter_disconnect(client_fd);
}

static void
accept_bridge_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   pid_t pid;
   struct accept_io* ai;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_debug("accept_bridge_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_bridge();

         free(bridge_fds);
         bridge_fds = NULL;
         bridge_fds_length = 0;

         if (pgexporter_bind(config->host, config->bridge, &bridge_fds, &bridge_fds_length))
         {
            pgexporter_log_fatal("Could not bind to %s:%d", config->host, config->bridge);
            exit(1);
         }

         if (bridge_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("Too many descriptors %d", bridge_fds_length);
            exit(1);
         }

         start_bridge();

         for (int i = 0; i < bridge_fds_length; i++)
         {
            pgexporter_log_debug("Bridge: %d", *(metrics_fds + i));
         }
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pid = fork();
   if (pid == -1)
   {
      pgexporter_log_error("Bridge: No fork (%d)", MANAGEMENT_ERROR_BRIDGE_NOFORK);
      goto error;
   }
   else if (pid == 0)
   {
      ev_loop_fork(loop);

      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      shutdown_ports();

      pgexporter_set_proc_title(1, ai->argv, "bridge", NULL);
      pgexporter_bridge(client_fd);
   }

   pgexporter_disconnect(client_fd);

   return;

error:

   pgexporter_disconnect(client_fd);
}

static void
accept_bridge_json_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   pid_t pid;
   struct accept_io* ai;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_debug("accept_bridge_json_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_bridge_json();

         free(bridge_json_fds);
         bridge_json_fds = NULL;
         bridge_json_fds_length = 0;

         if (pgexporter_bind(config->host, config->bridge_json, &bridge_json_fds, &bridge_json_fds_length))
         {
            pgexporter_log_fatal("Could not bind to %s:%d", config->host, config->bridge_json);
            exit(1);
         }

         if (bridge_json_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("Too many descriptors %d", bridge_json_fds_length);
            exit(1);
         }

         start_bridge_json();

         for (int i = 0; i < bridge_json_fds_length; i++)
         {
            pgexporter_log_debug("Bridge JSON: %d", *(metrics_fds + i));
         }
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pid = fork();
   if (pid == -1)
   {
      pgexporter_log_error("Bridge JSON: No fork (%d)", MANAGEMENT_ERROR_BRIDGE_JSON_NOFORK);
      goto error;
   }
   else if (pid == 0)
   {
      ev_loop_fork(loop);

      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      shutdown_ports();

      pgexporter_set_proc_title(1, ai->argv, "bridge_json", NULL);
      pgexporter_bridge_json(client_fd);
   }

   pgexporter_disconnect(client_fd);

   return;

error:

   pgexporter_disconnect(client_fd);
}

static void
accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_debug("accept_management_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   memset(&address, 0, sizeof(address));

   config = (struct configuration*)shmem;

   errno = 0;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgexporter_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_management();

         free(management_fds);
         management_fds = NULL;
         management_fds_length = 0;

         if (pgexporter_bind(config->host, config->management, &management_fds, &management_fds_length))
         {
            pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("pgexporter: Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgexporter_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pgexporter_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   if (!fork())
   {
      char* addr = strdup(address);

      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgexporter_remote_management(client_fd, addr);
   }

   pgexporter_disconnect(client_fd);
}

static void
shutdown_cb(struct ev_loop* loop, ev_signal* w __attribute__((unused)), int revents __attribute__((unused)))
{
   pgexporter_log_debug("pgexporter: shutdown requested");
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
reload_cb(struct ev_loop* loop __attribute__((unused)), ev_signal* w __attribute__((unused)), int revents __attribute__((unused)))
{
   pgexporter_log_debug("pgexporter: reload requested");
   reload_configuration();
}

static void
coredump_cb(struct ev_loop* loop __attribute__((unused)), ev_signal* w __attribute__((unused)), int revents __attribute__((unused)))
{
   pgexporter_log_info("pgexporter: core dump requested");
   abort();
}

static void
sigchld_cb(struct ev_loop* loop __attribute__((unused)), ev_signal* w __attribute__((unused)), int revents __attribute__((unused)))
{
   while (waitpid(-1, NULL, WNOHANG) > 0)
   {
      /* Wait for child processes to finish */
   }
}

static bool
accept_fatal(int error)
{
   switch (error)
   {
      case EAGAIN:
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
#ifdef HAVE_LINUX
      case ENONET:
#endif
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
         return false;
         break;
   }

   return true;
}

static bool
reload_configuration(void)
{
   bool restart = false;
   int old_metrics;
   int old_console;
   int old_management;
   struct configuration* config;

   config = (struct configuration*)shmem;

   errno = 0;

   old_metrics = config->metrics;
   old_console = config->console;
   old_management = config->management;

   pgexporter_reload_configuration(&restart);

   if (old_metrics != config->metrics)
   {
      shutdown_metrics();

      free(metrics_fds);
      metrics_fds = NULL;
      metrics_fds_length = 0;

      if (config->metrics > 0)
      {
         /* Bind metrics socket */
         if (pgexporter_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
         {
            pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("pgexporter: Too many descriptors %d", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            pgexporter_log_debug("Metrics: %d", *(metrics_fds + i));
         }
      }
   }

   if (old_console != config->console)
   {
      shutdown_console();

      free(console_fds);
      console_fds = NULL;
      console_fds_length = 0;

      if (config->console > 0)
      {
         /* Bind console socket */
         if (pgexporter_bind(config->host, config->console, &console_fds, &console_fds_length))
         {
            pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->console);
            exit(1);
         }

         if (console_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("pgexporter: Too many descriptors %d", console_fds_length);
            exit(1);
         }

         start_console();

         for (int i = 0; i < console_fds_length; i++)
         {
            pgexporter_log_debug("Console: %d", *(console_fds + i));
         }
      }
   }

   if (old_management != config->management)
   {
      shutdown_management();

      free(management_fds);
      management_fds = NULL;
      management_fds_length = 0;

      if (config->management > 0)
      {
         /* Bind management socket */
         if (pgexporter_bind(config->host, config->management, &management_fds, &management_fds_length))
         {
            pgexporter_log_fatal("pgexporter: Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgexporter_log_fatal("pgexporter: Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgexporter_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
   }

   return restart;
}

static int
create_pidfile(void)
{
   char buffer[64];
   pid_t pid;
   int r;
   int fd;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      pid = getpid();

      fd = open(config->pidfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if (fd < 0)
      {
         warn("Could not create PID file '%s'", config->pidfile);
         goto error;
      }

      pgexporter_snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

      r = write(fd, &buffer[0], strlen(buffer));
      if (r < 0)
      {
         warn("Could not write pidfile '%s'", config->pidfile);
         goto error;
      }

      close(fd);
   }

   return 0;

error:

   return 1;
}

static void
remove_pidfile(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      unlink(config->pidfile);
   }
}

static int
create_lockfile(int port)
{
   char* f = NULL;
   int fd;

   if (port > 0)
   {
      f = pgexporter_append(f, "/tmp/pgexporter.");
      f = pgexporter_append_int(f, port);
      f = pgexporter_append(f, ".lock");

      fd = open(f, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if (fd < 0)
      {
         warn("Could not create lock file '%s'", f);
         goto error;
      }

      close(fd);
   }

   free(f);

   return 0;

error:

   free(f);

   return 1;
}

static void
remove_lockfile(int port)
{
   char* f = NULL;

   if (port > 0)
   {
      f = pgexporter_append(f, "/tmp/pgexporter.");
      f = pgexporter_append_int(f, port);
      f = pgexporter_append(f, ".lock");

      unlink(f);
   }

   free(f);
}

static void
shutdown_ports(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics > 0)
   {
      shutdown_metrics();
   }

   if (config->management > 0)
   {
      shutdown_management();
   }
}
