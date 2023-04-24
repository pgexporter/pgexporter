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
#include <logging.h>
#include <management.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <openssl/ssl.h>

#define ACTION_UNKNOWN      0
#define ACTION_STOP         6
#define ACTION_STATUS       7
#define ACTION_DETAILS      8
#define ACTION_ISALIVE      9
#define ACTION_RESET       10
#define ACTION_RELOAD      11
#define ACTION_HELP        99

static int find_action(int argc, char** argv, int* place);

static int stop(SSL* ssl, int socket);
static int status(SSL* ssl, int socket);
static int details(SSL* ssl, int socket);
static int isalive(SSL* ssl, int socket);
static int reset(SSL* ssl, int socket);
static int reload(SSL* ssl, int socket);

static void
version(void)
{
   printf("pgexporter-cli %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgexporter-cli %s\n", VERSION);
   printf("  Command line utility for pgexporter\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgexporter-cli [ -c CONFIG_FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE Set the path to the pgexporter.conf file\n");
   printf("  -h, --host HOST          Set the host name\n");
   printf("  -p, --port PORT          Set the port number\n");
   printf("  -U, --user USERNAME      Set the user name\n");
   printf("  -P, --password PASSWORD  Set the password\n");
   printf("  -L, --logfile FILE       Set the log file\n");
   printf("  -v, --verbose            Output text string of result\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  is-alive                 Is pgexporter alive\n");
   printf("  stop                     Stop pgexporter\n");
   printf("  status                   Status of pgexporter\n");
   printf("  details                  Detailed status of pgexporter\n");
   printf("  reload                   Reload the configuration\n");
   printf("  reset                    Reset the Prometheus statistics\n");
   printf("\n");
   printf("pgexporter: %s\n", PGEXPORTER_HOMEPAGE);
   printf("Report bugs: %s\n", PGEXPORTER_ISSUES);
}

