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
#include <configuration.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ACTION_CONFIG_INIT 300
#define ACTION_CONFIG_SET  301
#define ACTION_CONFIG_GET  302
#define ACTION_CONFIG_DEL  303
#define ACTION_CONFIG_LS   304

#define INPUT_BUFFER_SIZE  1024
#define MAX_LINE_LENGTH    4096
#define MAX_LINES          8192

// clang-format off
struct pgexporter_command command_table[] =
{
   {
      .command = "init",
      .subcommand = "",
      .accepted_argument_count = {0},
      .deprecated = false,
      .action = ACTION_CONFIG_INIT,
      .log_message = "<init>",
   },
   {
      .command = "set",
      .subcommand = "",
      .accepted_argument_count = {4},
      .deprecated = false,
      .action = ACTION_CONFIG_SET,
      .log_message = "<set>",
   },
   {
      .command = "get",
      .subcommand = "",
      .accepted_argument_count = {3},
      .deprecated = false,
      .action = ACTION_CONFIG_GET,
      .log_message = "<get>",
   },
   {
      .command = "del",
      .subcommand = "",
      .accepted_argument_count = {2, 3},
      .deprecated = false,
      .action = ACTION_CONFIG_DEL,
      .log_message = "<del>",
   },
   {
      .command = "ls",
      .subcommand = "",
      .accepted_argument_count = {1, 2},
      .deprecated = false,
      .action = ACTION_CONFIG_LS,
      .log_message = "<ls>",
   },
};
// clang-format on

static void
version(void)
{
   printf("pgexporter-config %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgexporter-config %s\n", VERSION);
   printf("  Configuration utility for pgexporter\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgexporter-config [ OPTIONS ] [main|cli] [ COMMAND ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -o, --output FILE        Set the output file path (default: ./pgexporter.conf)\n");
   printf("  -q, --quiet              Generate default options without prompts (for init)\n");
   printf("  -F, --force              Force overwrite if the output file already exists\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  init                     Generate a pgexporter.conf interactively\n");
   printf("  get <file> <section> <key>\n");
   printf("                           Get a configuration value\n");
   printf("  set <file> <section> <key> <value>\n");
   printf("                           Set a configuration value\n");
   printf("  del <file> <section> [key]\n");
   printf("                           Delete a section or key\n");
   printf("  ls <file> [section]      List sections or keys in a section\n");
   printf("\n");
   printf("pgexporter: %s\n", PGEXPORTER_HOMEPAGE);
   printf("Report bugs: %s\n", PGEXPORTER_ISSUES);
}

/**
 * Prompt the user for input with a default value.
 * If the user presses Enter without input, the default is used.
 * @param prompt The prompt message
 * @param default_value The default value (can be NULL for required fields)
 * @param result The buffer to store the result
 * @param result_size The size of the result buffer
 * @return 0 upon success, otherwise 1
 */
static int
prompt_input(const char* prompt, const char* default_value, char* result, size_t result_size)
{
   char buf[INPUT_BUFFER_SIZE];

   if (default_value != NULL && strlen(default_value) > 0)
   {
      printf("%s [%s]: ", prompt, default_value);
   }
   else
   {
      printf("%s: ", prompt);
   }

   memset(buf, 0, sizeof(buf));
   if (fgets(buf, sizeof(buf), stdin) == NULL)
   {
      return 1;
   }

   /* Remove trailing newline */
   size_t len = strlen(buf);
   if (len > 0 && buf[len - 1] == '\n')
   {
      buf[len - 1] = '\0';
      len--;
   }

   if (len == 0 && default_value != NULL)
   {
      memset(result, 0, result_size);
      memcpy(result, default_value, MIN(result_size - 1, strlen(default_value)));
   }
   else if (len == 0 && default_value == NULL)
   {
      return 1;
   }
   else
   {
      memset(result, 0, result_size);
      memcpy(result, buf, MIN(result_size - 1, len));
   }

   return 0;
}

/**
 * Prompt the user for a yes/no question.
 * @param prompt The prompt message
 * @param default_yes True if default is yes
 * @return true for yes, false for no
 */
