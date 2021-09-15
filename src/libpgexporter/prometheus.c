/*
 * Copyright (C) 2021 Red Hat
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
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <queries.h>
#include <security.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define CHUNK_SIZE 32768

#define PAGE_UNKNOWN 0
#define PAGE_HOME    1
#define PAGE_METRICS 2

static int resolve_page(struct message* msg);
static int unknown_page(int client_fd);
static int home_page(int client_fd);
static int metrics_page(int client_fd);

static void general_information(int client_fd);
static void server_information(int client_fd);
static void database_information(int client_fd);
static void replication_information(int client_fd);
static void settings_information(int client_fd);

static int send_chunk(int client_fd, char* data);

static char* get_value(char* tag, char* name, char* val);

void
pgexporter_prometheus(int client_fd)
{
   int status;
   int page;
   struct message* msg = NULL;
   struct configuration* config;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   status = pgexporter_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   page = resolve_page(msg);

   if (page == PAGE_HOME)
   {
      home_page(client_fd);
   }
   else if (page == PAGE_METRICS)
   {
      metrics_page(client_fd);
   }
   else
   {
      unknown_page(client_fd);
   }

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(0);

error:

   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   exit(1);
}

void
pgexporter_prometheus_reset(void)
{
}

static int
resolve_page(struct message* msg)
{
   char* from = NULL;
   int index;

   if (strncmp((char*)msg->data, "GET", 3) != 0)
   {
      pgexporter_log_debug("Promethus: Not a GET request");
      return PAGE_UNKNOWN;
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
   else if (strcmp(from, "/metrics") == 0)
   {
      return PAGE_METRICS;
   }

   return PAGE_UNKNOWN;
}

static int
unknown_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;
   
   data = pgexporter_append(data, "HTTP/1.1 403 Forbidden\r\n");
   data = pgexporter_append(data, "Date: ");
   data = pgexporter_append(data, &time_buf[0]);
   data = pgexporter_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(NULL, client_fd, &msg);

   free(data);

   return status;
}

static int
home_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&data, 0, sizeof(data));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;
   
   data = pgexporter_append(data, "HTTP/1.1 200 OK\r\n");
   data = pgexporter_append(data, "Content-Type: text/html; charset=utf-8\r\n");
   data = pgexporter_append(data, "Date: ");
   data = pgexporter_append(data, &time_buf[0]);
   data = pgexporter_append(data, "\r\n");
   data = pgexporter_append(data, "Transfer-Encoding: chunked\r\n");
   data = pgexporter_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto done;
   }

   free(data);
   data = NULL;

   data = pgexporter_append(data, "<html>\n");
   data = pgexporter_append(data, "<head>\n");
   data = pgexporter_append(data, "  <title>pgexporter</title>\n");
   data = pgexporter_append(data, "</head>\n");
   data = pgexporter_append(data, "<body>\n");
   data = pgexporter_append(data, "  <h1>pgexporter</h1>\n");
   data = pgexporter_append(data, "  Prometheus exporter for PostgreSQL\n");
   data = pgexporter_append(data, "  <p>\n");
   data = pgexporter_append(data, "  <a href=\"/metrics\">Metrics</a>\n");
   data = pgexporter_append(data, "  <p>\n");
   data = pgexporter_append(data, "  Support for\n");
   data = pgexporter_append(data, "  <ul>\n");
   data = pgexporter_append(data, "  <li>pg_database</li>\n");
   data = pgexporter_append(data, "  <li>pg_replication_slots</li>\n");
   data = pgexporter_append(data, "  <li>pg_settings</li>\n");
   data = pgexporter_append(data, "  </ul>\n");
   data = pgexporter_append(data, "  <p>\n");
   data = pgexporter_append(data, "  <a href=\"https://pgexporter.github.io/\">pgexporter.github.io/</a>\n");
   data = pgexporter_append(data, "</body>\n");
   data = pgexporter_append(data, "</html>\n");

   send_chunk(client_fd, data);
   free(data);
   data = NULL;

   /* Footer */
   data = pgexporter_append(data, "0\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(NULL, client_fd, &msg);

done:
   if (data != NULL)
   {
      free(data);
   }

   return status;
}

