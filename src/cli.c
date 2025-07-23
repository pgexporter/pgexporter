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
#include <cmd.h>
#include <configuration.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>

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

#define HELP 99

#define COMMAND_RESET "reset"
#define COMMAND_RELOAD "reload"
#define COMMAND_PING "ping"
#define COMMAND_SHUTDOWN "shutdown"
#define COMMAND_STATUS "status"
#define COMMAND_STATUS_DETAILS "status-details"
#define COMMAND_CONF "conf"
#define COMMAND_CLEAR "clear"

#define OUTPUT_FORMAT_JSON "json"
#define OUTPUT_FORMAT_TEXT "text"

#define UNSPECIFIED "Unspecified"

static void help_shutdown(void);
static void help_ping(void);
static void help_status_details(void);
static void help_conf(void);
static void help_clear(void);
static void display_helper(char* command);

static int pgexporter_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int reset(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format);

static int  process_result(SSL* ssl, int socket, int32_t output_format);
static int process_ls_result(SSL* ssl, int socket, int32_t output_format);
static int process_get_result(SSL* ssl, int socket, char* config_key, int32_t output_format);
static int process_set_result(SSL* ssl, int socket, char* config_key, int32_t output_format);

static int get_conf_path_result(struct json* j, uintptr_t* r);
static int get_config_key_result(char* config_key, struct json* j, uintptr_t* r, int32_t output_format);

static char* translate_command(int32_t cmd_code);
static char* translate_output_format(int32_t out_code);
static char* translate_compression(int32_t compression_code);
static char* translate_encryption(int32_t encryption_code);
static void translate_json_object(struct json* j);

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
   printf("  -c, --config CONFIG_FILE                       Set the path to the pgexporter.conf file\n");
   printf("  -h, --host HOST                                Set the host name\n");
   printf("  -p, --port PORT                                Set the port number\n");
   printf("  -U, --user USERNAME                            Set the user name\n");
   printf("  -P, --password PASSWORD                        Set the password\n");
   printf("  -L, --logfile FILE                             Set the log file\n");
   printf("  -v, --verbose                                  Output text string of result\n");
   printf("  -V, --version                                  Display version information\n");
   printf("  -F, --format text|json|raw                     Set the output format\n");
   printf("  -C, --compress none|gz|zstd|lz4|bz2            Compress the wire protocol\n");
   printf("  -E, --encrypt none|aes|aes256|aes192|aes128    Encrypt the wire protocol\n");
   printf("  -?, --help                                     Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  ping                     Check if pgexporter is alive\n");
   printf("  shutdown                 Shutdown pgexporter\n");
   printf("  status [details]         Status of pgexporter, with optional details\n");
   printf("  conf <action>            Manage the configuration, with one of subcommands:\n");
   printf("                           - 'reload' to reload the configuration\n");
   printf("                           - 'ls' to print the configurations used\n");
   printf("                           - 'get' to obtain information about a runtime configuration value\n");
   printf("                           - 'set' to modify a configuration value;\n");
   printf("  clear <what>             Clear data, with:\n");
   printf("                           - 'prometheus' to reset the Prometheus statistics\n");
   printf("\n");
   printf("pgexporter: %s\n", PGEXPORTER_HOMEPAGE);
   printf("Report bugs: %s\n", PGEXPORTER_ISSUES);
}