static bool
prompt_yes_no(const char* prompt, bool default_yes)
{
   char buf[INPUT_BUFFER_SIZE];

   if (default_yes)
   {
      printf("%s [Y/n]: ", prompt);
   }
   else
   {
      printf("%s [y/N]: ", prompt);
   }

   memset(buf, 0, sizeof(buf));
   if (fgets(buf, sizeof(buf), stdin) == NULL)
   {
      return default_yes;
   }

   size_t len = strlen(buf);
   if (len > 0 && buf[len - 1] == '\n')
   {
      buf[len - 1] = '\0';
      len--;
   }

   if (len == 0)
   {
      return default_yes;
   }

   if (buf[0] == 'y' || buf[0] == 'Y')
   {
      return true;
   }
   else if (buf[0] == 'n' || buf[0] == 'N')
   {
      return false;
   }

   return default_yes;
}

/**
 * Write a section header to a file.
 * @param file The file pointer
 * @param section The section name
 */
static void
write_section(FILE* file, const char* section)
{
   fprintf(file, "[%s]\n", section);
}

/**
 * Write a key-value pair to a file.
 * @param file The file pointer
 * @param key The key
 * @param value The value
 */
static void
write_key_value(FILE* file, const char* key, const char* value)
{
   fprintf(file, "%s = %s\n", key, value);
}

enum config_target {
   TARGET_MAIN,
   TARGET_CLI
};

/**
 * Interactive configuration generator.
 * @param output_path The output file path
 * @return 0 upon success, otherwise 1
 */