int
main(int argc, char** argv)
{
   int socket = -1;
   SSL* s_ssl = NULL;
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   char* host = NULL;
   char* port = NULL;
   char* username = NULL;
   char* password = NULL;
   bool verbose = false;
   char* logfile = NULL;
   /* char* server = NULL; */
   /* char* id = NULL; */
   /* char* pos = NULL; */
   /* char* dir = NULL; */
   bool do_free = true;
   int c;
   int option_index = 0;
   size_t size;
   int position;
   int32_t action = ACTION_UNKNOWN;
   char un[MAX_USERNAME_LENGTH];
   struct configuration* config = NULL;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"host", required_argument, 0, 'h'},
         {"port", required_argument, 0, 'p'},
         {"user", required_argument, 0, 'U'},
         {"password", required_argument, 0, 'P'},
         {"logfile", required_argument, 0, 'L'},
         {"verbose", no_argument, 0, 'v'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'},
         {0, 0, 0, 0}
      };

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:",
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
         case 'h':
            host = optarg;
            break;
         case 'p':
            port = optarg;
            break;
         case 'U':
            username = optarg;
            break;
         case 'P':
            password = optarg;
            break;
         case 'L':
            logfile = optarg;
            break;
         case 'v':
            verbose = true;
            break;
         case 'V':
            version();
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
      warnx("pgexporter-cli: Using the root account is not allowed");
      exit(1);
   }

   if (configuration_path != NULL && (host != NULL || port != NULL))
   {
      warnx("pgexporter-cli: Use either -c or -h/-p to define endpoint");
      exit(1);
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   size = sizeof(struct configuration);
   if (pgexporter_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgexporter-cli: Error creating shared memory");
      exit(1);
   }
   pgexporter_init_configuration(shmem);

   if (configuration_path != NULL)
   {
      ret = pgexporter_read_configuration(shmem, configuration_path);
      if (ret)
      {
         warnx("pgexporter-cli: Configuration not found: %s", configuration_path);
         exit(1);
      }

      if (logfile)
      {
         config = (struct configuration*)shmem;

         config->log_type = PGEXPORTER_LOGGING_TYPE_FILE;
         memset(&config->log_path[0], 0, MISC_LENGTH);
         memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }

      if (pgexporter_start_logging())
      {
         exit(1);
      }

      config = (struct configuration*)shmem;
   }
   else
   {
      ret = pgexporter_read_configuration(shmem, "/etc/pgexporter/pgexporter.conf");
      if (ret)
      {
         if (host == NULL || port == NULL)
         {
            warnx("pgexporter-cli: Host and port must be specified");
            exit(1);
         }
      }
      else
      {
         configuration_path = "/etc/pgexporter/pgexporter.conf";

         if (logfile)
         {
            config = (struct configuration*)shmem;

            config->log_type = PGEXPORTER_LOGGING_TYPE_FILE;
            memset(&config->log_path[0], 0, MISC_LENGTH);
            memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
         }

         if (pgexporter_start_logging())
         {
            exit(1);
         }

         config = (struct configuration*)shmem;
      }
   }

   if (argc > 0)
   {
      action = find_action(argc, argv, &position);

      if (action == ACTION_STOP)
      {
         /* Ok */
      }
      else if (action == ACTION_STATUS)
      {
         /* Ok */
      }
      else if (action == ACTION_DETAILS)
      {
         /* Ok */
      }
      else if (action == ACTION_ISALIVE)
      {
         /* Ok */
      }
      else if (action == ACTION_RESET)
      {
         /* Ok */
      }
      else if (action == ACTION_RELOAD)
      {
         /* Local connection only */
         if (configuration_path == NULL)
         {
            action = ACTION_UNKNOWN;
         }
      }

      if (action != ACTION_UNKNOWN)
      {
         if (configuration_path != NULL)
         {
            /* Local connection */
            if (pgexporter_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
            {
               exit_code = 1;
               goto done;
            }
         }
         else
         {
            /* Remote connection */
            if (pgexporter_connect(host, atoi(port), &socket))
            {
               warnx("pgexporter-cli: No route to host: %s:%s", host, port);
               goto done;
            }

            /* User name */
            if (username == NULL)
            {
username:
               printf("User name: ");

               memset(&un, 0, sizeof(un));
               if (fgets(&un[0], sizeof(un), stdin) == NULL)
               {
                  exit_code = 1;
                  goto done;
               }
               un[strlen(un) - 1] = 0;
               username = &un[0];
            }

            if (username == NULL || strlen(username) == 0)
            {
               goto username;
            }

            /* Password */
            if (password == NULL)
            {
password:
               if (password != NULL)
               {
                  free(password);
                  password = NULL;
               }

               printf("Password : ");
               password = pgexporter_get_password();
               printf("\n");
            }
            else
            {
               do_free = false;
            }

            for (int i = 0; i < strlen(password); i++)
            {
               if ((unsigned char)(*(password + i)) & 0x80)
               {
                  goto password;
               }
            }

            /* Authenticate */
            if (pgexporter_remote_management_scram_sha256(username, password, socket, &s_ssl) != AUTH_SUCCESS)
            {
               warnx("pgexporter-cli: Bad credentials for %s", username);
               goto done;
            }
         }
      }

      if (action == ACTION_STOP)
      {
         exit_code = stop(s_ssl, socket);
      }
      else if (action == ACTION_STATUS)
      {
         exit_code = status(s_ssl, socket);
      }
      else if (action == ACTION_DETAILS)
      {
         exit_code = details(s_ssl, socket);
      }
      else if (action == ACTION_ISALIVE)
      {
         exit_code = isalive(s_ssl, socket);
      }
      else if (action == ACTION_RESET)
      {
         exit_code = reset(s_ssl, socket);
      }
      else if (action == ACTION_RELOAD)
      {
         exit_code = reload(s_ssl, socket);
      }
   }

done:

   if (s_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(s_ssl);
      res = SSL_shutdown(s_ssl);
      if (res == 0)
      {
         SSL_shutdown(s_ssl);
      }
      SSL_free(s_ssl);
      SSL_CTX_free(ctx);
   }

   pgexporter_disconnect(socket);

   if (action == ACTION_UNKNOWN)
   {
      usage();
      exit_code = 1;
   }

   if (configuration_path != NULL)
   {
      if (action == ACTION_HELP)
      {
         /* Ok */
      }
      else if (action != ACTION_UNKNOWN && exit_code != 0)
      {
         warnx("No connection to pgexporter on %s", config->unix_socket_dir);
      }
   }

   pgexporter_stop_logging();
   pgexporter_destroy_shared_memory(shmem, size);

   if (do_free)
   {
      free(password);
   }

   if (verbose)
   {
      if (exit_code == 0)
      {
         printf("Success (0)\n");
      }
      else
      {
         printf("Error (%d)\n", exit_code);
      }
   }

   return exit_code;
}

static int
find_action(int argc, char** argv, int* place)
{
   *place = -1;

   for (int i = 1; i < argc; i++)
   {
      if (!strcmp("stop", argv[i]))
      {
         *place = i;
         return ACTION_STOP;
      }
      else if (!strcmp("status", argv[i]))
      {
         *place = i;
         return ACTION_STATUS;
      }
      else if (!strcmp("details", argv[i]))
      {
         *place = i;
         return ACTION_DETAILS;
      }
      else if (!strcmp("is-alive", argv[i]))
      {
         *place = i;
         return ACTION_ISALIVE;
      }
      else if (!strcmp("reset", argv[i]))
      {
         *place = i;
         return ACTION_RESET;
      }
      else if (!strcmp("reload", argv[i]))
      {
         *place = i;
         return ACTION_RELOAD;
      }
   }

   return ACTION_UNKNOWN;
}

static int
stop(SSL* ssl, int socket)
{
   if (pgexporter_management_stop(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
status(SSL* ssl, int socket)
{
   if (pgexporter_management_status(ssl, socket) == 0)
   {
      pgexporter_management_read_status(ssl, socket);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
details(SSL* ssl, int socket)
{
   if (pgexporter_management_details(ssl, socket) == 0)
   {
      pgexporter_management_read_details(ssl, socket);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
isalive(SSL* ssl, int socket)
{
   int status = -1;

   if (pgexporter_management_isalive(ssl, socket) == 0)
   {
      if (pgexporter_management_read_isalive(ssl, socket, &status))
      {
         return 1;
      }

      if (status != 1 && status != 2)
      {
         return 1;
      }
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
reset(SSL* ssl, int socket)
{
   if (pgexporter_management_reset(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
reload(SSL* ssl, int socket)
{
   if (pgexporter_management_reload(ssl, socket))
   {
      return 1;
   }

   return 0;
}
