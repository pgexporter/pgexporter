/*
 * Copyright (C) 2023 Red Hat
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
#include <configuration.h>
#include <internal.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <prometheus.h>
#include <queries.h>
#include <query_alts.h>
#include <remote.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <yaml_configuration.h>

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

#include <openssl/crypto.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define MAX_FDS 64

static void accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void reload_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void coredump_cb(struct ev_loop* loop, ev_signal* w, int revents);
static bool accept_fatal(int error);
static void reload_configuration(void);
static int  create_pidfile(void);
static void remove_pidfile(void);
static int  create_lockfile(void);
static void remove_lockfile(void);
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
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static struct accept_io io_management[MAX_FDS];
static int* management_fds = NULL;
static int management_fds_length = -1;

static void
start_mgt(void)
{
   memset(&io_mgt, 0, sizeof(struct accept_io));
   ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_management_socket, EV_READ);
   io_mgt.socket = unix_management_socket;
   io_mgt.argv = argv_ptr;
   ev_io_start(main_loop, (struct ev_io*)&io_mgt);
}

static void
shutdown_mgt(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   ev_io_stop(main_loop, (struct ev_io*)&io_mgt);
   pgexporter_disconnect(unix_management_socket);
   errno = 0;
   pgexporter_remove_unix_socket(config->unix_socket_dir, MAIN_UDS);
   errno = 0;
}

static void
start_metrics(void)
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

static void
shutdown_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_metrics[i]);
      pgexporter_disconnect(io_metrics[i].socket);
      errno = 0;
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
   char* collector = NULL;
   char collectors[NUMBER_OF_COLLECTORS][MAX_COLLECTOR_LENGTH];
   bool daemon = false;
   pid_t pid, sid;
   struct signal_info signal_watcher[5];
   size_t shmem_size;
   size_t prometheus_cache_shmem_size = 0;
   struct configuration* config = NULL;
   int ret;
   int c;
   int collector_idx = 0;

   argv_ptr = argv;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"users", required_argument, 0, 'u'},
         {"admins", required_argument, 0, 'A'},
         {"yaml", required_argument, 0, 'Y'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'},
         {"collectors", required_argument, 0, 'C'},
         {0, 0, 0, 0}
      };
      int option_index = 0;

      c = getopt_long (argc, argv, "dV?c:u:A:Y:C:",
                       long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'c':
            configuration_path = optarg;
            break;
         case 'u':
            users_path = optarg;
            break;
         case 'A':
            admins_path = optarg;
            break;
         case 'Y':
            yaml_path = optarg;
            break;
         case 'd':
            daemon = true;
            break;
         case 'V':
            version();
            break;
         case 'C':
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
                  strncpy(collectors[collector_idx++], collector, MAX_COLLECTOR_LENGTH - 1);
               }
            }

            break;
         case '?':
            usage();
            exit(1);
            break;
         default:
            break;
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

   /* Configuration File */
   if (configuration_path != NULL)
   {
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
   memcpy(&config->configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

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
      memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
   }
   else
   {
      users_path = "/etc/pgexporter/pgexporter_users.conf";
      ret = pgexporter_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
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
      memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
   }
   else
   {
      admins_path = "/etc/pgexporter/pgexporter_admins.conf";
      ret = pgexporter_read_admins_configuration(shmem, admins_path);
      if (ret == 0)
      {
         memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
      }
   }

   if (pgexporter_init_logging())
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to init logging");
#endif
      exit(1);
   }

   if (pgexporter_start_logging())
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      exit(1);
   }

   /* Internal Metrics Collectors YAML File */
   pgexporter_read_internal_yaml_metrics(config, true);

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
   if (yaml_path != NULL)
   {
      memcpy(config->metrics_path, yaml_path, MIN(strlen(yaml_path), MAX_PATH - 1));
   }
   if (strlen(config->metrics_path) > 0)
   {
      if (pgexporter_read_metrics_configuration(shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid metrics yaml");
#endif
         exit(1);
      }
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

   if (create_pidfile())
   {
      exit(1);
   }

   if (create_lockfile())
   {
      exit(1);
   }

   pgexporter_set_proc_title(argc, argv, "main", NULL);

   if (pgexporter_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
      errx(1, "Error in creating and initializing prometheus cache shared memory");
   }

   /* Bind Unix Domain Socket */
   if (pgexporter_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgexporter_log_fatal("pgexporter: Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
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

   for (int i = 0; i < 5; i++)
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

   start_mgt();

   if (config->metrics > 0)
   {
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
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgexporter_log_debug("Metrics: %d", *(metrics_fds + i));
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
              "MAINPID=%lu", (unsigned long)getpid());
#endif

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
   shutdown_metrics();
   shutdown_mgt();

   for (int i = 0; i < 5; i++)
   {
      ev_signal_stop(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(main_loop);

   free(metrics_fds);
   free(management_fds);

   remove_pidfile();
   remove_lockfile();

   pgexporter_stop_logging();

   pgexporter_free_query_alts(config);

   pgexporter_destroy_shared_memory(shmem, shmem_size);
   pgexporter_destroy_shared_memory(prometheus_cache_shmem, prometheus_cache_shmem_size);

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
   signed char id;
   int payload_i1;
   int payload_i2;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct configuration*)shmem;

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
            pgexporter_log_fatal("pgexporter: Could not bind to %s", config->unix_socket_dir);
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

   /* Process internal management request -- f.ex. returning a file descriptor to the pool */
   if (pgexporter_management_read_header(client_fd, &id))
   {
      goto disconnect;
   }
   if (pgexporter_management_read_payload(client_fd, id, &payload_i1, &payload_i2))
   {
      goto disconnect;
   }

   switch (id)
   {
      case MANAGEMENT_TRANSFER_CONNECTION:
         pgexporter_log_debug("pgexporter: Management transfer connection: Server %d FD %d", payload_i1, payload_i2);
         config->servers[payload_i1].fd = payload_i2;
         break;
      case MANAGEMENT_STOP:
         pgexporter_log_debug("pgexporter: Management stop");
         ev_break(loop, EVBREAK_ALL);
         keep_running = 0;
         stop = 1;
         break;
      case MANAGEMENT_STATUS:
         pgexporter_log_debug("pgexporter: Management status");
         pgexporter_management_write_status(client_fd);
         break;
      case MANAGEMENT_DETAILS:
         pgexporter_log_debug("pgexporter: Management details");
         pgexporter_management_write_details(client_fd);
         break;
      case MANAGEMENT_ISALIVE:
         pgexporter_log_debug("pgexporter: Management isalive");
         pgexporter_management_write_isalive(client_fd);
         break;
      case MANAGEMENT_RESET:
         pgexporter_log_debug("pgexporter: Management reset");
         pgexporter_prometheus_reset();
         break;
      case MANAGEMENT_RELOAD:
         pgexporter_log_debug("pgexporter: Management reload");
         reload_configuration();
         break;
      default:
         pgexporter_log_debug("pgexporter: Unknown management id: %d", id);
         break;
   }

disconnect:

   pgexporter_disconnect(client_fd);
}

static void
accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgexporter_log_debug("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;

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
      else
      {
         pgexporter_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   if (!fork())
   {
      ev_loop_fork(loop);
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      shutdown_ports();
      pgexporter_prometheus(client_fd);
   }

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
      char* addr = malloc(strlen(address) + 1);
      memset(addr, 0, strlen(address) + 1);
      memcpy(addr, address, strlen(address));

      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgexporter_remote_management(client_fd, addr);
   }

   pgexporter_disconnect(client_fd);
}

static void
shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgexporter_log_debug("pgexporter: shutdown requested");
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
reload_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgexporter_log_debug("pgexporter: reload requested");
   reload_configuration();
}

static void
coredump_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgexporter_log_info("pgexporter: core dump requested");
   abort();
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

static void
reload_configuration(void)
{
   int old_metrics;
   int old_management;
   struct configuration* config;

   config = (struct configuration*)shmem;

   old_metrics = config->metrics;
   old_management = config->management;

   pgexporter_reload_configuration();

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

      snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

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
create_lockfile(void)
{
   char* f = NULL;
   int fd;

   f = pgexporter_append(f, "/tmp/pgexporter.lock");

   fd = open(f, O_WRONLY | O_CREAT | O_EXCL, 0644);
   if (fd < 0)
   {
      warn("Could not create lock file '%s'", f);
      goto error;
   }

   close(fd);

   free(f);

   return 0;

error:

   free(f);

   return 1;
}

static void
remove_lockfile(void)
{
   char* f = NULL;

   f = pgexporter_append(f, "/tmp/pgexporter.lock");

   unlink(f);

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