const struct pgexporter_command command_table[] = {
   {
      .command = "ping",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_PING,
      .deprecated = false,
      .log_message = "<ping>"
   },
   {
      .command = "shutdown",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_SHUTDOWN,
      .deprecated = false,
      .log_message = "<shutdown>"
   },
   {
      .command = "status",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_STATUS,
      .deprecated = false,
      .log_message = "<status>"
   },
   {
      .command = "status",
      .subcommand = "details",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_STATUS_DETAILS,
      .deprecated = false,
      .log_message = "<status details>"
   },
   {
      .command = "conf",
      .subcommand = "reload",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_RELOAD,
      .deprecated = false,
      .log_message = "<conf reload>"
   },
   {
      .command = "conf",
      .subcommand = "ls",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_CONF_LS,
      .deprecated = false,
      .log_message = "<conf ls>"
   },
   {
      .command = "conf",
      .subcommand = "get",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_CONF_GET,
      .deprecated = false,
      .log_message = "<conf get> [%s]"
   },
   {
      .command = "conf",
      .subcommand = "set",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_CONF_SET,
      .deprecated = false,
      .log_message = "<conf set> [%s]"
   },
   {
      .command = "clear",
      .subcommand = "prometheus",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_RESET,
      .deprecated = false,
      .log_message = "<clear prometheus>"
   }
};

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
   bool do_free = true;
   /* Store the result from command parser*/
   size_t size;
   char un[MAX_USERNAME_LENGTH];
   struct configuration* config = NULL;
   int32_t output_format = MANAGEMENT_OUTPUT_FORMAT_TEXT;
   int32_t compression = MANAGEMENT_COMPRESSION_NONE;
   int32_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   size_t command_count = sizeof(command_table) / sizeof(struct pgexporter_command);
   struct pgexporter_parsed_command parsed = {.cmd = NULL, .args = {0}};

   // Disable stdout buffering (i.e. write to stdout immediatelly).
   setbuf(stdout, NULL);

   char* filepath = NULL;
   int optind = 0;
   int num_options = 0;
   int num_results = 0;

   cli_option options[] = {
      {"c", "config", true},
      {"h", "host", true},
      {"p", "port", true},
      {"U", "user", true},
      {"P", "password", true},
      {"L", "logfile", true},
      {"v", "verbose", false},
      {"V", "version", false},
      {"F", "format", true},
      {"?", "help", false},
      {"C", "compress", true},
      {"E", "encrypt", true}
   };

   num_options = sizeof(options) / sizeof(cli_option);
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
      else if (!strcmp(optname, "host") || !strcmp(optname, "h"))
      {
         host = optarg;
      }
      else if (!strcmp(optname, "port") || !strcmp(optname, "p"))
      {
         port = optarg;
      }
      else if (!strcmp(optname, "user") || !strcmp(optname, "U"))
      {
         username = optarg;
      }
      else if (!strcmp(optname, "password") || !strcmp(optname, "P"))
      {
         password = optarg;
      }
      else if (!strcmp(optname, "logfile") || !strcmp(optname, "L"))
      {
         logfile = optarg;
      }
      else if (!strcmp(optname, "verbose") || !strcmp(optname, "v"))
      {
         verbose = true;
      }
      else if (!strcmp(optname, "version") || !strcmp(optname, "V"))
      {
         version();
      }
      else if (!strcmp(optname, "format") || !strcmp(optname, "F"))
      {
         if (!strncmp(optarg, "json", MISC_LENGTH))
         {
            output_format = MANAGEMENT_OUTPUT_FORMAT_JSON;
         }
         else if (!strncmp(optarg, "raw", MISC_LENGTH))
         {
            output_format = MANAGEMENT_OUTPUT_FORMAT_RAW;
         }
         else if (!strncmp(optarg, "text", MISC_LENGTH))
         {
            output_format = MANAGEMENT_OUTPUT_FORMAT_TEXT;
         }
         else
         {
            warnx("pgexporter-cli: Format type is not correct");
            exit(1);
         }
      }
      else if (!strcmp(optname, "compress") || !strcmp(optname, "C"))
      {
         if (!strncmp(optarg, "gz", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_GZIP;
         }
         else if (!strncmp(optarg, "zstd", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_ZSTD;
         }
         else if (!strncmp(optarg, "lz4", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_LZ4;
         }
         else if (!strncmp(optarg, "bz2", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_BZIP2;
         }
         else if (!strncmp(optarg, "none", MISC_LENGTH))
         {
            break;
         }
         else
         {
            warnx("pgexporter-cli: Compress method is not correct");
            exit(1);
         }
      }
      else if (!strcmp(optname, "encrypt") || !strcmp(optname, "E"))
      {
         if (!strncmp(optarg, "aes", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES256;
         }
         else if (!strncmp(optarg, "aes256", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES256;
         }
         else if (!strncmp(optarg, "aes192", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES192;
         }
         else if (!strncmp(optarg, "aes128", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES128;
         }
         else if (!strncmp(optarg, "none", MISC_LENGTH))
         {
            break;
         }
         else
         {
            warnx("pgexporter-cli: Encrypt method is not correct");
            exit(1);
         }
      }
      else if (!strcmp(optname, "help") || !strcmp(optname, "?"))
      {
         usage();
         exit(1);
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

   pgexporter_memory_init();

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
   if (!parse_command(argc, argv, optind, &parsed, command_table, command_count))
   {
      if (argc > optind)
      {
         char* command = argv[optind];
         display_helper(command);
      }
      else
      {
         usage();
      }
      exit_code = 1;
      goto done;
   }

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

      for (size_t i = 0; i < strlen(password); i++)
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

   if (parsed.cmd->action == MANAGEMENT_SHUTDOWN)
   {
      exit_code = pgexporter_shutdown(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_STATUS)
   {
      exit_code = status(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_STATUS_DETAILS)
   {
      exit_code = details(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_PING)
   {
      exit_code = ping(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RESET)
   {
      exit_code = reset(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RELOAD)
   {
      exit_code = reload(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONF_LS)
   {
      exit_code = conf_ls(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONF_GET)
   {
      if (parsed.args[0])
      {
         exit_code = conf_get(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         exit_code = conf_get(s_ssl, socket, NULL, compression, encryption, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_CONF_SET)
   {
      exit_code = conf_set(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
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

   pgexporter_stop_logging();
   pgexporter_destroy_shared_memory(shmem, size);

   pgexporter_memory_destroy();

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

static void
help_shutdown(void)
{
   printf("Shutdown pgexporter\n");
   printf("  pgexporter-cli shutdown\n");
}

static void
help_ping(void)
{
   printf("Check if pgexporter is alive\n");
   printf("  pgexporter-cli ping\n");
}

static void
help_status_details(void)
{
   printf("Status of pgexporter\n");
   printf("  pgexporter-cli status [details]\n");
}

static void
help_conf(void)
{
   printf("Manage the configuration\n");
   printf("  pgexporter-cli conf [reload]\n");
   printf("  pgexporter-cli conf [ls]\n");
   printf("  pgexporter-cli conf [get] <parameter_name>\n");
   printf("  pgexporter-cli conf [set] <parameter_name> <parameter_value>\n");
}

static void
help_clear(void)
{
   printf("Reset data\n");
   printf("  pgexporter-cli clear [prometheus]\n");
}

static void
display_helper(char* command)
{
   if (!strcmp(command, COMMAND_PING))
   {
      help_ping();
   }
   else if (!strcmp(command, COMMAND_SHUTDOWN))
   {
      help_shutdown();
   }
   else if (!strcmp(command, COMMAND_STATUS))
   {
      help_status_details();
   }
   else if (!strcmp(command, COMMAND_CONF))
   {
      help_conf();
   }
   else if (!strcmp(command, COMMAND_CLEAR))
   {
      help_clear();
   }
   else
   {
      usage();
   }
}

static int
pgexporter_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_shutdown(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_status(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_details(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_ping(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
reset(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_reset(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_reload(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_conf_ls(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_ls_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_conf_get(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_get_result(ssl, socket, config_key, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgexporter_management_request_conf_set(ssl, socket, config_key, config_value, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_set_result(ssl, socket, config_key, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
process_result(SSL* ssl, int socket, int32_t output_format)
{
   struct json* read = NULL;

   if (pgexporter_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (MANAGEMENT_OUTPUT_FORMAT_RAW != output_format)
   {
      translate_json_object(read);
   }

   if (MANAGEMENT_OUTPUT_FORMAT_TEXT == output_format)
   {
      pgexporter_json_print(read, FORMAT_TEXT);
   }
   else
   {
      pgexporter_json_print(read, FORMAT_JSON);
   }

   pgexporter_json_destroy(read);

   return 0;

error:

   pgexporter_json_destroy(read);

   return 1;
}

static int
process_ls_result(SSL* ssl, int socket, int32_t output_format)
{
   struct json* read = NULL;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgexporter_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (get_conf_path_result(read, &res))
   {
      goto error;
   }

   json_res = (struct json*)res;

   if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
   {
      pgexporter_json_print(json_res, FORMAT_JSON_COMPACT);
   }
   else
   {
      struct json_iterator* iter = NULL;
      pgexporter_json_iterator_create(json_res, &iter);
      while (pgexporter_json_iterator_next(iter))
      {
         char* value = pgexporter_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         printf("%s\n", value);
         free(value);
      }
      pgexporter_json_iterator_destroy(iter);
   }

   pgexporter_json_destroy(read);
   pgexporter_json_destroy(json_res);
   return 0;

error:

   pgexporter_json_destroy(read);
   pgexporter_json_destroy(json_res);
   return 1;
}

static int
process_get_result(SSL* ssl, int socket, char* config_key, int32_t output_format)
{
   struct json* read = NULL;
   bool is_char = false;
   char* char_res = NULL;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgexporter_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (get_config_key_result(config_key, read, &res, output_format))
   {
      if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
      {
         json_res = (struct json*)res;
         pgexporter_json_print(json_res, FORMAT_JSON_COMPACT);
      }
      else
      {
         is_char = true;
         char_res = (char*)res;
         printf("%s\n", char_res);
      }
      goto error;
   }

   if (!config_key)  // error response | complete configuration
   {
      json_res = (struct json*)res;

      if (MANAGEMENT_OUTPUT_FORMAT_RAW != output_format)
      {
         translate_json_object(json_res);
      }

      if (MANAGEMENT_OUTPUT_FORMAT_TEXT == output_format)
      {
         pgexporter_json_print(json_res, FORMAT_TEXT);
      }
      else
      {
         pgexporter_json_print(json_res, FORMAT_JSON);
      }
   }
   else
   {
      if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
      {
         json_res = (struct json*)res;
         pgexporter_json_print(json_res, FORMAT_JSON_COMPACT);
      }
      else
      {
         is_char = true;
         char_res = (char*)res;
         printf("%s\n", char_res);
      }
   }

   pgexporter_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgexporter_json_destroy(json_res);
      }
   }

   return 0;

error:

   pgexporter_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgexporter_json_destroy(json_res);
      }
   }

   return 1;
}

static int
process_set_result(SSL* ssl, int socket, char* config_key, int32_t output_format)
{
   struct json* read = NULL;
   bool is_char = false;
   char* char_res = NULL;
   int status = 0;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgexporter_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   status = get_config_key_result(config_key, read, &res, output_format);
   if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
   {
      json_res = (struct json*)res;
      pgexporter_json_print(json_res, FORMAT_JSON_COMPACT);
   }
   else
   {
      is_char = true;
      char_res = (char*)res;
      printf("%s\n", char_res);
   }

   if (status == 1)
   {
      goto error;
   }

   pgexporter_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgexporter_json_destroy(json_res);
      }
   }

   return 0;

error:

   pgexporter_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgexporter_json_destroy(json_res);
      }
   }

   return 1;
}

static int
get_conf_path_result(struct json* j, uintptr_t* r)
{
   struct json* conf_path_response = NULL;
   struct json* response = NULL;

   response = (struct json*)pgexporter_json_get(j, MANAGEMENT_CATEGORY_RESPONSE);

   if (!response)
   {
      goto error;
   }

   if (pgexporter_json_create(&conf_path_response))
   {
      goto error;
   }

   if (pgexporter_json_contains_key(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH))
   {
      pgexporter_json_put(conf_path_response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)pgexporter_json_get(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH), ValueString);
   }
   if (pgexporter_json_contains_key(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH))
   {
      pgexporter_json_put(conf_path_response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)pgexporter_json_get(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH), ValueString);
   }
   if (pgexporter_json_contains_key(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH))
   {
      pgexporter_json_put(conf_path_response, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)pgexporter_json_get(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH), ValueString);
   }

   *r = (uintptr_t)conf_path_response;

   return 0;
error:

   return 1;

}

static int
get_config_key_result(char* config_key, struct json* j, uintptr_t* r, int32_t output_format)
{
   char section[MISC_LENGTH];
   char context[MISC_LENGTH];
   char key[MISC_LENGTH];

   struct json* configuration_js = NULL;
   struct json* filtered_response = NULL;
   struct json* response = NULL;
   struct json* outcome = NULL;
   struct json_iterator* iter = NULL;
   char* config_value = NULL;
   enum value_type section_type;
   uintptr_t section_data;
   int part_count = 0;
   char* parts[4] = {NULL, NULL, NULL, NULL}; // Allow max 4 to detect invalid input
   char* token;
   char* config_key_copy = NULL;

   if (!config_key)
   {
      *r = (uintptr_t)j;
      return 0;
   }

   if (pgexporter_json_create(&filtered_response))
   {
      goto error;
   }

   memset(section, 0, MISC_LENGTH);
   memset(context, 0, MISC_LENGTH);
   memset(key, 0, MISC_LENGTH);

   size_t config_key_len = strlen(config_key);
   config_key_copy = malloc(config_key_len + 1);
   if (!config_key_copy)
   {
      goto error;
   }
   memcpy(config_key_copy, config_key, config_key_len);
   config_key_copy[config_key_len] = '\0';  // Null terminate

   if (!config_key_copy)
   {
      goto error;
   }

   // Parse the config_key by dots and count them
   token = strtok(config_key_copy, ".");
   while (token != NULL && part_count < 4) // Allow parsing up to 4 parts to detect invalid input
   {
      size_t token_len = strlen(token);
      parts[part_count] = malloc(token_len + 1);
      if (!parts[part_count])
      {
         goto error;
      }
      memcpy(parts[part_count], token, token_len);
      parts[part_count][token_len] = '\0';
      part_count++;
      token = strtok(NULL, ".");
   }

   // Validate the number of parts - only 1, 2, or 3 parts are valid
   if (part_count < 1 || part_count > 3)
   {
      pgexporter_log_warn("Invalid configuration key format: %s (only 1-3 dot-separated parts are allowed)", config_key);
      goto error;
   }

   // Assign parts based on count
   if (part_count == 1)
   {
      // Single key: config_key
      strncpy(key, parts[0], MISC_LENGTH - 1);
      key[MISC_LENGTH - 1] = '\0';
   }
   else if (part_count == 2)
   {
      // Two parts: section.key
      strncpy(section, parts[0], MISC_LENGTH - 1);
      section[MISC_LENGTH - 1] = '\0';
      strncpy(key, parts[1], MISC_LENGTH - 1);
      key[MISC_LENGTH - 1] = '\0';

      // Treat "pgexporter" as the main section (empty)
      if (!strcasecmp(section, PGEXPORTER_MAIN_INI_SECTION))
      {
         memset(section, 0, MISC_LENGTH);
      }
   }
   else if (part_count == 3)
   {
      // Three parts: section.context.key
      strncpy(section, parts[0], MISC_LENGTH - 1);
      section[MISC_LENGTH - 1] = '\0';
      strncpy(context, parts[1], MISC_LENGTH - 1);
      context[MISC_LENGTH - 1] = '\0';
      strncpy(key, parts[2], MISC_LENGTH - 1);
      key[MISC_LENGTH - 1] = '\0';
   }

   response = (struct json*)pgexporter_json_get(j, MANAGEMENT_CATEGORY_RESPONSE);
   outcome = (struct json*)pgexporter_json_get(j, MANAGEMENT_CATEGORY_OUTCOME);
   if (!response || !outcome)
   {
      goto error;
   }

   // Check if error response
   if (pgexporter_json_contains_key(outcome, MANAGEMENT_ARGUMENT_ERROR))
   {
      goto error;
   }

   if (strlen(section) > 0)
   {
      section_data = pgexporter_json_get_typed(response, section, &section_type);
      pgexporter_log_debug("Section '%s' has type: %s", section, pgexporter_value_type_to_string(section_type));

      if (section_type != ValueJSON)
      {
         goto error;
      }
      configuration_js = (struct json*)section_data;

      if (!configuration_js)
      {
         goto error;
      }
   }
   else
   {
      configuration_js = response;
   }

   if (pgexporter_json_iterator_create(configuration_js, &iter))
   {
      goto error;
   }
   while (pgexporter_json_iterator_next(iter))
   {
      // Handle three-part keys: section.context.key
      if (strlen(context) > 0)
      {
         // Looking for a specific context (like "mydb" in "limit.mydb.username")
         if (!strcmp(context, iter->key) && iter->value->type == ValueJSON)
         {
            struct json* nested_obj = (struct json*)iter->value->data;
            struct json_iterator* nested_iter;
            pgexporter_json_iterator_create(nested_obj, &nested_iter);
            while (pgexporter_json_iterator_next(nested_iter))
            {
               if (!strcmp(key, nested_iter->key))
               {
                  config_value = pgexporter_value_to_string(nested_iter->value, FORMAT_TEXT, NULL, 0);
                  if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
                  {
                     pgexporter_json_put(filtered_response, key, (uintptr_t)nested_iter->value->data, nested_iter->value->type);
                  }
                  break;
               }
            }
            pgexporter_json_iterator_destroy(nested_iter);
            break;
         }
      }
      else if (!strcmp(key, iter->key))
      {
         // Handle single or two-part keys
         config_value = pgexporter_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         if (iter->value->type == ValueJSON)
         {
            struct json* server_data = NULL;
            pgexporter_json_clone((struct json*)iter->value->data, &server_data);
            pgexporter_json_put(filtered_response, key, (uintptr_t)server_data, iter->value->type);
         }
         else
         {
            pgexporter_json_put(filtered_response, key, (uintptr_t)iter->value->data, iter->value->type);
         }
         break;
      }
   }

   pgexporter_json_iterator_destroy(iter);

   if (!config_value)  // if key doesn't match with any field in configuration
   {
      goto error;
   }

   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON || !config_key)
   {
      *r = (uintptr_t)filtered_response;

      free(config_value);
      config_value = NULL;

   }
   else
   {
      *r = (uintptr_t)config_value;
      pgexporter_json_destroy(filtered_response);
   }

   // Clean up parts
   for (int i = 0; i < part_count; i++)
   {
      free(parts[i]);
      parts[i] = NULL;
   }

   free(config_key_copy);
   config_key_copy = NULL;

   return 0;

error:
   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
   {
      pgexporter_json_put(filtered_response, "Outcome", (uintptr_t)false, ValueBool);
      *r = (uintptr_t)filtered_response;

      free(config_value);
      config_value = NULL;

   }
   else
   {

      free(config_value);
      config_value = NULL;

      config_value = (char*)malloc(6);
      if (config_value)
      {
         snprintf(config_value, 6, "Error");
      }
      *r = (uintptr_t)config_value;
      pgexporter_json_destroy(filtered_response);
   }

   // Clean up parts on error
   for (int i = 0; i < part_count; i++)
   {

      free(parts[i]);
      parts[i] = NULL;

   }

   free(config_key_copy);
   config_key_copy = NULL;

   return 1;
}

static char*
translate_command(int32_t cmd_code)
{
   char* command_output = NULL;
   switch (cmd_code)
   {
      case MANAGEMENT_SHUTDOWN:
         command_output = pgexporter_append(command_output, COMMAND_SHUTDOWN);
         break;
      case MANAGEMENT_STATUS:
         command_output = pgexporter_append(command_output, COMMAND_STATUS);
         break;
      case MANAGEMENT_STATUS_DETAILS:
         command_output = pgexporter_append(command_output, COMMAND_STATUS_DETAILS);
         break;
      case MANAGEMENT_PING:
         command_output = pgexporter_append(command_output, COMMAND_PING);
         break;
      case MANAGEMENT_RESET:
         command_output = pgexporter_append(command_output, COMMAND_RESET);
         break;
      case MANAGEMENT_RELOAD:
         command_output = pgexporter_append(command_output, COMMAND_RELOAD);
         break;
      case MANAGEMENT_CONF_LS:
         command_output = pgexporter_append(command_output, COMMAND_CONF);
         command_output = pgexporter_append_char(command_output, ' ');
         command_output = pgexporter_append(command_output, "ls");
         break;
      case MANAGEMENT_CONF_GET:
         command_output = pgexporter_append(command_output, COMMAND_CONF);
         command_output = pgexporter_append_char(command_output, ' ');
         command_output = pgexporter_append(command_output, "get");
         break;
      case MANAGEMENT_CONF_SET:
         command_output = pgexporter_append(command_output, COMMAND_CONF);
         command_output = pgexporter_append_char(command_output, ' ');
         command_output = pgexporter_append(command_output, "set");
         break;
      default:
         break;
   }
   return command_output;
}

static char*
translate_output_format(int32_t out_code)
{
   char* output_format_output = NULL;
   switch (out_code)
   {
      case MANAGEMENT_OUTPUT_FORMAT_JSON:
         output_format_output = pgexporter_append(output_format_output, OUTPUT_FORMAT_JSON);
         break;
      case MANAGEMENT_OUTPUT_FORMAT_TEXT:
         output_format_output = pgexporter_append(output_format_output, OUTPUT_FORMAT_TEXT);
         break;
      default:
         break;
   }
   return output_format_output;
}

static char*
translate_compression(int32_t compression_code)
{
   char* compression_output = NULL;
   switch (compression_code)
   {
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         compression_output = pgexporter_append(compression_output, "gzip");
         break;
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         compression_output = pgexporter_append(compression_output, "zstd");
         break;
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         compression_output = pgexporter_append(compression_output, "lz4");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         compression_output = pgexporter_append(compression_output, "bzip2");
         break;
      default:
         compression_output = pgexporter_append(compression_output, "none");
         break;
   }
   return compression_output;
}

static char*
translate_encryption(int32_t encryption_code)
{
   char* encryption_output = NULL;
   switch (encryption_code)
   {
      case ENCRYPTION_AES_256_CBC:
         encryption_output = pgexporter_append(encryption_output, "aes-256-cbc");
         break;
      case ENCRYPTION_AES_192_CBC:
         encryption_output = pgexporter_append(encryption_output, "aes-192-cbc");
         break;
      case ENCRYPTION_AES_128_CBC:
         encryption_output = pgexporter_append(encryption_output, "aes-128-cbc");
         break;
      case ENCRYPTION_AES_256_CTR:
         encryption_output = pgexporter_append(encryption_output, "aes-256-ctr");
         break;
      case ENCRYPTION_AES_192_CTR:
         encryption_output = pgexporter_append(encryption_output, "aes-192-ctr");
         break;
      case ENCRYPTION_AES_128_CTR:
         encryption_output = pgexporter_append(encryption_output, "aes-128-ctr");
         break;
      default:
         encryption_output = pgexporter_append(encryption_output, "none");
         break;
   }
   return encryption_output;
}

static void
translate_json_object(struct json* j)
{
   struct json* header = NULL;
   int32_t command = 0;
   char* translated_command = NULL;
   int32_t out_format = -1;
   char* translated_out_format = NULL;
   int32_t out_compression = -1;
   char* translated_compression = NULL;
   int32_t out_encryption = -1;
   char* translated_encryption = NULL;

   // Translate arguments of header
   header = (struct json*)pgexporter_json_get(j, MANAGEMENT_CATEGORY_HEADER);

   if (header)
   {
      command = (int32_t)pgexporter_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);
      translated_command = translate_command(command);
      if (translated_command)
      {
         pgexporter_json_put(header, MANAGEMENT_ARGUMENT_COMMAND, (uintptr_t)translated_command, ValueString);
      }

      out_format = (int32_t)pgexporter_json_get(header, MANAGEMENT_ARGUMENT_OUTPUT);
      translated_out_format = translate_output_format(out_format);
      if (translated_out_format)
      {
         pgexporter_json_put(header, MANAGEMENT_ARGUMENT_OUTPUT, (uintptr_t)translated_out_format, ValueString);
      }

      out_compression = (int32_t)pgexporter_json_get(header, MANAGEMENT_ARGUMENT_COMPRESSION);
      translated_compression = translate_compression(out_compression);
      if (translated_compression)
      {
         pgexporter_json_put(header, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)translated_compression, ValueString);
      }

      out_encryption = (int32_t)pgexporter_json_get(header, MANAGEMENT_ARGUMENT_ENCRYPTION);
      translated_encryption = translate_encryption(out_encryption);
      if (translated_encryption)
      {
         pgexporter_json_put(header, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)translated_encryption, ValueString);
      }

      free(translated_command);
      free(translated_out_format);
      free(translated_compression);
      free(translated_encryption);
   }
}