static int
config_init(const char* output_path, bool quiet, bool force, enum config_target target)
{
   FILE* file = NULL;
   char host[MISC_LENGTH];
   char metrics[MISC_LENGTH];
   char management[MISC_LENGTH];
   char log_type[MISC_LENGTH];
   char log_level[MISC_LENGTH];
   char log_path[MAX_PATH];
   char unix_socket_dir[MISC_LENGTH];
   struct stat st;
   char tmp_path[MAX_PATH];

   pgexporter_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", output_path);

   if (!quiet)
   {
      printf("pgexporter configuration generator\n");
      printf("==================================\n\n");
   }

   /* Check if output file already exists */
   if (stat(output_path, &st) == 0)
   {
      if (!force)
      {
         if (quiet)
         {
            warnx("Output file '%s' already exists. Use --force to overwrite.", output_path);
            return 1;
         }

         if (!prompt_yes_no("Output file already exists. Overwrite?", false))
         {
            printf("Aborted.\n");
            return 1;
         }
      }
   }

   if (!quiet)
   {
      if (target == TARGET_CLI)
      {
         printf("--- [pgexporter-cli] section ---\n\n");
      }
      else
      {
         printf("--- [pgexporter] section ---\n\n");
      }
   }

   if (quiet)
   {
      pgexporter_snprintf(host, sizeof(host), PGEXPORTER_DEFAULT_HOST);
      pgexporter_snprintf(metrics, sizeof(metrics), PGEXPORTER_DEFAULT_METRICS);
      pgexporter_snprintf(management, sizeof(management), PGEXPORTER_DEFAULT_MANAGEMENT);
      pgexporter_snprintf(log_type, sizeof(log_type), PGEXPORTER_DEFAULT_LOG_TYPE);
      pgexporter_snprintf(log_level, sizeof(log_level), PGEXPORTER_DEFAULT_LOG_LEVEL);
      pgexporter_snprintf(log_path, sizeof(log_path), PGEXPORTER_DEFAULT_LOG_PATH);
      pgexporter_snprintf(unix_socket_dir, sizeof(unix_socket_dir), PGEXPORTER_DEFAULT_UNIX_SOCKET_DIR);
   }
   else
   {
      if (prompt_input("Host (bind address)", PGEXPORTER_DEFAULT_HOST, host, sizeof(host)))
      {
         warnx("Invalid input for host");
         goto error;
      }

      if (prompt_input("Metrics port", PGEXPORTER_DEFAULT_METRICS, metrics, sizeof(metrics)))
      {
         warnx("Invalid input for metrics");
         goto error;
      }

      if (prompt_input("Management port (0 to disable)", PGEXPORTER_DEFAULT_MANAGEMENT, management, sizeof(management)))
      {
         warnx("Invalid input for management");
         goto error;
      }

      if (prompt_input("Log type (console, file, syslog)", PGEXPORTER_DEFAULT_LOG_TYPE, log_type, sizeof(log_type)))
      {
         warnx("Invalid input for log_type");
         goto error;
      }

      if (prompt_input("Log level (fatal, error, warn, info, debug)", PGEXPORTER_DEFAULT_LOG_LEVEL, log_level, sizeof(log_level)))
      {
         warnx("Invalid input for log_level");
         goto error;
      }

      if (prompt_input("Log path", PGEXPORTER_DEFAULT_LOG_PATH, log_path, sizeof(log_path)))
      {
         warnx("Invalid input for log_path");
         goto error;
      }

      if (prompt_input("Unix socket directory", PGEXPORTER_DEFAULT_UNIX_SOCKET_DIR, unix_socket_dir, sizeof(unix_socket_dir)))
      {
         warnx("Invalid input for unix_socket_dir");
         goto error;
      }
   }

   /* Open output file initially as .tmp to be safe */
   file = fopen(tmp_path, "w");
   if (file == NULL)
   {
      warn("Could not open temp file '%s'", tmp_path);
      goto error;
   }

   if (target == TARGET_CLI)
   {
      write_section(file, "pgexporter-cli");
      write_key_value(file, CONFIGURATION_ARGUMENT_HOST, host);
      fprintf(file, "\n");
      write_key_value(file, CONFIGURATION_ARGUMENT_LOG_TYPE, log_type);
      write_key_value(file, CONFIGURATION_ARGUMENT_LOG_LEVEL, log_level);
      if (strlen(log_path) > 0)
      {
         write_key_value(file, CONFIGURATION_ARGUMENT_LOG_PATH, log_path);
      }
      fprintf(file, "\n");
   }
   else
   {
      /* Write [pgexporter] section */
      write_section(file, "pgexporter");
      write_key_value(file, CONFIGURATION_ARGUMENT_HOST, host);
      write_key_value(file, CONFIGURATION_ARGUMENT_METRICS, metrics);
      if (strlen(management) > 0 && strcmp(management, "0") != 0)
      {
         write_key_value(file, CONFIGURATION_ARGUMENT_MANAGEMENT, management);
      }
      fprintf(file, "\n");
      write_key_value(file, CONFIGURATION_ARGUMENT_LOG_TYPE, log_type);
      write_key_value(file, CONFIGURATION_ARGUMENT_LOG_LEVEL, log_level);
      if (strlen(log_path) > 0)
      {
         write_key_value(file, CONFIGURATION_ARGUMENT_LOG_PATH, log_path);
      }
      fprintf(file, "\n");
      write_key_value(file, CONFIGURATION_ARGUMENT_UNIX_SOCKET_DIR, unix_socket_dir);
      fprintf(file, "\n");
   }

   if (target != TARGET_CLI)
   {
      if (quiet)
      {
         write_section(file, "primary");
         write_key_value(file, CONFIGURATION_ARGUMENT_HOST, "localhost");
         write_key_value(file, CONFIGURATION_ARGUMENT_PORT, "5432");
         write_key_value(file, CONFIGURATION_ARGUMENT_USER, "pgexporter");
         fprintf(file, "\n");
      }
      else
      {
         /* Server sections */
         while (prompt_yes_no("\nAdd a PostgreSQL server?", true))
         {
            char section_name[MISC_LENGTH];
            char server_host[MISC_LENGTH];
            char server_port[MISC_LENGTH];
            char server_user[MISC_LENGTH];

            printf("\n--- Server section ---\n\n");

            if (prompt_input("Section name", "primary", section_name, sizeof(section_name)))
            {
               warnx("Invalid input for section name");
               continue;
            }

            if (prompt_input("Host", "localhost", server_host, sizeof(server_host)))
            {
               warnx("Invalid input for server host");
               continue;
            }

            if (prompt_input("Port", "5432", server_port, sizeof(server_port)))
            {
               warnx("Invalid input for server port");
               continue;
            }

            if (prompt_input("User", "pgexporter", server_user, sizeof(server_user)))
            {
               warnx("Invalid input for server user");
               continue;
            }

            write_section(file, section_name);
            write_key_value(file, CONFIGURATION_ARGUMENT_HOST, server_host);
            write_key_value(file, CONFIGURATION_ARGUMENT_PORT, server_port);
            write_key_value(file, CONFIGURATION_ARGUMENT_USER, server_user);
            fprintf(file, "\n");
         }
      }
   }

   fflush(file);
#ifndef _WIN32
   fsync(fileno(file));
#endif
   fclose(file);
   file = NULL;

   chmod(tmp_path, S_IRUSR | S_IWUSR);

   if (rename(tmp_path, output_path) != 0)
   {
      warn("Could not rename %s to %s", tmp_path, output_path);
      unlink(tmp_path);
      goto error;
   }

   if (!quiet)
   {
      printf("\nConfiguration written to: %s\n", output_path);
   }

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
      file = NULL;
      unlink(tmp_path);
   }

   return 1;
}

