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
#include <extension.h>
#include <logging.h>
#include <utils.h>
#include <yaml_configuration.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool extension_in_list(const char* extension_name, const char* extensions_list);

int
pgexporter_setup_extensions_path(struct configuration* config, const char* argv0, char** bin_path)
{
   char* local_bin_path = NULL;

   errno = 0;

   local_bin_path = realpath(argv0, NULL);

   if (local_bin_path != NULL && strstr(local_bin_path, "/build/src/") != NULL)
   {
      /* Development build - use build/extensions directory */
      char temp_path[MAX_PATH];
      pgexporter_snprintf(temp_path, MAX_PATH, "%s", local_bin_path);

      /* Remove the executable name to get directory */
      char* last_slash = strrchr(temp_path, '/');
      if (last_slash)
      {
         *last_slash = '\0';
      }

      int result = pgexporter_snprintf(config->extensions_path, MAX_PATH, "%s/../extensions", temp_path);
      if (result >= MAX_PATH)
      {
         pgexporter_log_error("Extensions path truncated");
         goto error;
      }

      pgexporter_log_debug("Development build: extensions at %s", config->extensions_path);
   }
   else
   {
      /* Standard installation: try multiple standard locations */
      const char* standard_paths[] = {
         "/usr/local/share/pgexporter/extensions", /* Debian based systems */
         "/usr/share/pgexporter/extensions"        /* RPM spec */
      };
      int num_paths = sizeof(standard_paths) / sizeof(standard_paths[0]);
      bool found = false;

      for (int i = 0; i < num_paths; i++)
      {
         pgexporter_snprintf(config->extensions_path, MAX_PATH, "%s", standard_paths[i]);

         /* Check if this path exists and is readable */
         if (access(config->extensions_path, R_OK) == 0)
         {
            found = true;
            pgexporter_log_debug("Standard installation: extensions at %s", config->extensions_path);
            break;
         }
         else
         {
            pgexporter_log_debug("Extensions path not found: %s", config->extensions_path);
         }
      }

      if (!found)
      {
         pgexporter_log_error("No extensions directory found");
         goto error;
      }
   }

   *bin_path = local_bin_path;
   return 0;

error:
   if (local_bin_path != NULL)
   {
      free(local_bin_path);
      local_bin_path = NULL;
   }
   return 1;
}

int
pgexporter_parse_extension_version(char* version_str, struct version* version)
{
   char* str_copy = NULL;
   char* token = NULL;
   char* saveptr = NULL;
   char* dash_pos = NULL;
   int part = 0;

   if (!version_str || !version)
   {
      pgexporter_log_error("Invalid parameters for version parsing");
      goto error;
   }

   version->major = -1;
   version->minor = -1;
   version->patch = -1;

   str_copy = strdup(version_str);
   if (!str_copy)
   {
      pgexporter_log_error("Failed to allocate memory for version string copy");
      goto error;
   }

   /* remove any suffix (e.g., "1.2.1-rc.2" -> "1.2.1") */
   dash_pos = strchr(str_copy, '-');
   if (dash_pos)
   {
      *dash_pos = '\0';
   }

   token = strtok_r(str_copy, ".", &saveptr);
   while (token && part < 3)
   {
      /* Skip empty tokens (e.g., from "1..2") */
      if (strlen(token) == 0)
      {
         token = strtok_r(NULL, ".", &saveptr);
         continue;
      }

      /* parse only the numeric part of the token */
      char* endptr = NULL;
      long value = strtol(token, &endptr, 10);

      /* conversion was successful and value is non-negative */
      if (endptr == token || value < 0)
      {
         pgexporter_log_error("Invalid version component: %s", token);
         goto error;
      }

      switch (part)
      {
         case 0:
            version->major = (int)value;
            break;
         case 1:
            version->minor = (int)value;
            break;
         case 2:
            version->patch = (int)value;
            break;
      }

      part++;
      token = strtok_r(NULL, ".", &saveptr);
   }

   // Must have at least major version
   if (version->major == -1)
   {
      pgexporter_log_error("No major version found in version string: %s", version_str);
      goto error;
   }

   free(str_copy);
   str_copy = NULL;
   return 0;

error:
   free(str_copy);
   str_copy = NULL;
   return 1;
}

