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
#include <console.h>
#include <http.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <prometheus_client.h>
#include <management.h>
#include <message.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

struct prefix_count
{
   char* prefix;
   int count;
};

struct category_candidate
{
   char* prefix;
   int count;
   int depth;
   double score;
};

/* Constants for category selection */
#define MIN_GROUP_SIZE 3
#define MAX_DEPTH      4

/* Page routing constants */
#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_API     2
#define BAD_REQUEST  3
#define PAGE_DETAIL  4

/* Forward declarations */
static int build_categories_from_bridge(struct prometheus_bridge* bridge, struct console_page* console);
static int record_prefix_counts(const char* metric_name, struct prefix_count** counts, int* size, int* capacity);
static int count_prefix_depth(const char* prefix);
static int build_category_candidates(struct prefix_count* counts, int size, struct category_candidate** candidates, int* candidate_count);
static int compare_candidates_by_score(const void* a, const void* b);
static char** select_global_categories(struct category_candidate* candidates, int candidate_count, int* selected_count);
static char* find_best_category(const char* metric_name, char** categories, int category_count);
static char* extract_category_prefix(char* metric_name);
static char* fallback_category_from_last_underscore(char* metric_name);
static struct console_category* find_or_create_category(struct console_page* console, char* category_name);
static int add_metric_to_category(struct console_category* category, struct console_metric* metric);
static struct console_metric* create_metric_from_prometheus(struct prometheus_metric* prom_metric, const char* display_name);
static char* format_label_value(char* key, char* value);
static char* generate_metrics_table(struct console_category* category, int cat_index);
static char* generate_category_tabs(struct console_page* console);
static int resolve_page(struct message* msg);
static int detail_page(SSL* client_ssl, int client_fd);
static int badrequest_page(SSL* client_ssl, int client_fd);
static int home_page(SSL* client_ssl, int client_fd);
static int api_page(SSL* client_ssl, int client_fd);

int pgexporter_console_detail_category_index = -1;

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;
   static int detail_cat = -1;

   if (msg->length < 3 || strncmp((char*)msg->data, "GET", 3) != 0)
   {
      return BAD_REQUEST;
   }

   index = 4;
   from = (char*)msg->data + index;

   while (pgexporter_read_byte(msg->data + index) != ' ')
   {
      index++;
   }

   pgexporter_write_byte(msg->data + index, '\0');

   if (strcmp(from, "/") == 0 || strcmp(from, "/index.html") == 0)
   {
      return PAGE_HOME;
   }
   else if (strcmp(from, "/api") == 0 || strcmp(from, "/api/") == 0)
   {
      return PAGE_API;
   }
   else if (strncmp(from, "/detail", 7) == 0)
   {
      detail_cat = -1;
      char* q = strchr(from, '?');
      if (q != NULL)
      {
         char* p = q + 1;
         while (*p)
         {
            char key[64];
            char val[64];
            char* eq = strchr(p, '=');
            if (!eq)
               break;
            size_t klen = eq - p;
            if (klen >= sizeof(key))
               break;
            strncpy(key, p, klen);
            key[klen] = '\0';
            char* amp = strchr(eq + 1, '&');
            size_t vlen = amp ? (size_t)(amp - (eq + 1)) : strlen(eq + 1);
            if (vlen >= sizeof(val))
               vlen = sizeof(val) - 1;
            strncpy(val, eq + 1, vlen);
            val[vlen] = '\0';
            if (strcmp(key, "cat") == 0)
            {
               detail_cat = atoi(val);
            }
            if (!amp)
               break;
            p = amp + 1;
         }
      }
      pgexporter_console_detail_category_index = detail_cat;
      return PAGE_DETAIL;
   }

   return PAGE_UNKNOWN;
}

static int
badrequest_page(SSL* client_ssl, int client_fd)
{
   struct message msg;
   char* data = NULL;
   int status;

   memset(&msg, 0, sizeof(struct message));

   data = pgexporter_append(data, "HTTP/1.1 400 Bad Request\r\n");
   data = pgexporter_append(data, "Content-Length: 0\r\n");
   data = pgexporter_append(data, "Connection: close\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(client_ssl, client_fd, &msg);

   free(data);

   return status;
}

static int
home_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* html = NULL;
   size_t html_size = 0;
   struct message msg;
   int status = 0;
   char response_header[512];
   int header_len;

   if (pgexporter_console_init(0, "pgexporter", "pgexporter_", &console))
   {
      pgexporter_log_error("Failed to initialize console");
      return badrequest_page(client_ssl, client_fd);
   }

   if (pgexporter_console_generate_html(console, &html, &html_size))
   {
      pgexporter_log_error("Failed to generate HTML");
      pgexporter_console_destroy(console);
      return badrequest_page(client_ssl, client_fd);
   }

   memset(&msg, 0, sizeof(struct message));
   header_len = snprintf(response_header, sizeof(response_header),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         html_size);

   msg.data = response_header;
   msg.length = header_len;
   status = pgexporter_write_message(client_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_log_error("console home_page: failed to write header (status=%d, len=%d)", status, header_len);
   }

   if (status == MESSAGE_STATUS_OK && html_size > 0)
   {
      memset(&msg, 0, sizeof(struct message));
      msg.data = html;
      msg.length = html_size;
      status = pgexporter_write_message(client_ssl, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         pgexporter_log_error("console home_page: failed to write body (status=%d, len=%zu)", status, html_size);
      }
   }

   free(html);
   pgexporter_console_destroy(console);

   return status;
}

static int
api_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* json = NULL;
   size_t json_size = 0;
   struct message msg;
   int status = 0;
   char response_header[512];
   int header_len;

   if (pgexporter_console_init(0, "pgexporter", "pgexporter_", &console))
   {
      pgexporter_log_error("Failed to initialize console for API");
      return badrequest_page(client_ssl, client_fd);
   }

   if (pgexporter_console_generate_json(console, &json, &json_size))
   {
      pgexporter_log_error("Failed to generate JSON");
      pgexporter_console_destroy(console);
      return badrequest_page(client_ssl, client_fd);
   }

   memset(&msg, 0, sizeof(struct message));
   header_len = snprintf(response_header, sizeof(response_header),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: application/json; charset=utf-8\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         json_size);

   msg.data = response_header;
   msg.length = header_len;
   status = pgexporter_write_message(client_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_log_error("console api_page: failed to write header (status=%d, len=%d)", status, header_len);
   }

   if (status == MESSAGE_STATUS_OK && json_size > 0)
   {
      memset(&msg, 0, sizeof(struct message));
      msg.data = json;
      msg.length = json_size;
      status = pgexporter_write_message(client_ssl, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         pgexporter_log_error("console api_page: failed to write body (status=%d, len=%zu)", status, json_size);
      }
   }

   free(json);
   pgexporter_console_destroy(console);

   return status;
}