/**
 * Trim leading and trailing whitespace from a string in-place.
 * @param str The string to trim
 * @return Pointer to the trimmed string (within the original buffer)
 */
static char*
trim(char* str)
{
   char* end;

   while (isspace((unsigned char)*str))
   {
      str++;
   }

   if (*str == '\0')
   {
      return str;
   }

   end = str + strlen(str) - 1;
   while (end > str && isspace((unsigned char)*end))
   {
      end--;
   }

   *(end + 1) = '\0';
   return str;
}

/**
 * Get a configuration value from a file.
 * @param file_path The config file path
 * @param section The section name
 * @param key The key name
 * @return 0 upon success, otherwise 1
 */
static int
config_get(const char* file_path, const char* section, const char* key)
{
   FILE* file = NULL;
   char line[MAX_LINE_LENGTH];
   bool in_section = false;
   char section_header[MISC_LENGTH];

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      warnx("Could not open file: %s", file_path);
      goto error;
   }

   pgexporter_snprintf(section_header, sizeof(section_header), "[%s]", section);

   while (fgets(line, sizeof(line), file) != NULL)
   {
      char* trimmed = trim(line);

      /* Skip comments and empty lines */
      if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
      {
         continue;
      }

      /* Check for section header */
      if (trimmed[0] == '[')
      {
         /* Remove trailing newline for comparison */
         char* nl = strchr(trimmed, '\n');
         if (nl)
         {
            *nl = '\0';
         }

         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            *(bracket_end + 1) = '\0';
         }

         if (!strcmp(trimmed, section_header))
         {
            in_section = true;
         }
         else if (in_section)
         {
            /* We've moved past our target section */
            break;
         }
         continue;
      }

      if (in_section)
      {
         /* Look for key = value */
         char* eq = strchr(trimmed, '=');
         if (eq != NULL)
         {
            *eq = '\0';
            char* found_key = trim(trimmed);
            char* found_value = trim(eq + 1);

            /* Remove trailing newline from value */
            char* nl = strchr(found_value, '\n');
            if (nl)
            {
               *nl = '\0';
            }

            if (!strcmp(found_key, key))
            {
               printf("%s\n", found_value);
               fclose(file);
               return 0;
            }
         }
      }
   }

   warnx("Key '%s' not found in section [%s]", key, section);

error:

   if (file != NULL)
   {
      fclose(file);
   }

   return 1;
}

/**
 * Set a configuration value in a file.
 * If the section or key doesn't exist, it is created.
 * Preserves comments, blank lines, and formatting.
 * @param file_path The config file path
 * @param section The section name
 * @param key The key name
 * @param value The value to set
 * @return 0 upon success, otherwise 1
 */