static int
metrics_page(int client_fd)
{
   char* data = NULL;
   time_t now;
   char time_buf[32];
   int status;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   now = time(NULL);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&now, &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;
   
   data = pgexporter_append(data, "HTTP/1.1 200 OK\r\n");
   data = pgexporter_append(data, "Content-Type: text/plain; version=0.0.1; charset=utf-8\r\n");
   data = pgexporter_append(data, "Date: ");
   data = pgexporter_append(data, &time_buf[0]);
   data = pgexporter_append(data, "\r\n");
   data = pgexporter_append(data, "Transfer-Encoding: chunked\r\n");
   data = pgexporter_append(data, "\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);
   data = NULL;

   pgexporter_open_connections();

   general_information(client_fd);
   server_information(client_fd);
   database_information(client_fd);
   replication_information(client_fd);
   settings_information(client_fd);

   pgexporter_close_connections();

   /* Footer */
   data = pgexporter_append(data, "0\r\n\r\n");

   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgexporter_write_message(NULL, client_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   free(data);

   return 0;

error:

   free(data);

   return 1;
}

static void
general_information(int client_fd)
{
   char* data = NULL;

   data = pgexporter_append(data, "#HELP pgexporter_state The state of pgexporter\n");
   data = pgexporter_append(data, "#TYPE pgexporter_state gauge\n");
   data = pgexporter_append(data, "pgexporter_state ");
   data = pgexporter_append(data, "1");
   data = pgexporter_append(data, "\n\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }
}

static void
server_information(int client_fd)
{
   char* data = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   data = pgexporter_append(data, "#HELP pgexporter_postgresql_active The state of PostgreSQL\n");
   data = pgexporter_append(data, "#TYPE pgexporter_postgresql_active gauge\n");

   for (int server = 0; server < config->number_of_servers; server++)
   {
      data = pgexporter_append(data, "pgexporter_postgresql_active{server=\"");
      data = pgexporter_append(data, &config->servers[server].name[0]);
      data = pgexporter_append(data, "\"} ");
      if (config->servers[server].fd != -1)
      {
         data = pgexporter_append(data, "1");
      }
      else
      {
         data = pgexporter_append(data, "0");
      }
      data = pgexporter_append(data, "\n");
   }
   data = pgexporter_append(data, "\n");

   if (data != NULL)
   {
      send_chunk(client_fd, data);
      free(data);
      data = NULL;
   }
}

static void
database_information(int client_fd)
{
   int ret;
   char* d;
   char* data = NULL;
   struct tuples* all = NULL;
   struct tuples* tuples = NULL;
   struct tuples* current = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd != -1)
      {
         ret = pgexporter_query_database_size(server, &tuples);
         if (ret == 0)
         {
            all = pgexporter_merge_tuples(all, tuples);
         }
         tuples = NULL;
      }
   }

   current = all;
   if (current != NULL)
   {
      d = NULL;
      d = pgexporter_append(d, "#HELP pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_size Size of the database");
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

      d = NULL;
      d = pgexporter_append(d, "#TYPE pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_size gauge");
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

      while (current != NULL)
      {
         d = NULL;
         d = pgexporter_append(d, "pgexporter_");
         d = pgexporter_append(d, &current->tuple->tag[0]);
         d = pgexporter_append(d, "_size{server=\"");
         d = pgexporter_append(d, &config->servers[current->tuple->server].name[0]);
         d = pgexporter_append(d, "\",database=\"");
         d = pgexporter_append(d, &current->tuple->name[0]);
         d = pgexporter_append(d, "\"} ");
         d = pgexporter_append(d, get_value(&current->tuple->tag[0], &current->tuple->name[0], &current->tuple->value[0]));
         d = pgexporter_append(d, "\n");
         data = pgexporter_append(data, d);
         free(d);

         current = current->next;
      }

      data = pgexporter_append(data, "\n");

      if (data != NULL)
      {
         send_chunk(client_fd, data);
         free(data);
         data = NULL;
      }
   }

   pgexporter_free_tuples(all);
}

static void
replication_information(int client_fd)
{
   int ret;
   char* d;
   char* data = NULL;
   struct tuples* all = NULL;
   struct tuples* tuples = NULL;
   struct tuples* current = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd != -1)
      {
         ret = pgexporter_query_replication_slot_active(server, &tuples);
         if (ret == 0)
         {
            all = pgexporter_merge_tuples(all, tuples);
         }
         tuples = NULL;
      }
   }

   current = all;
   if (current != NULL)
   {
      d = NULL;
      d = pgexporter_append(d, "#HELP pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_active Display status of replication slots");
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

      d = NULL;
      d = pgexporter_append(d, "#TYPE pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_active gauge");
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

      while (current != NULL)
      {
         d = NULL;
         d = pgexporter_append(d, "pgexporter_");
         d = pgexporter_append(d, &current->tuple->tag[0]);
         d = pgexporter_append(d, "_active{server=\"");
         d = pgexporter_append(d, &config->servers[current->tuple->server].name[0]);
         d = pgexporter_append(d, "\",slot=\"");
         d = pgexporter_append(d, &current->tuple->name[0]);
         d = pgexporter_append(d, "\"} ");
         d = pgexporter_append(d, get_value(&current->tuple->tag[0], &current->tuple->name[0], &current->tuple->value[0]));
         d = pgexporter_append(d, "\n");
         data = pgexporter_append(data, d);
         free(d);

         current = current->next;
      }

      data = pgexporter_append(data, "\n");

      if (data != NULL)
      {
         send_chunk(client_fd, data);
         free(data);
         data = NULL;
      }
   }

   pgexporter_free_tuples(all);
}