static int
detail_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* html = NULL;
   size_t html_size = 0;
   struct message msg;
   int status = 0;
   char response_header[512];
   int header_len;
   int cat_index = pgexporter_console_detail_category_index;

   if (pgexporter_console_init(0, "Web Console", "pgexporter_", &console))
   {
      pgexporter_log_error("Failed to initialize console for detail page");
      return badrequest_page(client_ssl, client_fd);
   }

   if (console == NULL || cat_index < 0 || cat_index >= console->category_count)
   {
      pgexporter_console_destroy(console);
      return badrequest_page(client_ssl, client_fd);
   }

   struct console_category* cat = &console->categories[cat_index];
   char** keys = NULL;
   int keys_count = 0;
   int keys_cap = 0;

   for (int i = 0; i < cat->metric_count; i++)
   {
      struct console_metric* m = &cat->metrics[i];
      for (int j = 0; j < m->label_count; j++)
      {
         char* kv = m->labels[j];
         char* eq = strchr(kv, '=');
         if (!eq)
            continue;
         size_t klen = eq - kv;
         if (klen == 0 || klen > 128)
            continue;
         char keybuf[129];
         strncpy(keybuf, kv, klen);
         keybuf[klen] = '\0';

         int found = 0;
         for (int x = 0; x < keys_count; x++)
         {
            if (strcmp(keys[x], keybuf) == 0)
            {
               found = 1;
               break;
            }
         }
         if (!found)
         {
            if (keys_count == keys_cap)
            {
               keys_cap = keys_cap == 0 ? 8 : keys_cap * 2;
               char** resized = realloc(keys, keys_cap * sizeof(char*));
               if (!resized)
                  break;
               keys = resized;
            }
            keys[keys_count++] = strdup(keybuf);
         }
      }
   }

   html = (char*)malloc(4096);
   if (!html)
   {
      for (int y = 0; y < keys_count; y++)
         free(keys[y]);
      free(keys);
      pgexporter_console_destroy(console);
      return badrequest_page(client_ssl, client_fd);
   }
   size_t buf_size = 4096;
   html[0] = '\0';

   strcat(html, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>Category details</title>\n<style>body{font-family:monospace;margin:20px;}table{border-collapse:collapse;width:100%;}th,td{border:1px solid #ccc;padding:8px;text-align:left;}th{background:#eee}</style>\n</head>\n<body>\n");
   {
      char header[512];
      snprintf(header, sizeof(header), "<h1>Category: %s</h1>\n", cat->name);
      strcat(html, header);
   }

   strcat(html, "<table>\n<tr><th>Name</th><th>Value</th>");
   for (int k = 0; k < keys_count; k++)
   {
      size_t need = strlen(html) + strlen(keys[k]) + 32;
      if (need > buf_size)
      {
         buf_size = need * 2;
         char* resized = realloc(html, buf_size);
         if (!resized)
         {
            for (int y = 0; y < keys_count; y++)
               free(keys[y]);
            free(keys);
            pgexporter_console_destroy(console);
            return badrequest_page(client_ssl, client_fd);
         }
         html = resized;
      }
      strcat(html, "<th>");
      strcat(html, keys[k]);
      strcat(html, "</th>");
   }
   strcat(html, "</tr>\n");

   for (int i = 0; i < cat->metric_count; i++)
   {
      struct console_metric* m = &cat->metrics[i];
      char value_str[64];
      if ((double)((int)m->value) == m->value)
      {
         snprintf(value_str, sizeof(value_str), "%d", (int)m->value);
      }
      else
      {
         snprintf(value_str, sizeof(value_str), "%.2f", m->value);
      }

      size_t need = strlen(html) + 512;
      if (need > buf_size)
      {
         buf_size = need * 2;
         char* resized = realloc(html, buf_size);
         if (!resized)
         {
            for (int y = 0; y < keys_count; y++)
               free(keys[y]);
            free(keys);
            pgexporter_console_destroy(console);
            return badrequest_page(client_ssl, client_fd);
         }
         html = resized;
      }

      strcat(html, "<tr>");
      strcat(html, "<td>");
      strcat(html, m->name);
      strcat(html, "</td>");
      strcat(html, "<td>");
      strcat(html, value_str);
      strcat(html, "</td>");

      for (int k = 0; k < keys_count; k++)
      {
         const char* found_val = "";
         for (int j = 0; j < m->label_count; j++)
         {
            char* kv = m->labels[j];
            char* eq = strchr(kv, '=');
            if (!eq)
               continue;
            size_t klen = eq - kv;
            if (strlen(keys[k]) == klen && strncmp(keys[k], kv, klen) == 0)
            {
               found_val = eq + 1;
               break;
            }
         }
         strcat(html, "<td>");
         strcat(html, found_val);
         strcat(html, "</td>");
      }

      strcat(html, "</tr>\n");
   }

   strcat(html, "</table>\n</body>\n</html>\n");

   for (int y = 0; y < keys_count; y++)
      free(keys[y]);
   free(keys);

   memset(&msg, 0, sizeof(struct message));
   header_len = snprintf(response_header, sizeof(response_header),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         strlen(html));

   msg.data = response_header;
   msg.length = header_len;
   status = pgexporter_write_message(client_ssl, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_log_error("console detail_page: failed to write header (status=%d, len=%d)", status, header_len);
   }

   if (status == MESSAGE_STATUS_OK)
   {
      memset(&msg, 0, sizeof(struct message));
      msg.data = html;
      msg.length = strlen(html);
      status = pgexporter_write_message(client_ssl, client_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         pgexporter_log_error("console detail_page: failed to write body (status=%d, len=%zu)", status, strlen(html));
      }
   }

   free(html);
   pgexporter_console_destroy(console);

   return status;
}

int
pgexporter_console_init(int endpoint, const char* brand_name, const char* metric_prefix, struct console_page** result)
{
   struct console_page* console = NULL;

   if (result == NULL)
   {
      pgexporter_log_error("Invalid parameters for console init");
      goto error;
   }

   console = (struct console_page*)malloc(sizeof(struct console_page));
   if (console == NULL)
   {
      pgexporter_log_error("Failed to allocate console page");
      goto error;
   }

   memset(console, 0, sizeof(struct console_page));

   console->brand_name = brand_name ? strdup(brand_name) : strdup("Metrics Console");
   console->metric_prefix = metric_prefix ? strdup(metric_prefix) : NULL;

   console->status = (struct console_status*)malloc(sizeof(struct console_status));
   if (console->status == NULL)
   {
      pgexporter_log_error("Failed to allocate console status");
      goto error;
   }

   memset(console->status, 0, sizeof(struct console_status));

   if (pgexporter_console_refresh_metrics(endpoint, console))
   {
      pgexporter_log_error("Failed to refresh metrics");
      goto error;
   }

   if (pgexporter_console_refresh_status(console))
   {
      pgexporter_log_warn("Failed to refresh status");
   }

   console->refresh_time = time(NULL);

   *result = console;

   return 0;

error:
   if (console != NULL)
   {
      pgexporter_console_destroy(console);
   }

   return 1;
}

int
pgexporter_console_refresh_metrics(int endpoint, struct console_page* console)
{
   struct prometheus_bridge* bridge = NULL;
   struct configuration* config = NULL;
   int effective_endpoint = endpoint;

   if (console == NULL)
   {
      pgexporter_log_error("Invalid console parameter");
      goto error;
   }

   config = (struct configuration*)shmem;
   if (config != NULL)
   {
      if (config->number_of_endpoints <= 0 || effective_endpoint >= config->number_of_endpoints || config->endpoints[effective_endpoint].port == 0)
      {
         if (config->metrics > 0)
         {
            effective_endpoint = 0;
            config->number_of_endpoints = 1;
            /* Use loopback if host is wildcard/empty */
            const char* h = (strlen(config->host) == 0 || strcmp(config->host, "*") == 0 || strcmp(config->host, "0.0.0.0") == 0) ? "127.0.0.1" : config->host;
            strncpy(config->endpoints[0].host, h, MISC_LENGTH - 1);
            config->endpoints[0].host[MISC_LENGTH - 1] = '\0';
            config->endpoints[0].port = config->metrics;
         }
         else
         {
            pgexporter_log_error("No Prometheus endpoint configured and metrics listener disabled");
            goto error;
         }
      }
   }

   if (pgexporter_prometheus_client_create_bridge(&bridge))
   {
      pgexporter_log_error("Failed to create Prometheus bridge");
      goto error;
   }

   if (pgexporter_prometheus_client_get(effective_endpoint, bridge))
   {
      pgexporter_log_error("Failed to fetch metrics from endpoint %d", effective_endpoint);
      goto error;
   }

   if (build_categories_from_bridge(bridge, console))
   {
      pgexporter_log_error("Failed to build categories from metrics");
      goto error;
   }

   pgexporter_prometheus_client_destroy_bridge(bridge);

   console->refresh_time = time(NULL);

   return 0;

error:
   if (bridge != NULL)
   {
      pgexporter_prometheus_client_destroy_bridge(bridge);
   }

   return 1;
}

int
pgexporter_console_refresh_status(struct console_page* console)
{
   int socket = -1;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   struct json* payload = NULL;
   struct json* response = NULL;
   struct json* servers = NULL;
   struct configuration* config = NULL;
   char timestamp[128];
   time_t now;
   struct tm* time_info;
   int num_servers = 0;

   if (console == NULL || console->status == NULL)
   {
      pgexporter_log_error("Invalid console or status parameter");
      goto error;
   }

   config = (struct configuration*)shmem;
   if (config == NULL)
   {
      goto default_values;
   }

   if (pgexporter_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
   {
      pgexporter_log_debug("Failed to connect to management socket, using default values");
      goto default_values;
   }

   if (pgexporter_management_request_status(NULL, socket, compression, encryption, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      pgexporter_log_warn("Failed to send status request");
      goto default_values;
   }

   if (pgexporter_management_read_json(NULL, socket, &compression, &encryption, &payload))
   {
      pgexporter_log_warn("Failed to read status response");
      goto default_values;
   }

   response = (struct json*)pgexporter_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE);
   if (response == NULL)
   {
      pgexporter_log_warn("No response in payload");
      goto default_values;
   }

   char* server_version = (char*)pgexporter_json_get(response, MANAGEMENT_ARGUMENT_SERVER_VERSION);
   if (server_version != NULL)
   {
      console->status->version = strdup(server_version);
   }
   else
   {
      console->status->version = strdup(VERSION);
   }

   num_servers = (int32_t)(uintptr_t)pgexporter_json_get(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS);
   console->status->num_servers = num_servers;

   servers = (struct json*)pgexporter_json_get(response, MANAGEMENT_ARGUMENT_SERVERS);
   if (servers != NULL && servers->type == JSONArray)
   {
      int active_count = 0;
      int server_idx = 0;
      struct json_iterator* iter = NULL;

      console->status->servers = (struct console_server*)malloc(num_servers * sizeof(struct console_server));
      if (console->status->servers != NULL)
      {
         memset(console->status->servers, 0, num_servers * sizeof(struct console_server));
      }

      if (pgexporter_json_iterator_create(servers, &iter) == 0)
      {
         while (pgexporter_json_iterator_next(iter) && server_idx < num_servers)
         {
            struct json* server = (struct json*)(iter->value->data);
            bool active = (bool)(uintptr_t)pgexporter_json_get(server, MANAGEMENT_ARGUMENT_ACTIVE);
            char* server_name = (char*)pgexporter_json_get(server, MANAGEMENT_ARGUMENT_SERVER);

            if (console->status->servers != NULL)
            {
               console->status->servers[server_idx].name = server_name ? strdup(server_name) : strdup("unknown");
               console->status->servers[server_idx].active = active;
            }

            if (active)
            {
               active_count++;
            }

            server_idx++;
         }

         pgexporter_json_iterator_destroy(iter);
      }

      if (active_count == num_servers)
      {
         console->status->status = strdup("Running");
      }
      else if (active_count > 0)
      {
         console->status->status = strdup("Partial");
      }
      else
      {
         console->status->status = strdup("Disconnected");
      }
   }
   else
   {
      console->status->status = strdup("Running");
      console->status->servers = NULL;
   }

   now = time(NULL);
   time_info = localtime(&now);
   if (time_info != NULL)
   {
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
      console->status->last_updated = strdup(timestamp);
   }
   else
   {
      console->status->last_updated = strdup("Unknown");
   }

   pgexporter_json_destroy(payload);
   pgexporter_disconnect(socket);

   return 0;

default_values:
   if (console->status->status == NULL)
   {
      console->status->status = strdup("Unknown");
   }
   if (console->status->version == NULL)
   {
      console->status->version = strdup(VERSION);
   }
   if (console->status->last_updated == NULL)
   {
      console->status->last_updated = strdup("Unknown");
   }
   if (console->status->num_servers == 0)
   {
      console->status->num_servers = 1;
   }

   if (socket != -1)
   {
      pgexporter_disconnect(socket);
   }
   if (payload != NULL)
   {
      pgexporter_json_destroy(payload);
   }

   return 0;

error:
   return 1;
}

int
pgexporter_console_generate_html(struct console_page* console, char** html, size_t* html_size)
{
   char* tabs_html = NULL;
   char* final_html = NULL;
   size_t buffer_size = 8192;

   if (console == NULL || html == NULL || html_size == NULL)
   {
      pgexporter_log_error("Invalid parameters for HTML generation");
      return 1;
   }

   final_html = (char*)malloc(buffer_size);
   if (final_html == NULL)
   {
      pgexporter_log_error("Failed to allocate HTML buffer");
      return 1;
   }

   memset(final_html, 0, buffer_size);

   /* Build HTML header */
   snprintf(final_html, buffer_size,
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "<meta charset=\"UTF-8\">\n"
            "<title>Web Console</title>\n"
            "<style>\n"
            "body { font-family: monospace; margin: 20px; }\n"
            "h1 { border-bottom: 1px solid #000; }\n"
            "h2 { margin-top: 12px; }\n"
            "table { border-collapse: collapse; width: 100%%; margin: 10px 0; }\n"
            "th, td { border: 1px solid #ccc; padding: 8px; text-align: left; }\n"
            "th { background-color: #eee; font-weight: bold; }\n"
            ".tab-bar { display: flex; gap: 18px; flex-wrap: wrap; align-items: center; justify-content: flex-start; margin: 12px 0 20px 0; }\n"
            ".tab-btn { border: 1px solid #ccc; background: #f5f5f5; padding: 6px 10px; cursor: pointer; border-radius: 4px; font-weight: 600; }\n"
            ".tab-btn.active { background: #222; color: #fff; }\n"
            ".tab-panel { display: none; }\n"
            ".tab-panel.active { display: block; }\n"
            ".view-toggle { display: flex; gap: 8px; align-items: center; }\n"
            ".view-btn { border: 1px solid #ccc; background: #f5f5f5; padding: 6px 10px; cursor: pointer; border-radius: 4px; font-weight: 600; }\n"
            ".view-btn.active { background: #222; color: #fff; }\n"
            ".simple .col-type, .simple .col-labels { display: none; }\n"
            ".tab-bar label { margin: 0; font-weight: 600; }\n"
            ".tab-bar select { padding: 6px 8px; border-radius: 4px; border: 1px solid #ccc; background: #fff; }\n"
            ".header-box { padding: 12px; background: #fff; border: 1px solid #ddd; border-radius: 8px; box-shadow: 0 1px 2px rgba(0,0,0,0.03); margin-bottom: 14px; }\n"
            ".tab-container { margin-top: 8px; }\n"
            "</style>\n"
            "</head>\n"
            "<body>\n"
            "<div class=\"header-box\">\n"
            "<h1>Web Console</h1>\n"
            "<p><strong>Status:</strong> %s | <strong>Version:</strong> %s | <strong>Updated:</strong> %s</p>\n"
            "</div>\n",
            console->status->status ? console->status->status : "Unknown",
            console->status->version ? console->status->version : "Unknown",
            console->status->last_updated ? console->status->last_updated : "Never");

   tabs_html = generate_category_tabs(console);
   if (tabs_html != NULL)
   {
      size_t current_len = strlen(final_html);
      size_t needed_len = current_len + strlen(tabs_html) + 256;

      if (needed_len > buffer_size)
      {
         char* resized = realloc(final_html, needed_len);
         if (resized == NULL)
         {
            pgexporter_log_error("Failed to resize HTML buffer");
            free(tabs_html);
            free(final_html);
            return 1;
         }
         final_html = resized;
         buffer_size = needed_len;
      }

      strcat(final_html, tabs_html);
      free(tabs_html);
   }

   strcat(final_html, "</body>\n</html>\n");

   *html = final_html;
   *html_size = strlen(final_html);

   return 0;
}

int
pgexporter_console_generate_json(struct console_page* console, char** json, size_t* json_size)
{
   char* json_buffer = NULL;
   size_t buffer_size = 2048;

   if (console == NULL || json == NULL || json_size == NULL)
   {
      pgexporter_log_error("Invalid parameters for JSON generation");
      return 1;
   }

   json_buffer = (char*)malloc(buffer_size);
   if (json_buffer == NULL)
   {
      pgexporter_log_error("Failed to allocate JSON buffer");
      return 1;
   }

   memset(json_buffer, 0, buffer_size);

   strcat(json_buffer, "{\"categories\":[");

   for (int i = 0; i < console->category_count; i++)
   {
      struct console_category* cat = &console->categories[i];
      char cat_json[512];

      if (i > 0)
      {
         strcat(json_buffer, ",");
      }

      snprintf(cat_json, sizeof(cat_json),
               "{\"name\":\"%s\",\"metrics\":[",
               cat->name);

      size_t needed = strlen(json_buffer) + strlen(cat_json) + (cat->metric_count * 256) + 256;
      if (needed > buffer_size)
      {
         buffer_size = needed * 2;
         char* resized = realloc(json_buffer, buffer_size);
         if (resized == NULL)
         {
            free(json_buffer);
            return 1;
         }
         json_buffer = resized;
      }

      strcat(json_buffer, cat_json);

      for (int j = 0; j < cat->metric_count; j++)
      {
         struct console_metric* metric = &cat->metrics[j];
         char metric_json[256];

         if (j > 0)
         {
            strcat(json_buffer, ",");
         }

         snprintf(metric_json, sizeof(metric_json),
                  "{\"name\":\"%s\",\"type\":\"%s\",\"value\":%.2f}",
                  metric->name,
                  metric->type,
                  metric->value);

         strcat(json_buffer, metric_json);
      }

      strcat(json_buffer, "]}");
   }

   strcat(json_buffer, "]}");

   *json = json_buffer;
   *json_size = strlen(json_buffer);

   return 0;
}

struct console_metric*
pgexporter_console_get_metric(struct console_page* console, char* metric_name)
{
   if (console == NULL || metric_name == NULL)
   {
      return NULL;
   }

   for (int i = 0; i < console->category_count; i++)
   {
      struct console_category* cat = &console->categories[i];
      for (int j = 0; j < cat->metric_count; j++)
      {
         struct console_metric* metric = &cat->metrics[j];
         if (strcmp(metric->name, metric_name) == 0)
         {
            return metric;
         }
      }
   }

   return NULL;
}

struct console_category*
pgexporter_console_get_category(struct console_page* console, char* category_name)
{
   if (console == NULL || category_name == NULL)
   {
      return NULL;
   }

   for (int i = 0; i < console->category_count; i++)
   {
      if (strcmp(console->categories[i].name, category_name) == 0)
      {
         return &console->categories[i];
      }
   }

   return NULL;
}

int
pgexporter_console_destroy(struct console_page* console)
{
   if (console != NULL)
   {
      if (console->categories != NULL)
      {
         for (int i = 0; i < console->category_count; i++)
         {
            struct console_category* cat = &console->categories[i];
            free(cat->name);

            if (cat->metrics != NULL)
            {
               for (int j = 0; j < cat->metric_count; j++)
               {
                  struct console_metric* metric = &cat->metrics[j];
                  free(metric->name);
                  free(metric->type);
                  free(metric->help);

                  if (metric->labels != NULL)
                  {
                     for (int k = 0; k < metric->label_count; k++)
                     {
                        free(metric->labels[k]);
                     }
                     free(metric->labels);
                  }
               }
               free(cat->metrics);
            }
         }
         free(console->categories);
      }

      if (console->status != NULL)
      {
         free(console->status->status);
         free(console->status->version);
         free(console->status->last_updated);

         if (console->status->servers != NULL)
         {
            for (int i = 0; i < console->status->num_servers; i++)
            {
               free(console->status->servers[i].name);
            }
            free(console->status->servers);
         }

         free(console->status);
      }

      free(console->brand_name);
      free(console->metric_prefix);
      free(console);
   }

   return 0;
}

/**
 * Helper: Build categories from Prometheus bridge
 */
static int
build_categories_from_bridge(struct prometheus_bridge* bridge, struct console_page* console)
{
   struct art_iterator* iter = NULL;
   struct prometheus_metric** metrics = NULL;
   int metric_count = 0;
   int metric_capacity = 0;

   struct prefix_count* prefix_counts = NULL;
   int prefix_count_size = 0;
   int prefix_count_capacity = 0;

   if (bridge == NULL || console == NULL)
   {
      pgexporter_log_error("Invalid parameters for building categories");
      return 1;
   }

   console->categories = NULL;
   console->category_count = 0;

   if (pgexporter_art_iterator_create(bridge->metrics, &iter))
   {
      pgexporter_log_error("Failed to create ART iterator");
      return 1;
   }

   /* collect metrics and count shared prefixes */
   while (pgexporter_art_iterator_next(iter))
   {
      struct prometheus_metric* prom_metric = (struct prometheus_metric*)iter->value->data;
      const char* base_name = NULL;

      if (prom_metric == NULL || prom_metric->name == NULL)
      {
         continue;
      }

      base_name = prom_metric->name;
      if (strncmp(base_name, "pgexporter_", strlen("pgexporter_")) == 0)
      {
         base_name = base_name + strlen("pgexporter_");
      }

      if (metric_count == metric_capacity)
      {
         metric_capacity = metric_capacity == 0 ? 64 : metric_capacity * 2;
         struct prometheus_metric** resized = realloc(metrics, metric_capacity * sizeof(struct prometheus_metric*));
         if (resized == NULL)
         {
            pgexporter_log_error("Failed to grow metric list");
            pgexporter_art_iterator_destroy(iter);
            free(metrics);
            return 1;
         }
         metrics = resized;
      }

      metrics[metric_count++] = prom_metric;

      if (record_prefix_counts(base_name, &prefix_counts, &prefix_count_size, &prefix_count_capacity))
      {
         pgexporter_log_error("Failed to record prefix counts");
         pgexporter_art_iterator_destroy(iter);
         free(metrics);
         /* Free prefix_counts before returning */
         for (int i = 0; i < prefix_count_size; i++)
         {
            free(prefix_counts[i].prefix);
         }
         free(prefix_counts);
         return 1;
      }
   }

   pgexporter_art_iterator_destroy(iter);

   /* Build and rank category candidates globally */
   struct category_candidate* candidates = NULL;
   int candidate_count = 0;
   char** selected_categories = NULL;
   int selected_count = 0;

   if (build_category_candidates(prefix_counts, prefix_count_size, &candidates, &candidate_count))
   {
      pgexporter_log_error("Failed to build category candidates");
      for (int i = 0; i < prefix_count_size; i++)
      {
         free(prefix_counts[i].prefix);
      }
      free(prefix_counts);
      free(metrics);
      return 1;
   }

   selected_categories = select_global_categories(candidates, candidate_count, &selected_count);
   if (selected_categories == NULL)
   {
      pgexporter_log_warn("No categories selected, using fallback");
   }

   /* assign metrics to selected categories */
   for (int i = 0; i < metric_count; i++)
   {
      struct prometheus_metric* prom_metric = metrics[i];
      struct console_metric* console_metric = NULL;
      struct console_category* category = NULL;
      char* category_name = NULL;
      char* leaf_name = NULL;
      const char* base_name = NULL;

      if (prom_metric == NULL || prom_metric->name == NULL)
      {
         continue;
      }

      base_name = prom_metric->name;
      if (console->metric_prefix != NULL && strncmp(base_name, console->metric_prefix, strlen(console->metric_prefix)) == 0)
      {
         base_name = base_name + strlen(console->metric_prefix);
      }

      /* Find the best matching category from the globally selected set */
      category_name = find_best_category(base_name, selected_categories, selected_count);
      if (category_name == NULL)
      {
         category_name = extract_category_prefix((char*)base_name);
      }
      if (category_name == NULL)
      {
         category_name = strdup("uncategorized");
      }

      /* Leaf = remainder after category prefix */
      size_t cat_len = strlen(category_name);
      if (strlen(base_name) > cat_len + 1 && base_name[cat_len] == '_')
      {
         leaf_name = strdup(base_name + cat_len + 1);
      }
      else
      {
         leaf_name = strdup(base_name);
      }

      console_metric = create_metric_from_prometheus(prom_metric, leaf_name);
      if (console_metric == NULL)
      {
         pgexporter_log_warn("Failed to create console metric for %s", prom_metric->name);
         free(category_name);
         free(leaf_name);
         continue;
      }

      category = find_or_create_category(console, category_name);
      free(category_name);

      if (category == NULL)
      {
         pgexporter_log_warn("Failed to find/create category for %s", prom_metric->name);
         free(console_metric->name);
         free(console_metric->type);
         free(console_metric->help);
         if (console_metric->labels != NULL)
         {
            free(console_metric->labels);
         }
         free(console_metric);
         free(leaf_name);
         continue;
      }

      if (add_metric_to_category(category, console_metric))
      {
         pgexporter_log_warn("Failed to add metric %s to category", prom_metric->name);
         free(console_metric->name);
         free(console_metric->type);
         free(console_metric->help);
         if (console_metric->labels != NULL)
         {
            free(console_metric->labels);
         }
         free(console_metric);
      }

      free(console_metric);
      free(leaf_name);
   }

   for (int i = 0; i < prefix_count_size; i++)
   {
      free(prefix_counts[i].prefix);
   }
   free(prefix_counts);
   free(metrics);

   if (candidates != NULL)
   {
      for (int i = 0; i < candidate_count; i++)
      {
         free(candidates[i].prefix);
      }
      free(candidates);
   }

   if (selected_categories != NULL)
   {
      for (int i = 0; i < selected_count; i++)
      {
         free(selected_categories[i]);
      }
      free(selected_categories);
   }

   return 0;
}

/**
 * Helper: Extract category prefix from metric name
 * Example: "pg_stat_statements_calls" -> "pg_stat_statements"
 */
static char*
extract_category_prefix(char* metric_name)
{
   char* prefix = NULL;
   if (metric_name == NULL)
   {
      return NULL;
   }

   prefix = fallback_category_from_last_underscore(metric_name);

   return prefix;
}

static char*
fallback_category_from_last_underscore(char* metric_name)
{
   char* prefix = NULL;
   char* last_underscore = NULL;

   if (metric_name == NULL)
   {
      return NULL;
   }

   prefix = strdup(metric_name);
   if (prefix == NULL)
   {
      return NULL;
   }

   last_underscore = strrchr(prefix, '_');
   if (last_underscore != NULL)
   {
      *last_underscore = '\0';
   }

   return prefix;
}

/**
 * Helper: Find or create category
 */
static struct console_category*
find_or_create_category(struct console_page* console, char* category_name)
{
   /* Try to find existing */
   for (int i = 0; i < console->category_count; i++)
   {
      if (strcmp(console->categories[i].name, category_name) == 0)
      {
         return &console->categories[i];
      }
   }

   /* Create new category */
   struct console_category* new_categories = realloc(console->categories,
                                                     (console->category_count + 1) * sizeof(struct console_category));
   if (new_categories == NULL)
   {
      pgexporter_log_error("Failed to reallocate categories");
      return NULL;
   }

   console->categories = new_categories;
   struct console_category* new_cat = &console->categories[console->category_count];
   memset(new_cat, 0, sizeof(struct console_category));

   new_cat->name = strdup(category_name);
   if (new_cat->name == NULL)
   {
      pgexporter_log_error("Failed to duplicate category name");
      return NULL;
   }

   console->category_count++;

   return new_cat;
}

/**
 * Helper: Add metric to category
 */
static int
add_metric_to_category(struct console_category* category, struct console_metric* metric)
{
   struct console_metric* new_metrics = NULL;

   if (category == NULL || metric == NULL)
   {
      pgexporter_log_error("Invalid parameters for adding metric");
      return 1;
   }

   new_metrics = realloc(category->metrics,
                         (category->metric_count + 1) * sizeof(struct console_metric));
   if (new_metrics == NULL)
   {
      pgexporter_log_error("Failed to reallocate metrics");
      return 1;
   }

   category->metrics = new_metrics;
   memcpy(&category->metrics[category->metric_count], metric, sizeof(struct console_metric));
   category->metric_count++;

   return 0;
}

/**
 * Helper: Create console_metric from prometheus_metric
 */
static struct console_metric*
create_metric_from_prometheus(struct prometheus_metric* prom_metric, const char* display_name)
{
   struct console_metric* metric = NULL;

   if (prom_metric == NULL)
   {
      return NULL;
   }

   metric = (struct console_metric*)malloc(sizeof(struct console_metric));
   if (metric == NULL)
   {
      pgexporter_log_error("Failed to allocate console metric");
      return NULL;
   }

   memset(metric, 0, sizeof(struct console_metric));

   metric->name = strdup(display_name != NULL ? display_name : prom_metric->name);
   metric->type = strdup(prom_metric->type != NULL ? prom_metric->type : "gauge");
   metric->help = strdup(prom_metric->help != NULL ? prom_metric->help : "");
   metric->value = 0.0;
   metric->label_count = 0;
   metric->labels = NULL;

   /* Extract value and labels from the last definition */
   if (prom_metric->definitions != NULL && pgexporter_deque_size(prom_metric->definitions) > 0)
   {
      struct prometheus_attributes* attrs = NULL;
      struct prometheus_value* value_data = NULL;
      struct deque_iterator* attr_iter = NULL;
      int label_idx = 0;

      attrs = (struct prometheus_attributes*)pgexporter_deque_peek_last(prom_metric->definitions, NULL);

      if (attrs != NULL)
      {
         /* Get the last value (most recent timestamp) */
         if (attrs->values != NULL && pgexporter_deque_size(attrs->values) > 0)
         {
            value_data = (struct prometheus_value*)pgexporter_deque_peek_last(attrs->values, NULL);
            if (value_data != NULL && value_data->value != NULL)
            {
               metric->value = atof(value_data->value);
            }
         }

         /* Extract labels from attributes */
         if (attrs->attributes != NULL && pgexporter_deque_size(attrs->attributes) > 0)
         {
            metric->label_count = pgexporter_deque_size(attrs->attributes);
            metric->labels = (char**)malloc(metric->label_count * sizeof(char*));

            if (metric->labels != NULL)
            {
               if (pgexporter_deque_iterator_create(attrs->attributes, &attr_iter) == 0)
               {
                  while (pgexporter_deque_iterator_next(attr_iter) && label_idx < metric->label_count)
                  {
                     struct prometheus_attribute* attr = (struct prometheus_attribute*)attr_iter->value->data;
                     if (attr != NULL && attr->key != NULL && attr->value != NULL)
                     {
                        metric->labels[label_idx] = format_label_value(attr->key, attr->value);
                        label_idx++;
                     }
                  }
                  pgexporter_deque_iterator_destroy(attr_iter);
               }

               /* Update actual label count in case some failed */
               metric->label_count = label_idx;
            }
            else
            {
               metric->label_count = 0;
            }
         }
      }
   }

   return metric;
}

/**
 * Helper: Format label as "key=value"
 */
static char*
format_label_value(char* key, char* value)
{
   char* label = NULL;
   size_t len = 0;

   if (key == NULL || value == NULL)
   {
      return NULL;
   }

   len = strlen(key) + strlen(value) + 2; /* key + = + value + null */
   label = (char*)malloc(len);
   if (label == NULL)
   {
      pgexporter_log_error("Failed to allocate label string");
      return NULL;
   }

   snprintf(label, len, "%s=%s", key, value);

   return label;
}

/**
 * Helper: Increment shared prefix counts for a metric name
 */
static int
record_prefix_counts(const char* metric_name, struct prefix_count** counts, int* size, int* capacity)
{
   int len = 0;

   if (metric_name == NULL || counts == NULL || size == NULL || capacity == NULL)
   {
      return 1;
   }

   len = strlen(metric_name);

   /* Traverse the string and record prefixes at every underscore boundary */
   for (int i = 0; i < len; i++)
   {
      if (metric_name[i] == '_')
      {
         char* prefix = strndup(metric_name, i);
         if (prefix == NULL)
         {
            return 1;
         }

         /* Find existing */
         int found = -1;
         for (int j = 0; j < *size; j++)
         {
            if (strcmp((*counts)[j].prefix, prefix) == 0)
            {
               found = j;
               break;
            }
         }

         if (found == -1)
         {
            if (*size == *capacity)
            {
               *capacity = (*capacity == 0) ? 32 : (*capacity * 2);
               struct prefix_count* resized = realloc(*counts, *capacity * sizeof(struct prefix_count));
               if (resized == NULL)
               {
                  free(prefix);
                  return 1;
               }
               *counts = resized;
            }

            (*counts)[*size].prefix = prefix;
            (*counts)[*size].count = 1;
            (*size)++;
         }
         else
         {
            (*counts)[found].count += 1;
            free(prefix);
         }
      }
   }

   {
      char* prefix = strdup(metric_name);
      if (prefix == NULL)
      {
         return 1;
      }

      int found = -1;
      for (int j = 0; j < *size; j++)
      {
         if (strcmp((*counts)[j].prefix, prefix) == 0)
         {
            found = j;
            break;
         }
      }

      if (found == -1)
      {
         if (*size == *capacity)
         {
            *capacity = (*capacity == 0) ? 32 : (*capacity * 2);
            struct prefix_count* resized = realloc(*counts, *capacity * sizeof(struct prefix_count));
            if (resized == NULL)
            {
               free(prefix);
               return 1;
            }
            *counts = resized;
         }

         (*counts)[*size].prefix = prefix;
         (*counts)[*size].count = 1;
         (*size)++;
      }
      else
      {
         (*counts)[found].count += 1;
         free(prefix);
      }
   }

   return 0;
}

/**
 * Helper: Count the depth (number of underscores) in a prefix
 */
static int
count_prefix_depth(const char* prefix)
{
   int depth = 0;
   if (prefix == NULL)
   {
      return 0;
   }

   for (int i = 0; prefix[i] != '\0'; i++)
   {
      if (prefix[i] == '_')
      {
         depth++;
      }
   }
   return depth;
}

/**
 * Helper: Build category candidates from prefix counts
 * Filters by MIN_GROUP_SIZE and MAX_DEPTH, calculates scores
 */
static int
build_category_candidates(struct prefix_count* counts, int size, struct category_candidate** candidates, int* candidate_count)
{
   struct category_candidate* cands = NULL;
   int count = 0;
   int capacity = 0;

   if (counts == NULL || candidates == NULL || candidate_count == NULL)
   {
      return 1;
   }

   for (int i = 0; i < size; i++)
   {
      int depth = count_prefix_depth(counts[i].prefix);

      /* Filter: must have enough members, sufficient depth, and not be too deep */
      if (counts[i].count >= MIN_GROUP_SIZE && depth > 0 && depth <= MAX_DEPTH)
      {
         if (count == capacity)
         {
            capacity = capacity == 0 ? 16 : capacity * 2;
            struct category_candidate* resized = realloc(cands, capacity * sizeof(struct category_candidate));
            if (resized == NULL)
            {
               free(cands);
               return 1;
            }
            cands = resized;
         }

         cands[count].prefix = strdup(counts[i].prefix);
         cands[count].count = counts[i].count;
         cands[count].depth = depth;
         /* higher count and moderate depth preferred */
         cands[count].score = counts[i].count * (1.0 + depth * 0.2);
         count++;
      }
   }

   *candidates = cands;
   *candidate_count = count;
   return 0;
}

/**
 * Helper: Compare candidates by score (descending)
 */
static int
compare_candidates_by_score(const void* a, const void* b)
{
   const struct category_candidate* ca = (const struct category_candidate*)a;
   const struct category_candidate* cb = (const struct category_candidate*)b;

   if (cb->score > ca->score)
   {
      return 1;
   }
   else if (cb->score < ca->score)
   {
      return -1;
   }
   return 0;
}

/**
 * Helper: Select non-overlapping category prefixes globally
 * Sort by score descending, accept prefix only if not already covered by a longer accepted prefix
 */
static char**
select_global_categories(struct category_candidate* candidates, int candidate_count, int* selected_count)
{
   char** selected = NULL;
   int count = 0;
   int capacity = 0;

   if (candidates == NULL || candidate_count == 0 || selected_count == NULL)
   {
      *selected_count = 0;
      return NULL;
   }

   /* Sort candidates by score descending */
   qsort(candidates, candidate_count, sizeof(struct category_candidate), compare_candidates_by_score);

   for (int i = 0; i < candidate_count; i++)
   {
      int is_covered = 0;

      /* Check if this candidate is already covered by a longer accepted prefix */
      for (int j = 0; j < count; j++)
      {
         int sel_len = strlen(selected[j]);
         int cand_len = strlen(candidates[i].prefix);

         if (cand_len > sel_len && strncmp(candidates[i].prefix, selected[j], sel_len) == 0 && candidates[i].prefix[sel_len] == '_')
         {
            is_covered = 1;
            break;
         }
      }

      if (!is_covered)
      {
         if (count == capacity)
         {
            capacity = capacity == 0 ? 16 : capacity * 2;
            char** resized = realloc(selected, capacity * sizeof(char*));
            if (resized == NULL)
            {
               for (int k = 0; k < count; k++)
               {
                  free(selected[k]);
               }
               free(selected);
               return NULL;
            }
            selected = resized;
         }

         selected[count] = strdup(candidates[i].prefix);
         count++;
      }
   }

   *selected_count = count;
   return selected;
}

/**
 * Helper: Find the longest matching category for a metric name
 */
static char*
find_best_category(const char* metric_name, char** categories, int category_count)
{
   char* best = NULL;
   int best_len = -1;

   if (metric_name == NULL || categories == NULL || category_count == 0)
   {
      return NULL;
   }

   for (int i = 0; i < category_count; i++)
   {
      int cat_len = strlen(categories[i]);
      int name_len = strlen(metric_name);

      /* Check if category is a prefix of metric_name followed by _ */
      if (name_len > cat_len && strncmp(metric_name, categories[i], cat_len) == 0 && metric_name[cat_len] == '_')
      {
         if (cat_len > best_len)
         {
            best_len = cat_len;
            best = categories[i];
         }
      }
   }

   return best != NULL ? strdup(best) : NULL;
}

/**
 * Helper: Generate HTML table for metrics in a category
 */
static char*
generate_metrics_table(struct console_category* category, int cat_index)
{
   char* table_html = NULL;
   size_t buffer_size = 2048;

   if (category == NULL || category->metric_count == 0)
   {
      return strdup("<p>No metrics</p>\n");
   }

   table_html = (char*)malloc(buffer_size);
   if (table_html == NULL)
   {
      return NULL;
   }

   memset(table_html, 0, buffer_size);

   strcat(table_html,
          "<table class=\"metrics-table\">\n"
          "<tr><th class=\"col-name\">Name</th><th class=\"col-type\">Type</th><th class=\"col-value\">Value</th><th class=\"col-labels\">Labels</th></tr>\n");

   for (int m_idx = 0; m_idx < category->metric_count; m_idx++)
   {
      struct console_metric* metric = &category->metrics[m_idx];
      char row_buffer[512];
      char labels_str[256] = "";

      /* Build labels string */
      if (metric->label_count > 0)
      {
         for (int j = 0; j < metric->label_count; j++)
         {
            if (j > 0 && strlen(labels_str) < sizeof(labels_str) - 3)
            {
               strcat(labels_str, ", ");
            }
            if (strlen(labels_str) + strlen(metric->labels[j]) < sizeof(labels_str) - 1)
            {
               strcat(labels_str, metric->labels[j]);
            }
         }
      }

      /* Format - show as int if possible, else 2 decimals */
      char value_str[64];
      if ((double)((int)metric->value) == metric->value)
      {
         snprintf(value_str, sizeof(value_str), "%d", (int)metric->value);
      }
      else
      {
         snprintf(value_str, sizeof(value_str), "%.2f", metric->value);
      }
      snprintf(row_buffer, sizeof(row_buffer),
               "<tr><td class=\"col-name\">%s</td><td class=\"col-type\">%s</td><td class=\"col-value\">%s</td><td class=\"col-labels\">%s</td></tr>\n",
               metric->name,
               metric->type,
               value_str,
               strlen(labels_str) > 0 ? labels_str : "");

      size_t needed = strlen(table_html) + strlen(row_buffer) + 1;
      if (needed > buffer_size)
      {
         buffer_size *= 2;
         char* resized = realloc(table_html, buffer_size);
         if (resized == NULL)
         {
            free(table_html);
            return NULL;
         }
         table_html = resized;
      }

      strcat(table_html, row_buffer);
   }

   strcat(table_html, "</table>\n");

   return table_html;
}

/**
 * Helper: Generate tab buttons and content sections for all categories
 */
static char*
generate_category_tabs(struct console_page* console)
{
   char* tabs_html = NULL;
   size_t buffer_size = 8192;

   if (console == NULL || console->category_count == 0)
   {
      return strdup("<p>No metrics available</p>\n");
   }

   tabs_html = (char*)malloc(buffer_size);
   if (tabs_html == NULL)
   {
      return NULL;
   }

   tabs_html[0] = '\0';

   strcat(tabs_html, "<div class=\"tab-container\">\n<div class=\"tab-bar\">\n");

   {
      char view_buf[256];
      snprintf(view_buf, sizeof(view_buf),
               "<div class=\"view-toggle\">\n"
               "<label for=\"view-select\">View:</label>\n"
               "<select id=\"view-select\">\n"
               "<option value=\"simple\" selected>Simple</option>\n"
               "<option value=\"detailed\">Detailed</option>\n"
               "</select>\n"
               "</div>\n");

      size_t needed_v = strlen(tabs_html) + strlen(view_buf) + 1;
      if (needed_v > buffer_size)
      {
         buffer_size = needed_v * 2;
         char* resized = realloc(tabs_html, buffer_size);
         if (resized == NULL)
         {
            free(tabs_html);
            return NULL;
         }
         tabs_html = resized;
      }

      strcat(tabs_html, view_buf);
   }

   strcat(tabs_html, "<label for=\"category-select\">Category:</label>\n");
   strcat(tabs_html, "<select id=\"category-select\">\n");

   for (int i = 0; i < console->category_count; i++)
   {
      char opt_buffer[256];
      snprintf(opt_buffer, sizeof(opt_buffer),
               "<option value=\"cat-%d\"%s>%s</option>\n",
               i,
               i == 0 ? " selected" : "",
               console->categories[i].name);

      size_t needed = strlen(tabs_html) + strlen(opt_buffer) + 1;
      if (needed > buffer_size)
      {
         buffer_size = needed * 2;
         char* resized = realloc(tabs_html, buffer_size);
         if (resized == NULL)
         {
            free(tabs_html);
            return NULL;
         }
         tabs_html = resized;
      }

      strcat(tabs_html, opt_buffer);
   }

   strcat(tabs_html, "</select>\n");

   {
      char srv_buf[2048];
      size_t pos = 0;
      pos += snprintf(srv_buf + pos, sizeof(srv_buf) - pos, "<label for=\"server-select\">Servers:</label>\n<select id=\"server-select\">\n");

      /* default 'All servers' option */
      pos += snprintf(srv_buf + pos, sizeof(srv_buf) - pos, "<option value=\"all\" selected>All servers</option>\n");

      if (console->status && console->status->servers && console->status->num_servers > 0)
      {
         for (int s = 0; s < console->status->num_servers; s++)
         {
            const char* name = console->status->servers[s].name ? console->status->servers[s].name : "server";
            pos += snprintf(srv_buf + pos, sizeof(srv_buf) - pos, "<option value=\"%s\">%s</option>\n", name, name);
            if (pos >= (int)sizeof(srv_buf) - 128)
               break;
         }
      }
      else
      {
         pos += snprintf(srv_buf + pos, sizeof(srv_buf) - pos, "<option disabled>No servers</option>\n");
      }

      pos += snprintf(srv_buf + pos, sizeof(srv_buf) - pos, "</select>\n");

      size_t needed_s = strlen(tabs_html) + strlen(srv_buf) + 1;
      if (needed_s > buffer_size)
      {
         buffer_size = needed_s * 2;
         char* resized = realloc(tabs_html, buffer_size);
         if (resized == NULL)
         {
            free(tabs_html);
            return NULL;
         }
         tabs_html = resized;
      }

      strcat(tabs_html, srv_buf);
   }

   strcat(tabs_html, "</div>\n<div class=\"tab-panels\">\n");

   for (int i = 0; i < console->category_count; i++)
   {
      char panel_header[256];
      char* metrics_table = NULL;
      snprintf(panel_header, sizeof(panel_header),
               "<div class=\"tab-panel\" id=\"cat-%d\" style=\"display:%s\">\n<h2><a href=\"/detail?cat=%d\">%s</a></h2>\n",
               i,
               i == 0 ? "block" : "none",
               i,
               console->categories[i].name);

      size_t needed = strlen(tabs_html) + strlen(panel_header) + 1024;
      if (needed > buffer_size)
      {
         buffer_size = needed * 2;
         char* resized = realloc(tabs_html, buffer_size);
         if (resized == NULL)
         {
            free(tabs_html);
            return NULL;
         }
         tabs_html = resized;
      }

      strcat(tabs_html, panel_header);

      metrics_table = generate_metrics_table(&console->categories[i], i);
      if (metrics_table != NULL)
      {
         size_t mt_needed = strlen(tabs_html) + strlen(metrics_table) + 16;
         if (mt_needed > buffer_size)
         {
            buffer_size = mt_needed * 2;
            char* resized = realloc(tabs_html, buffer_size);
            if (resized == NULL)
            {
               free(metrics_table);
               free(tabs_html);
               return NULL;
            }
            tabs_html = resized;
         }

         strcat(tabs_html, metrics_table);
         free(metrics_table);
      }

      strcat(tabs_html, "</div>\n");
   }

   strcat(tabs_html, "</div>\n</div>\n");

   strcat(tabs_html,
          "<script>\n"
          "(function(){\n"
          "  const select = document.getElementById('category-select');\n"
          "  const panels = document.querySelectorAll('.tab-panel');\n"
          "  const container = document.querySelector('.tab-container');\n"
          "  const viewButtons = document.querySelectorAll('.view-btn');\n"
          "  function show(id){\n"
          "    panels.forEach(p=>p.style.display = (p.id===id) ? 'block' : 'none');\n"
          "  }\n"
          "  select.addEventListener('change', function(){ show(this.value); });\n"
          "  // View mode dropdown\n"
          "  const viewSelect = document.getElementById('view-select');\n"
          "  if(viewSelect){\n"
          "    if(viewSelect.value === 'simple'){ container.classList.add('simple'); } else { container.classList.remove('simple'); }\n"
          "    viewSelect.addEventListener('change', function(){\n"
          "      if(this.value === 'simple'){ container.classList.add('simple'); } else { container.classList.remove('simple'); }\n"
          "    });\n"
          "  }\n"
          "  if(select && select.options.length){ show(select.value); }\n"
          "})();\n"
          "</script>\n");

   return tabs_html;
}

void
pgexporter_console(SSL* client_ssl, int client_fd)
{
   struct configuration* config = (struct configuration*)shmem;
   struct message* msg = NULL;
   int page;
   int status = MESSAGE_STATUS_OK;

   pgexporter_start_logging();
   pgexporter_memory_init();

   if (client_ssl)
   {
      char buffer[5] = {0};

      recv(client_fd, buffer, 5, MSG_PEEK);

      if ((unsigned char)buffer[0] == 0x16 || (unsigned char)buffer[0] == 0x80) // SSL/TLS request
      {
         if (SSL_accept(client_ssl) <= 0)
         {
            pgexporter_log_error("Failed to accept SSL connection");
            goto error;
         }
      }
   }

   pgexporter_log_info("pgexporter_console: start");

   status = pgexporter_read_timeout_message(client_ssl, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      status = home_page(client_ssl, client_fd);
   }
   else if (page == PAGE_API)
   {
      status = api_page(client_ssl, client_fd);
   }
   else if (page == PAGE_DETAIL)
   {
      status = detail_page(client_ssl, client_fd);
   }
   else
   {
      status = badrequest_page(client_ssl, client_fd);
   }

error:
   pgexporter_close_ssl(client_ssl);
   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   if (status == MESSAGE_STATUS_OK)
   {
      exit(0);
   }

   exit(1);
}
