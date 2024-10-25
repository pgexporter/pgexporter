/*
 * Copyright (C) 2024 The pgexporter community
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

static int  process_result(SSL* ssl, int socket, int32_t output_format);

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
   int c;
   int option_index = 0;
   /* Store the result from command parser*/
   bool matched = false;
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
         {"format", required_argument, 0, 'F'},
         {"help", no_argument, 0, '?'},
         {"compress", required_argument, 0, 'C'},
         {"encrypt", required_argument, 0, 'E'}
      };

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:F:C:E:",
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
         case 'F':
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
            break;
         case 'C':
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
            break;
         case 'E':
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

   matched = true;

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

   if (configuration_path != NULL)
   {
      if (matched && exit_code != 0)
      {
         warnx("No connection to pgexporter on %s", config->unix_socket_dir);
      }
   }

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