int
pgexporter_compare_extension_versions(struct version* v1, struct version* v2)
{
   if (!v1 || !v2)
   {
      return VERSION_ERROR;
   }

   /* major version */
   if (v1->major > v2->major)
   {
      return VERSION_GREATER;
   }
   if (v1->major < v2->major)
   {
      return VERSION_LESS;
   }

   /* minor version */
   int minor1 = (v1->minor == -1) ? 0 : v1->minor;
   int minor2 = (v2->minor == -1) ? 0 : v2->minor;

   if (minor1 > minor2)
   {
      return VERSION_GREATER;
   }
   if (minor1 < minor2)
   {
      return VERSION_LESS;
   }

   /* patch */
   int patch1 = (v1->patch == -1) ? 0 : v1->patch;
   int patch2 = (v2->patch == -1) ? 0 : v2->patch;

   if (patch1 > patch2)
   {
      return VERSION_GREATER;
   }
   if (patch1 < patch2)
   {
      return VERSION_LESS;
   }

   return VERSION_EQUAL;
}

int
pgexporter_version_to_string(struct version* version, char* buffer, size_t buffer_size)
{
   if (!version || !buffer || buffer_size == 0)
   {
      pgexporter_log_error("Invalid parameters for version to string conversion");
      goto error;
   }

   int major = (version->major == -1) ? 0 : version->major;
   int minor = (version->minor == -1) ? 0 : version->minor;
   int patch = (version->patch == -1) ? 0 : version->patch;

   int result;

   if (version->patch != -1)
   {
      // version eg: "1.2.3" major.minor.patch
      result = pgexporter_snprintf(buffer, buffer_size, "%d.%d.%d", major, minor, patch);
   }
   else if (version->minor != -1)
   {
      // Major.minor: "1.2"
      result = pgexporter_snprintf(buffer, buffer_size, "%d.%d", major, minor);
   }
   else
   {
      // Major only: "1"
      result = pgexporter_snprintf(buffer, buffer_size, "%d", major);
   }

   if (result >= buffer_size)
   {
      pgexporter_log_error("Buffer too small for version string");
      goto error;
   }

   return 0;

error:
   return 1;
}

int
pgexporter_load_extension_yamls(struct configuration* config)
{
   if (!config)
   {
      pgexporter_log_debug("Invalid configuration for extension YAML loading");
      goto error;
   }

   pgexporter_log_debug("Loading extension YAMLs for %d servers", config->number_of_servers);

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd == -1)
      {
         pgexporter_log_debug("Server %s is not connected, skipping extension YAML loading",
                              config->servers[server].name);
         continue;
      }

      pgexporter_log_debug("Loading extension YAMLs for server %s with %d extensions",
                           config->servers[server].name,
                           config->servers[server].number_of_extensions);

      for (int i = 0; i < config->servers[server].number_of_extensions; i++)
      {
         if (config->servers[server].extensions[i].enabled)
         {
            pgexporter_log_debug("Attempting to load YAML for extension: %s",
                                 config->servers[server].extensions[i].name);

            if (pgexporter_load_single_extension_yaml(config->extensions_path,
                                                      config->servers[server].extensions[i].name,
                                                      config))
            {
               pgexporter_log_debug("Failed to load YAML for extension: %s",
                                    config->servers[server].extensions[i].name);
            }
         }
         else
         {
            pgexporter_log_info("Extension %s not enabled for metrics on: %s",
                                config->servers[server].extensions[i].name, config->servers[server].name);
         }
      }
   }

   return 0;

error:
   return 1;
}

static bool
extension_in_list(const char* extension_name, const char* extensions_list)
{
   if (extensions_list == NULL || strlen(extensions_list) == 0)
   {
      return false;
   }

   char* list_copy = strdup(extensions_list);
   char* token = strtok(list_copy, ",");
   bool found = false;

   while (token != NULL)
   {
      // Remove leading/trailing whitespace from token
      while (*token == ' ' || *token == '\t')
         token++;

      char* end = token + strlen(token) - 1;
      while (end > token && (*end == ' ' || *end == '\t'))
         end--;
      *(end + 1) = '\0';

      if (!strcmp(token, extension_name))
      {
         found = true;
         break;
      }

      token = strtok(NULL, ",");
   }

   free(list_copy);
   return found;
}

bool
pgexporter_extension_is_enabled(struct configuration* config, int server, char* extension_name)
{
   char* extensions_list = NULL;

   // Check server-specific config first
   if (strlen(config->servers[server].extensions_config) > 0)
   {
      extensions_list = config->servers[server].extensions_config;
   }
   // Fall back to global config
   else if (strlen(config->global_extensions) > 0)
   {
      extensions_list = config->global_extensions;
   }

   // If no config specified, enable by default
   if (extensions_list == NULL)
   {
      return true;
   }

   // If config specified, check if this extension is in the list
   return extension_in_list(extension_name, extensions_list);
}
