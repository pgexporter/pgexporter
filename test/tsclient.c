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
 *
 */

/* pgexporter */
#include <pgexporter.h>
#include <configuration.h>
#include <extension.h>
#include <http.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <queries.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>

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

char project_directory[BUFFER_SIZE];

static int check_output_outcome(int socket);
static int get_connection();
static char* get_configuration_path();

int
pgexporter_tsclient_init(char* base_dir)
{
    struct configuration* config = NULL;
    int ret;
    size_t size;
    char* configuration_path = NULL;
    char* users_path = NULL;

    memset(project_directory, 0, sizeof(project_directory));
    memcpy(project_directory, base_dir, strlen(base_dir));

    configuration_path = get_configuration_path();

    pgexporter_memory_init();

    size = sizeof(struct configuration);
    if (pgexporter_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
    {
        goto error;
    }

    pgexporter_init_configuration(shmem);
    config = (struct configuration*)shmem;

    // Read configuration file
    if (configuration_path != NULL)
    {
        if (pgexporter_read_configuration(shmem, configuration_path))
        {
            goto error;
        }
    }
    else
    {
        goto error;
    }

    users_path = (char*)malloc(strlen(project_directory) + strlen("/pgexporter-testsuite/conf/pgexporter_users.conf") + 1);
    strcpy(users_path, project_directory);
    strcat(users_path, "/pgexporter-testsuite/conf/pgexporter_users.conf");
    
    ret = pgexporter_read_users_configuration(shmem, users_path);
    if (ret != 0)
    {
        printf("Failed to read users configuration: %s (ret=%d)\n", users_path, ret);
        goto error;
    }

    // Initialize logging (from main.c around line 570)
    if (pgexporter_init_logging())
    {
        goto error;
    }

    if (pgexporter_start_logging())
    {
        goto error;
    }

    // Validate configurations (from main.c around line 580)
    if (pgexporter_validate_configuration(shmem))
    {
        printf("Configuration validation failed\n");
        goto error;
    }

    if (pgexporter_validate_users_configuration(shmem))
    {
        printf("Users configuration validation failed\n");
        goto error;
    }

    free(configuration_path);
    free(users_path);
    return 0;
    
error:
    free(configuration_path);
    free(users_path);
    return 1;
}

int
pgexporter_tsclient_destroy()
{
    size_t size;

    pgexporter_stop_logging();
    
    size = sizeof(struct configuration);
    pgexporter_destroy_shared_memory(shmem, size);
    
    pgexporter_memory_destroy();
    
    return 0;
}

int
pgexporter_tsclient_execute_ping()
{
    int socket = -1;
    
    socket = get_connection();
    
    if (!pgexporter_socket_isvalid(socket))
    {
        goto error;
    }
    
    if (pgexporter_management_request_ping(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
    {
        goto error;
    }

    if (check_output_outcome(socket))
    {
        goto error;
    }

    pgexporter_disconnect(socket);
    return 0;
    
error:
    pgexporter_disconnect(socket);
    return 1;
}

int
pgexporter_tsclient_execute_shutdown()
{
    int socket = -1;
    
    socket = get_connection();
    
    if (!pgexporter_socket_isvalid(socket))
    {
        goto error;
    }
    
    if (pgexporter_management_request_shutdown(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
    {
        goto error;
    }

    if (check_output_outcome(socket))
    {
        goto error;
    }

    pgexporter_disconnect(socket);
    return 0;
    
error:
    pgexporter_disconnect(socket);
    return 1;
}

int
pgexporter_tsclient_execute_status()
{
    int socket = -1;
    
    socket = get_connection();
    
    if (!pgexporter_socket_isvalid(socket))
    {
        goto error;
    }
    
    if (pgexporter_management_request_status(NULL, socket, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
    {
        goto error;
    }

    if (check_output_outcome(socket))
    {
        goto error;
    }

    pgexporter_disconnect(socket);
    return 0;
    
error:
    pgexporter_disconnect(socket);
    return 1;
}

int
pgexporter_tsclient_test_db_connection()
{
    struct configuration* config;
    int connected_servers = 0;

    config = (struct configuration*)shmem;

    printf("=== Testing database connections ===\n");
    printf("Number of configured servers: %d\n", config->number_of_servers);
    printf("Number of configured users: %d\n", config->number_of_users);
    
    // Print debug info
    for (int i = 0; i < config->number_of_servers; i++)
    {
        printf("Server %d: name='%s', host='%s', port=%d, user='%s'\n", 
               i, config->servers[i].name, config->servers[i].host, 
               config->servers[i].port, config->servers[i].username);
    }
    
    printf("Calling pgexporter_open_connections()...\n");
    pgexporter_open_connections();
    printf("pgexporter_open_connections() completed\n");

    // Check results (from main.c around line 750)
    for (int i = 0; i < config->number_of_servers; i++)
    {
        printf("Server %s: fd=%d, state=%d, version=%d.%d\n", 
               config->servers[i].name, config->servers[i].fd, config->servers[i].state,
               config->servers[i].version, config->servers[i].minor_version);
        
        if (config->servers[i].fd != -1)
        {
            connected_servers++;
            printf("  -> Connected successfully\n");
        }
        else
        {
            printf("  -> Connection failed\n");
        }
    }

    printf("Total connected servers: %d/%d\n", connected_servers, config->number_of_servers);
    
    pgexporter_close_connections();
    return (connected_servers > 0) ? 0 : 1;
}

int
pgexporter_tsclient_test_version_query()
{
    struct configuration* config;
    struct query* query = NULL;
    struct tuple* current = NULL;
    int ret = 1;
    int server_tested = 0;

    config = (struct configuration*)shmem;

    printf("=== Testing PostgreSQL version query ===\n");
    
    printf("Opening connections...\n");
    pgexporter_open_connections();

    // Test version query on first available server (from main.c pattern)
    for (int i = 0; i < config->number_of_servers && !server_tested; i++)
    {
        if (config->servers[i].fd != -1)
        {
            printf("Testing version query on server %s (fd=%d)...\n", 
                   config->servers[i].name, config->servers[i].fd);
            
            if (pgexporter_query_version(i, &query) == 0 && query != NULL)
            {
                current = query->tuples;
                if (current != NULL)
                {
                    printf("PostgreSQL Version: %s.%s\n", 
                           pgexporter_get_column(0, current),
                           pgexporter_get_column(1, current));
                    ret = 0;
                    server_tested = 1;
                }
                else
                {
                    printf("No version data returned from query\n");
                }
                pgexporter_free_query(query);
            }
            else
            {
                printf("Failed to execute version query\n");
            }
        }
        else
        {
            printf("Server %s not connected (fd=%d)\n", config->servers[i].name, config->servers[i].fd);
        }
    }

    if (!server_tested)
    {
        printf("No servers available for version query test\n");
    }

    pgexporter_close_connections();
    return ret;
}

int
pgexporter_tsclient_test_extension_path()
{
    struct configuration* config;
    char* bin_path = NULL;
    int ret = 1;

    config = (struct configuration*)shmem;

    printf("=== Testing extension path setup ===\n");
    
    // Use real program path from project directory (from main.c around line 650)
    char* program_path = NULL;
    program_path = pgexporter_append(program_path, project_directory);
    program_path = pgexporter_append(program_path, "/src/pgexporter");
    
    printf("Using program path: %s\n", program_path);
    
    if (pgexporter_setup_extensions_path(config, program_path, &bin_path) == 0)
    {
        if (bin_path != NULL && strlen(bin_path) > 0)
        {
            printf("Extension path setup successful: %s\n", bin_path);
            ret = 0;
        }
        else
        {
            printf("Extension path setup returned success but path is empty\n");
        }
    }
    else
    {
        printf("Extension path setup failed\n");
    }

    // Print paths for debugging
    if (bin_path != NULL)
    {
        printf("Final extension path: %s\n", bin_path);
        free(bin_path);
    }
    else
    {
        printf("Extension path is NULL\n");
    }
    
    printf("Configured extensions path: %s\n", config->extensions_path);

    free(program_path);
    return ret;
}

static int 
check_output_outcome(int socket)
{
    struct json* read = NULL;
    struct json* outcome = NULL;

    if (pgexporter_management_read_json(NULL, socket, NULL, NULL, &read))
    {
        goto error;
    }
    
    if (!pgexporter_json_contains_key(read, MANAGEMENT_CATEGORY_OUTCOME))
    {
        goto error;
    }

    outcome = (struct json*)pgexporter_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
    if (!pgexporter_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS) || !(bool)pgexporter_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
    {
        goto error;
    }

    pgexporter_json_destroy(read);
    return 0;
    
error:
    pgexporter_json_destroy(read);
    return 1;
}

static int
get_connection()
{
    int socket = -1;
    struct configuration* config;

    config = (struct configuration*)shmem;
    
    if (pgexporter_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
    {
        return -1;
    }
    
    return socket;
}

static char*
get_configuration_path()
{
   char* configuration_path = NULL;
   int project_directory_length = strlen(project_directory);
   int configuration_trail_length = strlen(PGEXPORTER_CONFIGURATION_TRAIL);

   configuration_path = (char*)calloc(project_directory_length + configuration_trail_length + 1, sizeof(char));

   memcpy(configuration_path, project_directory, project_directory_length);
   memcpy(configuration_path + project_directory_length, PGEXPORTER_CONFIGURATION_TRAIL, configuration_trail_length);

   return configuration_path;
}

int
pgexporter_tsclient_test_http_metrics()
{
    struct http* connection = NULL;
    struct http_request* request = NULL;
    struct http_response* response = NULL;
    struct configuration* config;
    char* response_body = NULL;
    size_t response_size = 0;
    char* line = NULL;
    char* saveptr = NULL;
    char* last_line = NULL;
    bool found_first_metric = false;
    bool found_version_metric = false;
    int postgresql_version = 0;
    int ret = 1;

    config = (struct configuration*)shmem;

    printf("=== Testing HTTP /metrics endpoint ===\n");
    printf("Attempting to connect to localhost:%d\n", config->metrics);

    if (pgexporter_http_create("localhost", config->metrics, false, &connection))
    {
        printf("ERROR: Failed to connect to HTTP endpoint localhost:%d\n", config->metrics);
        goto error;
    }
    printf("Successfully connected to HTTP endpoint\n");

    if (pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/metrics", &request))
    {
        printf("ERROR: Failed to create HTTP request\n");
        goto error;
    }

    printf("Executing HTTP GET /metrics request\n");
    if (pgexporter_http_invoke(connection, request, &response))
    {
        printf("ERROR: Failed to execute HTTP GET /metrics\n");
        goto error;
    }
    printf("HTTP GET request completed\n");

    if (response->payload.data == NULL)
    {
        printf("ERROR: HTTP response body is NULL\n");
        goto error;
    }
    printf("HTTP response body received\n");

    response_body = strdup((char*)response->payload.data);
    if (response_body == NULL)
    {
        printf("ERROR: Failed to duplicate response body\n");
        goto error;
    }

    response_size = strlen(response_body);
    printf("Response size: %zu bytes\n", response_size);

    if (response_size == 0)
    {
        printf("ERROR: Response size is 0\n");
        goto error;
    }

    printf("Parsing response for core metrics\n");
    line = strtok_r(response_body, "\n", &saveptr);
    while (line != NULL)
    {
        if (pgexporter_starts_with(line, "pgexporter_state 1"))
        {
            found_first_metric = true;
            printf("Found first core metric: %s\n", line);
        }

        if (pgexporter_starts_with(line, "pgexporter_postgresql_version"))
        {
            char* version_start = strstr(line, "version=\"");
            if (version_start != NULL)
            {
                version_start += 9;
                char* version_end = strchr(version_start, '"');
                if (version_end != NULL)
                {
                    *version_end = '\0';
                    postgresql_version = atoi(version_start);
                    found_version_metric = true;
                    printf("Found PostgreSQL version metric: version=%d\n", postgresql_version);
                    *version_end = '"';
                }
            }
        }

        last_line = line;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (last_line != NULL)
    {
        printf("Last line of response: %s\n", last_line);
    }

    printf("Validating metrics\n");
    if (!found_first_metric)
    {
        printf("ERROR: Failed to find first core metric (pgexporter_state)\n");
        goto error;
    }

    if (!found_version_metric)
    {
        printf("ERROR: Failed to find PostgreSQL version metric\n");
        goto error;
    }

    if (postgresql_version != 17)
    {
        printf("ERROR: Expected PostgreSQL version 17, got %d\n", postgresql_version);
        goto error;
    }

    printf("HTTP metrics test completed successfully\n");
    ret = 0;

error:
    if (connection != NULL)
    {
        pgexporter_http_destroy(connection);
    }

    if (request != NULL)
    {
        pgexporter_http_request_destroy(request);
    }

    if (response != NULL)
    {
        pgexporter_http_response_destroy(response);
    }

    free(response_body);
    return ret;
}

int
pgexporter_tsclient_test_bridge_endpoint()
{
   printf("DEBUG: Bridge test function entry\n");
   fflush(stdout);

   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct configuration* config;
   char* response_body = NULL;
   size_t response_size = 0;
   char* line = NULL;
   char* saveptr = NULL;
   char* last_line = NULL;
   bool found_first_metric = false;
   bool found_version_metric = false;
   int postgresql_version = 0;
   int ret = 1;

   printf("DEBUG: Getting config pointer\n");
   fflush(stdout);
   
   config = (struct configuration*)shmem;
   
   if (config == NULL)
   {
       printf("ERROR: config is NULL\n");
       return 1;
   }
   
   printf("DEBUG: Config loaded successfully\n");
   fflush(stdout);

   printf("=== Testing bridge endpoint ===\n");
   printf("Bridge port from config: %d\n", config->bridge);
   
   if (config->bridge == 0)
   {
       printf("ERROR: Bridge port is 0, bridge not configured\n");
       return 1;
   }
   
   printf("Attempting to connect to localhost:%d\n", config->bridge);

   if (pgexporter_http_create("localhost", config->bridge, false, &connection))
   {
       printf("ERROR: Failed to connect to bridge endpoint localhost:%d\n", config->bridge);
       printf("Is bridge endpoint running and configured?\n");
       goto error;
   }
   printf("Successfully connected to bridge endpoint\n");

   if (pgexporter_http_request_create(PGEXPORTER_HTTP_GET, "/metrics", &request))
   {
       printf("ERROR: Failed to create HTTP request\n");
       goto error;
   }

   printf("Executing HTTP GET /metrics request\n");
   if (pgexporter_http_invoke(connection, request, &response))
   {
       printf("ERROR: Failed to execute HTTP GET /metrics\n");
       goto error;
   }
   printf("HTTP GET request completed\n");

   if (response->payload.data == NULL)
   {
       printf("ERROR: HTTP response body is NULL\n");
       goto error;
   }
   printf("HTTP response body received\n");

   response_body = strdup((char*)response->payload.data);
   if (response_body == NULL)
   {
       printf("ERROR: Failed to duplicate response body\n");
       goto error;
   }

   response_size = strlen(response_body);
   printf("Response size: %zu bytes\n", response_size);

   if (response_size == 0)
   {
       printf("ERROR: Response size is 0\n");
       goto error;
   }

   printf("First 200 characters of response: %.200s\n", response_body);

   printf("Parsing response for core metrics\n");
   line = strtok_r(response_body, "\n", &saveptr);
   while (line != NULL)
   {
       if (pgexporter_starts_with(line, "pgexporter_state{endpoint="))
       {
           found_first_metric = true;
           printf("Found first core metric: %s\n", line);
       }

       if (pgexporter_starts_with(line, "pgexporter_postgresql_version{endpoint="))
       {
           char* version_start = strstr(line, "version=\"");
           if (version_start != NULL)
           {
               version_start += 9;
               char* version_end = strchr(version_start, '"');
               if (version_end != NULL)
               {
                   *version_end = '\0';
                   postgresql_version = atoi(version_start);
                   found_version_metric = true;
                   printf("Found PostgreSQL version metric: version=%d\n", postgresql_version);
                   *version_end = '"';
               }
           }
       }

       last_line = line;
       line = strtok_r(NULL, "\n", &saveptr);
   }

   if (last_line != NULL)
   {
       printf("Last line of response: %s\n", last_line);
   }

   printf("Validating metrics\n");
   if (!found_first_metric)
   {
       printf("ERROR: Failed to find first core metric (pgexporter_state with endpoint label)\n");
       goto error;
   }
   printf("First core metric validation passed\n");

   if (!found_version_metric)
   {
       printf("ERROR: Failed to find PostgreSQL version metric with endpoint label\n");
       goto error;
   }
   printf("PostgreSQL version metric found\n");

   if (postgresql_version != 17)
   {
       printf("ERROR: Expected PostgreSQL version 17, got %d\n", postgresql_version);
       goto error;
   }
   printf("PostgreSQL version validation passed\n");

   printf("Bridge endpoint test completed successfully\n");
   ret = 0;

error:
   printf("DEBUG: Bridge test cleanup\n");
   fflush(stdout);

   if (connection != NULL)
   {
       pgexporter_http_destroy(connection);
   }

   if (request != NULL)
   {
       pgexporter_http_request_destroy(request);
   }

   if (response != NULL)
   {
       pgexporter_http_response_destroy(response);
   }

   free(response_body);

   printf("DEBUG: Bridge test returning %d\n", ret);
   fflush(stdout);

   return ret;
}

int
pgexporter_tsclient_test_extension_detection()
{
    struct configuration* config;
    bool found_pg_stat_statements = false;
    int ret = 1;

    config = (struct configuration*)shmem;

    printf("=== Testing extension detection ===\n");
    printf("Number of configured servers: %d\n", config->number_of_servers);

    if (config->number_of_servers == 0)
    {
        printf("ERROR: No servers configured\n");
        goto error;
    }

    printf("Checking server 0 for extensions\n");
    printf("Number of extensions on server 0: %d\n", config->servers[0].number_of_extensions);

    for (int i = 0; i < config->servers[0].number_of_extensions; i++)
    {
        printf("Extension %d: name='%s', enabled=%s\n", 
               i, 
               config->servers[0].extensions[i].name,
               config->servers[0].extensions[i].enabled ? "true" : "false");
        
        if (strcmp(config->servers[0].extensions[i].name, "pg_stat_statements") == 0)
        {
            found_pg_stat_statements = true;
            printf("Found pg_stat_statements extension\n");
            
            if (config->servers[0].extensions[i].enabled)
            {
                printf("pg_stat_statements is enabled\n");
            }
            else
            {
                printf("WARNING: pg_stat_statements is not enabled\n");
            }
        }
    }

    if (!found_pg_stat_statements)
    {
        printf("ERROR: pg_stat_statements extension not found\n");
        goto error;
    }

    printf("Extension detection test completed successfully\n");
    ret = 0;

error:
    return ret;
}