static int
config_set(const char* file_path, const char* section, const char* key, const char* value)
{
   FILE* file = NULL;
   char* lines[MAX_LINES];
   int line_count = 0;
   char line_buf[MAX_LINE_LENGTH];
   char section_header[MISC_LENGTH];
   int section_start = -1;
   int section_end = -1;
   int key_line = -1;
   bool key_found = false;
   bool section_found = false;

   memset(lines, 0, sizeof(lines));

   pgexporter_snprintf(section_header, sizeof(section_header), "[%s]", section);

   /* Read all lines */
   file = fopen(file_path, "r");
   if (file != NULL)
   {
      while (fgets(line_buf, sizeof(line_buf), file) != NULL && line_count < MAX_LINES)
      {
         lines[line_count] = strdup(line_buf);
         if (lines[line_count] == NULL)
         {
            warnx("Out of memory");
            goto error;
         }
         line_count++;
      }
      fclose(file);
      file = NULL;
   }

   /* Find the section and key */
   for (int i = 0; i < line_count; i++)
   {
      char temp[MAX_LINE_LENGTH];
      memset(temp, 0, sizeof(temp));
      memcpy(temp, lines[i], MIN(sizeof(temp) - 1, strlen(lines[i])));

      char* trimmed = trim(temp);

      /* Skip comments and empty lines */
      if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
      {
         continue;
      }

      /* Check for section header */
      if (trimmed[0] == '[')
      {
         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            *(bracket_end + 1) = '\0';
         }

         if (!strcmp(trimmed, section_header))
         {
            section_found = true;
            section_start = i;
            section_end = line_count;
         }
         else if (section_found && section_end == line_count)
         {
            section_end = i;
         }
      }

      /* Look for key in our section */
      if (section_found && !key_found && i > section_start && (section_end == line_count || i < section_end))
      {
         char key_temp[MAX_LINE_LENGTH];
         memset(key_temp, 0, sizeof(key_temp));
         memcpy(key_temp, lines[i], MIN(sizeof(key_temp) - 1, strlen(lines[i])));

         char* kt = trim(key_temp);
         if (kt[0] != '#' && kt[0] != ';' && kt[0] != '[' && kt[0] != '\0')
         {
            char* eq = strchr(kt, '=');
            if (eq != NULL)
            {
               *eq = '\0';
               char* found_key = trim(kt);
               if (!strcmp(found_key, key))
               {
                  key_found = true;
                  key_line = i;
               }
            }
         }
      }
   }

   if (key_found)
   {
      free(lines[key_line]);
      char new_line[MAX_LINE_LENGTH];
      pgexporter_snprintf(new_line, sizeof(new_line), "%s = %s\n", key, value);
      lines[key_line] = strdup(new_line);
   }
   else if (section_found)
   {
      if (line_count >= MAX_LINES)
      {
         warnx("Too many lines");
         goto error;
      }

      int insert_at = section_end;
      for (int i = section_end - 1; i > section_start; i--)
      {
         char check_temp[MAX_LINE_LENGTH];
         memset(check_temp, 0, sizeof(check_temp));
         memcpy(check_temp, lines[i], MIN(sizeof(check_temp) - 1, strlen(lines[i])));
         char* ct = trim(check_temp);
         if (ct[0] != '\0')
         {
            insert_at = i + 1;
            break;
         }
      }

      for (int i = line_count; i > insert_at; i--)
      {
         lines[i] = lines[i - 1];
      }
      char new_line[MAX_LINE_LENGTH];
      pgexporter_snprintf(new_line, sizeof(new_line), "%s = %s\n", key, value);
      lines[insert_at] = strdup(new_line);
      line_count++;
   }
   else
   {
      if (line_count + 2 >= MAX_LINES)
      {
         warnx("Too many lines");
         goto error;
      }

      if (line_count > 0 && strlen(lines[line_count - 1]) > 0 && lines[line_count - 1][strlen(lines[line_count - 1]) - 1] == '\n')
      {
         char last_line[MAX_LINE_LENGTH];
         strcpy(last_line, lines[line_count - 1]);
         if (trim(last_line)[0] != '\0')
         {
            lines[line_count++] = strdup("\n");
         }
      }

      char new_section[MISC_LENGTH];
      pgexporter_snprintf(new_section, sizeof(new_section), "[%s]\n", section);
      lines[line_count++] = strdup(new_section);

      char new_line[MAX_LINE_LENGTH];
      pgexporter_snprintf(new_line, sizeof(new_line), "%s = %s\n", key, value);
      lines[line_count++] = strdup(new_line);
   }

   /* Write back to file */
   char tmp_path[MAX_PATH];
   pgexporter_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);
   file = fopen(tmp_path, "w");
   if (file == NULL)
   {
      warn("Could not open temp file");
      goto error;
   }

   for (int i = 0; i < line_count; i++)
   {
      fprintf(file, "%s", lines[i]);
      free(lines[i]);
   }

   fflush(file);