static void
settings_information(int client_fd)
{
   int ret;
   char* d;
   char* data = NULL;
   struct tuples* all = NULL;
   struct tuples* tuples = NULL;
   struct tuples* current = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd != -1)
      {
         ret = pgexporter_query_settings(server, &tuples);
         if (ret == 0)
         {
            all = pgexporter_merge_tuples(all, tuples);
         }
         tuples = NULL;
      }
   }

   current = all;
   while (current != NULL)
   {
      d = NULL;
      d = pgexporter_append(d, "#HELP pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_");
      d = pgexporter_append(d, &current->tuple->name[0]);
      d = pgexporter_append(d, " ");
      d = pgexporter_append(d, &current->tuple->desc[0]);
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

      d = NULL;
      d = pgexporter_append(d, "#TYPE pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_");
      d = pgexporter_append(d, &current->tuple->name[0]);
      d = pgexporter_append(d, " gauge");
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

data:
      d = NULL;
      d = pgexporter_append(d, "pgexporter_");
      d = pgexporter_append(d, &current->tuple->tag[0]);
      d = pgexporter_append(d, "_");
      d = pgexporter_append(d, &current->tuple->name[0]);
      d = pgexporter_append(d, "{server=\"");
      d = pgexporter_append(d, &config->servers[current->tuple->server].name[0]);
      d = pgexporter_append(d, "\"} ");
      d = pgexporter_append(d, get_value(&current->tuple->tag[0], &current->tuple->name[0], &current->tuple->value[0]));
      d = pgexporter_append(d, "\n");
      data = pgexporter_append(data, d);
      free(d);

      if (current->next != NULL && !strcmp(&current->tuple->name[0], &current->next->tuple->name[0]))
      {
         current = current->next;
         goto data;
      }

      if (data != NULL)
      {
         send_chunk(client_fd, data);
         free(data);
         data = NULL;
      }

      current = current->next;
   }

   pgexporter_free_tuples(all);
}

static int
send_chunk(int client_fd, char* data)
{
   int status;
   char* m = NULL;
   struct message msg;

   memset(&msg, 0, sizeof(struct message));

   m = malloc(20);
   memset(m, 0, 20);

   sprintf(m, "%lX\r\n", strlen(data));

   m = pgexporter_append(m, data);
   m = pgexporter_append(m, "\r\n");
   
   msg.kind = 0;
   msg.length = strlen(m);
   msg.data = m;

   status = pgexporter_write_message(NULL, client_fd, &msg);

   free(m);

   return status;
}

static char*
get_value(char* tag, char* name, char* val)
{
   char* end = NULL;

   /* Empty to 0 */
   if (val == NULL || !strcmp(val, ""))
   {
      return "0";
   }

   /* Bool */
   if (!strcmp(val, "off") || !strcmp(val, "f") || !strcmp(val, "(disabled)"))
   {
      return "0";
   }
   else if (!strcmp(val, "on") || !strcmp(val, "t"))
   {
      return "1";
   }

   if (!strcmp(val, "NaN"))
   {
      return val;
   }

   /* long */
   strtol(val, &end, 10);
   if (*end == '\0')
   {
      return val;
   }
   errno = 0;

   /* double */
   strtod(val, &end);
   if (*end == '\0')
   {
      return val;
   }
   errno = 0;

   pgexporter_log_trace("get_value(%s/%s): %s", tag, name, val);

   /* Map general strings to 1 */
   return "1";
}