#ifndef _WIN32
   fsync(fileno(file));
#endif
   fclose(file);
   file = NULL;

   chmod(tmp_path, S_IRUSR | S_IWUSR);
   if (rename(tmp_path, file_path) != 0)
   {
      warn("Rename failed");
      unlink(tmp_path);
      goto error;
   }

   return 0;

error:
   for (int i = 0; i < line_count; i++)
   {
      free(lines[i]);
   }
   if (file != NULL)
   {
      fclose(file);
      unlink(tmp_path);
   }
   return 1;
}

/**
 * Delete a configuration key or section from a file.
 * @param file_path The config file path
 * @param section The section name
 * @param key The key name (if NULL, deletes entire section)
 * @return 0 upon success, otherwise 1
 */
static int
config_del(const char* file_path, const char* section, const char* key)
{
   FILE* file = NULL;
   char* lines[MAX_LINES];
   int line_count = 0;
   char line_buf[MAX_LINE_LENGTH];
   char section_header[MISC_LENGTH];
   int section_start = -1;
   int section_end = -1;
   bool section_found = false;

   memset(lines, 0, sizeof(lines));
   pgexporter_snprintf(section_header, sizeof(section_header), "[%s]", section);

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      warnx("Could not open file: %s", file_path);
      return 1;
   }

   while (fgets(line_buf, sizeof(line_buf), file) != NULL && line_count < MAX_LINES)
   {
      lines[line_count] = strdup(line_buf);
      line_count++;
   }
   fclose(file);

   for (int i = 0; i < line_count; i++)
   {
      char temp[MAX_LINE_LENGTH];
      strcpy(temp, lines[i]);
      char* trimmed = trim(temp);
      if (trimmed[0] == '[')
      {
         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            *(bracket_end + 1) = '\0';
         }
         if (!strcmp(trimmed, section_header))
         {
            section_found = true;
            section_start = i;
            section_end = line_count;
         }
         else if (section_found && section_end == line_count)
         {
            section_end = i;
         }
      }
   }

   if (!section_found)
   {
      warnx("Section [%s] not found", section);
      goto error;
   }

   char tmp_path[MAX_PATH];
   pgexporter_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);
   file = fopen(tmp_path, "w");
   if (file == NULL)
   {
      warn("Could not open temp file");
      goto error;
   }

   for (int i = 0; i < line_count; i++)
   {
      bool skip = false;
      if (key == NULL)
      {
         if (i >= section_start && i < section_end)
         {
            skip = true;
         }
      }
      else
      {
         if (i > section_start && i < section_end)
         {
            char temp[MAX_LINE_LENGTH];
            strcpy(temp, lines[i]);
            char* kt = trim(temp);
            if (kt[0] != '#' && kt[0] != ';' && kt[0] != '\0')
            {
               char* eq = strchr(kt, '=');
               if (eq != NULL)
               {
                  *eq = '\0';
                  if (!strcmp(trim(kt), key))
                  {
                     skip = true;
                  }
               }
            }
         }
      }

      if (!skip)
      {
         fprintf(file, "%s", lines[i]);
      }
      free(lines[i]);
   }

   fflush(file);
#ifndef _WIN32
   fsync(fileno(file));
#endif
   fclose(file);
   file = NULL;

   chmod(tmp_path, S_IRUSR | S_IWUSR);
   if (rename(tmp_path, file_path) != 0)
   {
      warn("Rename failed");
      unlink(tmp_path);
      goto error;
   }

   return 0;

error:
   for (int i = 0; i < line_count; i++)
   {
      free(lines[i]);
   }
   return 1;
}

/**
 * List sections or keys in a section.
 * @param file_path The config file path
 * @param section The section name (if NULL, lists all sections)
 * @return 0 upon success, otherwise 1
 */
static int
config_ls(const char* file_path, const char* section)
{
   FILE* file = NULL;
   char line[MAX_LINE_LENGTH];
   bool in_section = false;
   char section_header[MISC_LENGTH];

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      warnx("Could not open file: %s", file_path);
      return 1;
   }

   if (section != NULL)
   {
      pgexporter_snprintf(section_header, sizeof(section_header), "[%s]", section);
   }

   while (fgets(line, sizeof(line), file) != NULL)
   {
      char* trimmed = trim(line);
      if (trimmed[0] == '[')
      {
         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            *(bracket_end + 1) = '\0';
         }

         if (section == NULL)
         {
            *bracket_end = '\0';
            printf("%s\n", trimmed + 1);
            continue;
         }
         else
         {
            if (!strcmp(trimmed, section_header))
            {
               in_section = true;
            }
            else if (in_section)
            {
               break;
            }
         }
         continue;
      }

      if (section != NULL && in_section)
      {
         char* eq = strchr(trimmed, '=');
         if (eq != NULL)
         {
            *eq = '\0';
            printf("%s\n", trim(trimmed));
         }
      }
   }

   fclose(file);
   return 0;
}

int
main(int argc, char** argv)
{
   char* output_path = NULL;
   bool quiet = false;
   bool force = false;
   int c;
   int option_index = 0;
   size_t command_count = sizeof(command_table) / sizeof(struct pgexporter_command);
   struct pgexporter_parsed_command parsed = {.cmd = NULL, .args = {0}};
   enum config_target target = TARGET_MAIN;

   setbuf(stdout, NULL);

   while (1)
   {
      static struct option long_options[] =
         {
            {"output", required_argument, 0, 'o'},
            {"quiet", no_argument, 0, 'q'},
            {"force", no_argument, 0, 'F'},
            {"version", no_argument, 0, 'V'},
            {"help", no_argument, 0, '?'},
         };

      c = getopt_long(argc, argv, "o:qFV?", long_options, &option_index);
      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'o':
            output_path = optarg;
            break;
         case 'q':
            quiet = true;
            break;
         case 'F':
            force = true;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(0);
            break;
      }
   }

   if (argc > optind)
   {
      if (!strcmp(argv[optind], "main"))
      {
         target = TARGET_MAIN;
         optind++;
      }
      else if (!strcmp(argv[optind], "cli"))
      {
         target = TARGET_CLI;
         optind++;
      }
   }

   if (getuid() == 0)
   {
      errx(1, "pgexporter-config: Using the root account is not allowed");
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   if (!parse_command(argc, argv, optind, &parsed, command_table, command_count))
   {
      usage();
      goto error;
   }

   if (parsed.cmd->action == ACTION_CONFIG_INIT)
   {
      if (output_path == NULL)
      {
         if (target == TARGET_CLI)
         {
            output_path = "pgexporter-cli.conf";
         }
         else
         {
            output_path = "pgexporter.conf";
         }
      }
      if (config_init(output_path, quiet, force, target))
      {
         errx(1, "Error generating configuration");
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_GET)
   {
      if (config_get(parsed.args[0], parsed.args[1], parsed.args[2]))
      {
         exit(1);
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_SET)
   {
      if (config_set(parsed.args[0], parsed.args[1], parsed.args[2], parsed.args[3]))
      {
         errx(1, "Error setting configuration value");
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_DEL)
   {
      if (config_del(parsed.args[0], parsed.args[1], parsed.args[2]))
      {
         errx(1, "Error deleting configuration");
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_LS)
   {
      if (config_ls(parsed.args[0], parsed.args[1]))
      {
         exit(1);
      }
   }

   exit(0);

error:
   exit(1);
}
