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
#include <connection.h>
#include <deque.h>
#include <extension.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <queries.h>
#include <security.h>
#include <server.h>
#include <string.h>
#include <utils.h>

/* system */
#include <stdlib.h>

#define SQLSTATE_QUERY_CANCELED "57014"

static int query_execute(int server, char* qs, char* tag, int columns, char* names[], struct query** query);
static bool is_query_timeout_error(struct message* error_msg);
static void* data_append(void* orig, size_t orig_size, void* n, size_t n_size);
static int create_D_tuple(int server, int number_of_columns, struct message* msg, struct tuple** tuple);
static int get_number_of_columns(struct message* msg);
static int get_column_name(struct message* msg, int index, char** name);
static int process_server_parameters(int server, struct deque* server_parameters);
static int pgexporter_detect_databases(int server);
static int pgexporter_detect_extensions(int server);
static int pgexporter_connect_db(int server, char* database);
static void pgexporter_apply_metrics_timeout(int server);

int
pgexporter_check_pg_monitor_role(int server)
{
   struct query* q = NULL;
   struct configuration* config;
   int ret = 1;

   config = (struct configuration*)shmem;

   if (config->servers[server].fd == -1)
   {
      pgexporter_log_error("Cannot check pg_monitor role: no active connection to server '%s'",
                           &config->servers[server].name[0]);
      return 1;
   }

   if (pgexporter_query_execute(server,
                                "SELECT pg_has_role(current_user, 'pg_monitor', 'USAGE') AS has_pg_monitor;",
                                "pg_monitor_check", &q) == 0 &&
       q != NULL)
   {
      if (q->tuples != NULL && q->tuples->data != NULL && q->tuples->data[0] != NULL)
      {
         if (strcmp(q->tuples->data[0], "t") == 0)
         {
            ret = 0;
            pgexporter_log_debug("User has pg_monitor role on server '%s'", &config->servers[server].name[0]);
         }
         else
         {
            pgexporter_log_error("User '%s' lacks pg_monitor role on server '%s'. "
                                 "Grant pg_monitor role: GRANT pg_monitor TO %s;",
                                 &config->servers[server].username[0],
                                 &config->servers[server].name[0],
                                 &config->servers[server].username[0]);
            ret = 1;
         }
      }
      else
      {
         pgexporter_log_error("Failed to check pg_monitor role on server '%s': empty result",
                              &config->servers[server].name[0]);
         ret = 1;
      }

      pgexporter_free_query(q);
   }
   else
   {
      pgexporter_log_error("Failed to execute pg_monitor role check query on server '%s'",
                           &config->servers[server].name[0]);
      ret = 1;
   }

   return ret;
}

void
pgexporter_open_connections(void)
{
   int ret;
   int user;
   struct configuration* config;
   struct deque* server_parameters;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].type == SERVER_TYPE_PROMETHEUS)
      {
         continue;
      }

      if (config->servers[server].fd != -1)
      {
         if (!pgexporter_connection_isvalid(config->servers[server].ssl, config->servers[server].fd))
         {
            pgexporter_disconnect(config->servers[server].fd);
            if (config->servers[server].ssl != NULL)
            {
               pgexporter_close_ssl(config->servers[server].ssl);
               config->servers[server].ssl = NULL;
            }
            config->servers[server].fd = -1;
         }
      }

      if (config->servers[server].fd == -1)
      {
         user = -1;
         for (int usr = 0; user == -1 && usr < config->number_of_users; usr++)
         {
            if (!strcmp(&config->users[usr].username[0], &config->servers[server].username[0]))
            {
               user = usr;
            }
         }
         if (user == -1)
         {
            pgexporter_log_error("No user '%s' configured for server '%s'",
                                 &config->servers[server].username[0],
                                 &config->servers[server].name[0]);
            continue;
         }

         config->servers[server].new = false;

         ret = pgexporter_server_authenticate(server, "postgres",
                                              &config->users[user].username[0], &config->users[user].password[0],
                                              &config->servers[server].ssl,
                                              &config->servers[server].fd);
         if (ret == AUTH_SUCCESS)
         {
            config->servers[server].new = true;
            pgexporter_server_info(server);
            if (!pgexporter_extract_server_parameters(&server_parameters))
            {
               process_server_parameters(server, server_parameters);
               pgexporter_deque_destroy(server_parameters);
            }

            if (pgexporter_check_pg_monitor_role(server) != 0)
            {
               pgexporter_log_fatal("Server '%s': pg_monitor role check failed. pgexporter cannot function without proper permissions.",
                                    &config->servers[server].name[0]);
               if (config->servers[server].ssl != NULL)
               {
                  pgexporter_close_ssl(config->servers[server].ssl);
                  config->servers[server].ssl = NULL;
               }
               pgexporter_disconnect(config->servers[server].fd);
               config->servers[server].fd = -1;
               config->servers[server].new = false;
               config->servers[server].state = SERVER_UNKNOWN;
               pgexporter_close_connections();
               exit(1);
            }

            pgexporter_detect_databases(server);
            pgexporter_detect_extensions(server);

            pgexporter_apply_metrics_timeout(server);
         }
         else
         {
            pgexporter_log_error("Failed login for '%s' on server '%s'", &config->users[user].username, &config->servers[server].name);
         }
      }
   }
}

void
pgexporter_close_connections(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd != -1)
      {
         pgexporter_write_terminate(config->servers[server].ssl, config->servers[server].fd);

         if (config->servers[server].ssl != NULL)
         {
            pgexporter_close_ssl(config->servers[server].ssl);
            config->servers[server].ssl = NULL;
         }

         pgexporter_disconnect(config->servers[server].fd);
         config->servers[server].fd = -1;
         config->servers[server].new = false;
         config->servers[server].state = SERVER_UNKNOWN;
      }
   }
}

int
pgexporter_query_execute(int server, char* sql, char* tag, struct query** query)
{
   return query_execute(server, sql, tag, -1, NULL, query);
}

int
pgexporter_execute_command(int server, char* sql)
{
   int status;
   bool cont = true;
   struct message qmsg = {0};
   struct message* msg = NULL;
   size_t size;
   char* content = NULL;
   void* data = NULL;
   size_t data_size = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   size = 1 + 4 + strlen(sql) + 1;
   content = (char*)malloc(size);
   memset(content, 0, size);

   pgexporter_write_byte(content, 'Q');
   pgexporter_write_int32(content + 1, size - 1);
   pgexporter_write_string(content + 5, sql);

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = content;

   status = pgexporter_write_message(config->servers[server].ssl, config->servers[server].fd, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_log_error("pgexporter_execute_command: failed to write message");
      goto error;
   }

   while (cont)
   {
      status = pgexporter_read_block_message(config->servers[server].ssl, config->servers[server].fd, &msg);

      if (status == MESSAGE_STATUS_OK)
      {
         data = data_append(data, data_size, msg->data, msg->length);
         data_size += msg->length;

         if (pgexporter_has_message('Z', data, data_size))
         {
            cont = false;
         }
      }
      else
      {
         pgexporter_log_error("pgexporter_execute_command: failed to read message, status=%d", status);
         goto error;
      }

      pgexporter_clear_message();
      msg = NULL;
   }

   /* Check for errors */
   if (pgexporter_has_message('E', data, data_size))
   {
      pgexporter_log_error("pgexporter_execute_command: found error message in response");
      goto error;
   }

   if (!pgexporter_has_message('C', data, data_size))
   {
      pgexporter_log_error("pgexporter_execute_command: no CommandComplete message found");
      goto error;
   }

   free(content);
   free(data);

   return 0;

error:
   pgexporter_log_error("pgexporter_execute_command: command failed");

   pgexporter_clear_message();
   free(content);
   free(data);

   return 1;
}

int
pgexporter_query_version(int server, struct query** query)
{
   return query_execute(server, "SELECT split_part(split_part(version(), ' ', 2), '.', 1) AS major, "
                                "split_part(split_part(version(), ' ', 2), '.', 2) AS minor;",
                        "pg_version",
                        2, NULL, query);
}

int
pgexporter_query_uptime(int server, struct query** query)
{
   return query_execute(server, "SELECT FLOOR(EXTRACT(EPOCH FROM now() - pg_postmaster_start_time)) FROM pg_postmaster_start_time();",
                        "pg_uptime", 1, NULL, query);
}

int
pgexporter_query_primary(int server, struct query** query)
{
   return query_execute(server, "SELECT (CASE pg_is_in_recovery() WHEN 'f' THEN 't' ELSE 'f' END);",
                        "pg_primary", 1, NULL, query);
}

int
pgexporter_query_database_size(int server, struct query** query)
{
   return query_execute(server, "SELECT datname, pg_database_size(datname) FROM pg_database;",
                        "pg_database", 2, NULL, query);
}

int
pgexporter_query_database_list(int server, struct query** query)
{
   return query_execute(server, "SELECT datname "
                                "FROM pg_database "
                                "WHERE datistemplate = false AND datname != 'postgres';",
                        "pg_db_list",
                        1, NULL, query);
}

int
pgexporter_query_extensions_list(int server, struct query** query)
{
   return query_execute(server, "SELECT name, installed_version, comment "
                                "FROM pg_available_extensions "
                                "WHERE installed_version IS NOT NULL "
                                "ORDER BY name;",
                        "pg_extensions_list",
                        3, NULL, query);
}

int
pgexporter_query_replication_slot_active(int server, struct query** query)
{
   return query_execute(server, "SELECT slot_name,active FROM pg_replication_slots;",
                        "pg_replication_slots", 2, NULL, query);
}

int
pgexporter_query_locks(int server, struct query** query)
{
   return query_execute(server,
                        "SELECT pg_database.datname as database, tmp.mode, COALESCE(count, 0) as count "
                        "FROM "
                        "("
                        " VALUES ('accesssharelock'),"
                        "        ('rowsharelock'),"
                        "        ('rowexclusivelock'),"
                        "        ('shareupdateexclusivelock'),"
                        "        ('sharelock'),"
                        "        ('sharerowexclusivelock'),"
                        "        ('exclusivelock'),"
                        "        ('accessexclusivelock'),"
                        "        ('sireadlock')"
                        ") AS tmp(mode) CROSS JOIN pg_database "
                        "LEFT JOIN "
                        "(SELECT database, lower(mode) AS mode, count(*) AS count "
                        " FROM pg_locks WHERE database IS NOT NULL "
                        " GROUP BY database, lower(mode) "
                        ") AS tmp2 "
                        "ON tmp.mode = tmp2.mode and pg_database.oid = tmp2.database ORDER BY 1, 2;",
                        "pg_locks", 3, NULL, query);
}

int
pgexporter_query_stat_bgwriter(int server, struct query** query)
{
   char* names[] = {
      "buffers_alloc",
      "buffers_backend",
      "buffers_backend_fsync",
      "buffers_checkpoint",
      "buffers_clean",
      "checkpoint_sync_time",
      "checkpoint_write_time",
      "checkpoints_req",
      "checkpoints_timed",
      "maxwritten_clean"};

   return query_execute(server,
                        "SELECT buffers_alloc, buffers_backend, buffers_backend_fsync, "
                        "buffers_checkpoint, buffers_clean, checkpoint_sync_time, "
                        "checkpoint_write_time, checkpoints_req, checkpoints_timed, "
                        "maxwritten_clean "
                        "FROM pg_stat_bgwriter;",
                        "pg_stat_bgwriter", 10, names, query);
}

int
pgexporter_query_stat_database(int server, struct query** query)
{
   char* names[] = {
      "database",
      "blk_read_time",
      "blk_write_time",
      "blks_hit",
      "blks_read",
      "deadlocks",
      "temp_files",
      "temp_bytes",
      "tup_returned",
      "tup_fetched",
      "tup_inserted",
      "tup_updated",
      "tup_deleted",
      "xact_commit",
      "xact_rollback",
      "conflicts",
      "numbackends"};

   return query_execute(server,
                        "SELECT datname, blk_read_time, blk_write_time, "
                        "blks_hit, blks_read, "
                        "deadlocks, temp_files, temp_bytes, "
                        "tup_returned, tup_fetched, tup_inserted, "
                        "tup_updated, tup_deleted, xact_commit, "
                        "xact_rollback, conflicts, numbackends "
                        "FROM pg_stat_database WHERE datname IS NOT NULL ORDER BY datname;",
                        "pg_stat_database", 17, names, query);
}

int
pgexporter_query_stat_database_conflicts(int server, struct query** query)
{
   char* names[] = {
      "database",
      "confl_tablespace",
      "confl_lock",
      "confl_snapshot",
      "confl_bufferpin",
      "confl_deadlock"};

   return query_execute(server,
                        "SELECT datname, confl_tablespace, confl_lock, "
                        "confl_snapshot, confl_bufferpin, confl_deadlock "
                        "FROM pg_stat_database_conflicts WHERE datname IS NOT NULL ORDER BY datname;",
                        "pg_stat_database_conflicts", 6, names, query);
}

int
pgexporter_query_settings(int server, struct query** query)
{
   return query_execute(server, "SELECT name,setting,short_desc FROM pg_settings;",
                        "pg_settings", 3, NULL, query);
}

int
pgexporter_custom_query(int server, char* qs, char* tag, int columns, char** names, struct query** query)
{
   return query_execute(server, qs, tag, columns, names, query);
}

struct query*
pgexporter_merge_queries(struct query* q1, struct query* q2, int sort)
{
   struct tuple* last = NULL;
   struct tuple* ct1 = NULL;
   struct tuple* ct2 = NULL;
   struct tuple* tmp1 = NULL;
   struct tuple* tmp2 = NULL;

   if (q1 == NULL)
   {
      return q2;
   }

   if (q2 == NULL)
   {
      return q1;
   }

   ct1 = q1->tuples;
   ct2 = q2->tuples;

   if (sort == SORT_NAME)
   {
      while (ct1 != NULL)
      {
         last = ct1;
         ct1 = ct1->next;
      }

      last->next = ct2;
   }
   else
   {
      if (ct1 != NULL)
      {
         while (ct1 != NULL && ct2 != NULL)
         {
            tmp1 = ct1;

            if (strcmp(tmp1->data[0], ct2->data[0]))
            {
               while (tmp1 != NULL && tmp1->next != NULL && strcmp(tmp1->next->data[0], ct2->data[0]))
               {
                  tmp1 = tmp1->next;
               }
            }
            while (tmp1 != NULL && tmp1->next != NULL && !strcmp(tmp1->next->data[0], ct2->data[0]))
            {
               tmp1 = tmp1->next;
            }

            if (tmp1 == NULL)
            {
               continue;
            }

            tmp2 = ct2->next;

            ct2->next = tmp1->next;
            tmp1->next = ct2;
            ct2 = tmp2;
         }
      }
      else
      {
         ct1 = ct2;
         ct2 = NULL;
      }
   }

   q2->tuples = NULL;
   pgexporter_free_query(q2);

   return q1;
}

int
pgexporter_free_query(struct query* query)
{
   if (query != NULL)
   {
      pgexporter_free_tuples(&query->tuples, query->number_of_columns);
      free(query);
   }

   return 0;
}

int
pgexporter_free_tuples(struct tuple** tuples, int n_columns)
{
   struct tuple* next = NULL;
   struct tuple* current = NULL;
   current = *tuples;

   while (current != NULL)
   {
      next = current->next;

      for (int i = 0; i < n_columns; i++)
      {
         free(current->data[i]);
      }
      free(current->data);
      free(current);

      current = next;
   }

   return 0;
}

char*
pgexporter_get_column(int col, struct tuple* tuple)
{
   return tuple->data[col];
}

void
pgexporter_query_debug(struct query* query)
{
   int number_of_tuples = 0;
   struct tuple* t = NULL;

   if (query == NULL)
   {
      pgexporter_log_info("Query is NULL");
      return;
   }

   pgexporter_log_trace("Query: %s", query->tag);
   pgexporter_log_trace("Columns: %d", query->number_of_columns);

   for (int i = 0; i < query->number_of_columns; i++)
   {
      pgexporter_log_trace("Column: %s", query->names[i]);
   }

   t = query->tuples;
   while (t != NULL)
   {
      number_of_tuples++;
      t = t->next;
   }

   pgexporter_log_trace("Tuples: %d", number_of_tuples);
}

char*
pgexporter_get_column_by_name(char* name, struct query* query, struct tuple* tuple)
{
   for (int i = 0; i < query->number_of_columns; i++)
   {
      if (!strcmp(query->names[i], name))
      {
         return pgexporter_get_column(i, tuple);
      }
   }

   return NULL;
}

static bool
is_query_timeout_error(struct message* error_msg)
{
   bool is_timeout = false;
   if (error_msg != NULL && error_msg->length > 5)
   {
      char* payload = (char*)error_msg->data;
      size_t offset = 5; /* kind (1) + length (4) */

      while (offset < error_msg->length)
      {
         char field_type = payload[offset];
         if (field_type == '\0')
         {
            break;
         }

         char* value = pgexporter_read_string(payload + offset + 1);

         if (field_type == 'C')
         {
            if (!strcmp(value, SQLSTATE_QUERY_CANCELED))
            {
               is_timeout = true;
               break;
            }
         }
         else if (field_type == 'M')
         {
            if (strstr(value, "statement timeout") != NULL || strstr(value, "canceling statement due to user request") != NULL)
            {
               is_timeout = true;
               break;
            }
         }

         offset += 1 + strlen(value) + 1;
      }
   }

   return is_timeout;
}

static int
query_execute(int server, char* qs, char* tag, int columns, char* names[], struct query** query)
{
   int status;
   bool cont;
   int cols;
   char* name = NULL;
   struct message qmsg = {0};
   struct message* tmsg = NULL;
   size_t size = 0;
   char* content = NULL;
   struct message* msg = NULL;
   struct query* q = NULL;
   struct tuple* current = NULL;
   void* data = NULL;
   size_t data_size = 0;
   size_t offset = 0;
   struct configuration* config;
   bool query_timeout = false;

   config = (struct configuration*)shmem;

   atomic_fetch_add(&config->query_executions_total, 1);

   *query = NULL;

   memset(&qmsg, 0, sizeof(struct message));

   size = 1 + 4 + strlen(qs) + 1;
   content = (char*)malloc(size);
   memset(content, 0, size);

   pgexporter_write_byte(content, 'Q');
   pgexporter_write_int32(content + 1, size - 1);
   pgexporter_write_string(content + 5, qs);

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = content;

   status = pgexporter_write_message(config->servers[server].ssl, config->servers[server].fd, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   cont = true;
   while (cont)
   {
      status = pgexporter_read_block_message(config->servers[server].ssl, config->servers[server].fd, &msg);

      if (status == MESSAGE_STATUS_OK)
      {
         data = data_append(data, data_size, msg->data, msg->length);
         data_size += msg->length;

         if (pgexporter_has_message('Z', data, data_size))
         {
            cont = false;
         }
      }
      else
      {
         goto error;
      }

      pgexporter_clear_message();
      msg = NULL;
   }

   if (pgexporter_has_message('E', data, data_size))
   {
      struct message* error_msg = NULL;
      if (!pgexporter_extract_message_from_data('E', data, data_size, &error_msg))
      {
         query_timeout = is_query_timeout_error(error_msg);
         pgexporter_free_message(error_msg);
      }
      goto error;
   }

   if (pgexporter_extract_message_from_data('T', data, data_size, &tmsg))
   {
      goto error;
   }

   if (columns <= 0)
   {
      cols = get_number_of_columns(tmsg);
   }
   else
   {
      cols = columns;
   }

   q = (struct query*)malloc(sizeof(struct query));
   memset(q, 0, sizeof(struct query));

   q->number_of_columns = cols;
   pgexporter_snprintf(&q->tag[0], PROMETHEUS_LENGTH, "%s", tag);

   for (int i = 0; i < cols; i++)
   {
      if (names != NULL)
      {
         pgexporter_snprintf(&q->names[i][0], PROMETHEUS_LENGTH, "%s", names[i]);
      }
      else
      {
         if (get_column_name(tmsg, i, &name))
         {
            goto error;
         }

         pgexporter_snprintf(&q->names[i][0], PROMETHEUS_LENGTH, "%s", name);

         free(name);
         name = NULL;
      }
   }

   while (offset < data_size)
   {
      offset = pgexporter_extract_message_offset(offset, data, &msg);

      if (msg != NULL && msg->kind == 'D')
      {
         struct tuple* dtuple = NULL;

         create_D_tuple(server, cols, msg, &dtuple);

         if (q->tuples == NULL)
         {
            q->tuples = dtuple;
         }
         else
         {
            current->next = dtuple;
         }

         current = dtuple;
      }

      pgexporter_free_message(msg);
      msg = NULL;
   }

   *query = q;

   pgexporter_free_message(tmsg);

   free(content);
   free(data);

   return 0;

error:
   atomic_fetch_add(&config->query_errors_total, 1);
   if (query_timeout)
   {
      atomic_fetch_add(&config->query_timeouts_total, 1);
   }
   if (q != NULL)
   {
      pgexporter_free_query(q);
   }
   pgexporter_clear_message();
   pgexporter_free_message(tmsg);
   free(content);
   free(data);

   return 1;
}

static void*
data_append(void* orig, size_t orig_size, void* n, size_t n_size)
{
   void* d = NULL;

   if (n != NULL)
   {
      d = realloc(orig, orig_size + n_size);
      memcpy(d + orig_size, n, n_size);
   }

   return d;
}

static int
create_D_tuple(int server, int number_of_columns, struct message* msg, struct tuple** tuple)
{
   int offset;
   int length;
   struct tuple* result = NULL;

   result = (struct tuple*)malloc(sizeof(struct tuple));
   memset(result, 0, sizeof(struct tuple));

   result->server = server;
   result->data = (char**)malloc(number_of_columns * sizeof(char*));
   result->next = NULL;

   offset = 7;

   for (int i = 0; i < number_of_columns; i++)
   {
      length = pgexporter_read_int32(msg->data + offset);
      offset += 4;

      if (length > 0)
      {
         result->data[i] = (char*)malloc(length + 1);
         memset(result->data[i], 0, length + 1);
         memcpy(result->data[i], msg->data + offset, length);
         offset += length;
      }
      else
      {
         result->data[i] = NULL;
      }
   }

   *tuple = result;

   return 0;
}

static int
get_number_of_columns(struct message* msg)
{
   if (msg->kind == 'T')
   {
      return pgexporter_read_int16(msg->data + 5);
   }

   return 0;
}

static int
get_column_name(struct message* msg, int index, char** name)
{
   int current = 0;
   int offset;
   int16_t cols;
   char* tmp = NULL;

   *name = NULL;

   if (msg->kind == 'T')
   {
      cols = pgexporter_read_int16(msg->data + 5);

      if (index < cols)
      {
         offset = 7;

         while (current < index)
         {
            tmp = pgexporter_read_string(msg->data + offset);

            offset += strlen(tmp) + 1;
            offset += 4;
            offset += 2;
            offset += 4;
            offset += 2;
            offset += 4;
            offset += 2;

            current++;
         }

         tmp = pgexporter_read_string(msg->data + offset);

         *name = pgexporter_append(*name, tmp);

         return 0;
      }
   }

   return 1;
}

static int
process_server_parameters(int server, struct deque* server_parameters)
{
   int status = 0;
   int major = 0;
   int minor = 0;
   struct deque_iterator* iter = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   config->servers[server].version = 0;
   config->servers[server].minor_version = 0;

   pgexporter_deque_iterator_create(server_parameters, &iter);
   while (pgexporter_deque_iterator_next(iter))
   {
      pgexporter_log_trace("%s/process server_parameter '%s'", config->servers[server].name, iter->tag);
      if (!strcmp("server_version", iter->tag))
      {
         char* server_version = pgexporter_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         if (sscanf(server_version, "%d.%d", &major, &minor) == 2)
         {
            config->servers[server].version = major;
            config->servers[server].minor_version = minor;
         }
         else
         {
            pgexporter_log_error("Unable to parse server_version '%s' for %s",
                                 server_version, config->servers[server].name);
            status = 1;
         }
         free(server_version);
      }
   }

   pgexporter_deque_iterator_destroy(iter);
   return status;
}

static int
pgexporter_detect_extensions(int server)
{
   int ret;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config;
   int extension_idx;

   config = (struct configuration*)shmem;

   config->servers[server].number_of_extensions = 0;

   ret = pgexporter_query_extensions_list(server, &query);
   if (ret != 0)
   {
      pgexporter_log_warn("Failed to detect extensions for server %s", config->servers[server].name);
      goto error;
   }

   current = query->tuples;
   while (current != NULL)
   {
      if (config->servers[server].number_of_extensions >= NUMBER_OF_EXTENSIONS)
      {
         pgexporter_log_warn("Maximum number of extensions reached for server %s (%d)",
                             config->servers[server].name, NUMBER_OF_EXTENSIONS);
         goto error;
      }

      extension_idx = config->servers[server].number_of_extensions;

      pgexporter_snprintf(config->servers[server].extensions[extension_idx].name,
                          MISC_LENGTH, "%s", pgexporter_get_column(0, current));
      config->servers[server].extensions[extension_idx].name[MISC_LENGTH - 1] = '\0';

      if (pgexporter_parse_extension_version(pgexporter_get_column(1, current),
                                             &config->servers[server].extensions[extension_idx].installed_version))
      {
         pgexporter_log_warn("Failed to parse extension version '%s' for %s on server %s",
                             pgexporter_get_column(1, current),
                             config->servers[server].extensions[extension_idx].name,
                             config->servers[server].name);
         config->servers[server].extensions[extension_idx].enabled = false;
      }
      else
      {
         config->servers[server].extensions[extension_idx].enabled =
            pgexporter_extension_is_enabled(config, server, config->servers[server].extensions[extension_idx].name);
         pgexporter_log_debug("Extension '%s' on server '%s': %s",
                              config->servers[server].extensions[extension_idx].name,
                              config->servers[server].name,
                              config->servers[server].extensions[extension_idx].enabled ? "ENABLED" : "DISABLED");
      }

      pgexporter_snprintf(config->servers[server].extensions[extension_idx].comment,
                          MISC_LENGTH, "%s", pgexporter_get_column(2, current));
      config->servers[server].extensions[extension_idx].comment[MISC_LENGTH - 1] = '\0';

      config->servers[server].number_of_extensions++;
      current = current->next;
   }

   pgexporter_log_debug("Server %s: Detected extensions:", config->servers[server].name);
   for (int i = 0; i < config->servers[server].number_of_extensions; i++)
   {
      pgexporter_log_debug("  - %s (version %d.%d.%d) - %s",
                           config->servers[server].extensions[i].name,
                           config->servers[server].extensions[i].installed_version.major,
                           config->servers[server].extensions[i].installed_version.minor,
                           config->servers[server].extensions[i].installed_version.patch,
                           config->servers[server].extensions[i].comment);
   }

   pgexporter_free_query(query);
   return 0;

error:
   pgexporter_free_query(query);
   return 1;
}

static int
pgexporter_detect_databases(int server)
{
   int ret;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config;
   int db_idx;

   config = (struct configuration*)shmem;

   config->servers[server].number_of_databases = 0;

   ret = pgexporter_query_database_list(server, &query);
   if (ret != 0)
   {
      pgexporter_log_warn("Failed to detect databases for server %s", config->servers[server].name);
      goto error;
   }

   db_idx = config->servers[server].number_of_databases;

   current = query->tuples;
   while (current != NULL)
   {
      if (config->servers[server].number_of_databases >= NUMBER_OF_DATABASES)
      {
         pgexporter_log_warn("Maximum number of databases reached for server %s (%d)",
                             config->servers[server].name, NUMBER_OF_DATABASES);
         goto error;
      }

      db_idx = config->servers[server].number_of_databases;

      pgexporter_snprintf((char*)&config->servers[server].databases[db_idx],
                          DB_NAME_LENGTH, "%s", pgexporter_get_column(0, current));

      config->servers[server].number_of_databases++;
      current = current->next;
   }

   db_idx = config->servers[server].number_of_databases;
   strcpy((char*)&config->servers[server].databases[db_idx], "postgres");
   config->servers[server].number_of_databases++;

   pgexporter_log_debug("Server %s: Detected databases:", config->servers[server].name);
   for (int i = 0; i < config->servers[server].number_of_databases; i++)
   {
      pgexporter_log_debug("  - %s", config->servers[server].databases[i]);
   }

   pgexporter_free_query(query);
   return 0;

error:
   pgexporter_free_query(query);
   return 1;
}

static int
pgexporter_connect_db(int server, char* database)
{
   int user;
   struct configuration* config;

   config = (struct configuration*)shmem;

   user = -1;
   for (int usr = 0; user == -1 && usr < config->number_of_users; usr++)
   {
      if (!strcmp(&config->users[usr].username[0], &config->servers[server].username[0]))
      {
         user = usr;
      }
   }
   if (user == -1)
   {
      pgexporter_log_error("No user '%s' configured for server '%s'",
                           &config->servers[server].username[0],
                           &config->servers[server].name[0]);
      return AUTH_ERROR;
   }

   int ret = pgexporter_server_authenticate(server, database == NULL ? "postgres" : database,
                                            &config->users[user].username[0], &config->users[user].password[0],
                                            &config->servers[server].ssl,
                                            &config->servers[server].fd);
   if (ret == 0)
   {
      pgexporter_apply_metrics_timeout(server);
   }
   return ret;
}

int
pgexporter_switch_db(int server, char* database)
{
   int ret;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->servers[server].fd != -1)
   {
      pgexporter_write_terminate(config->servers[server].ssl, config->servers[server].fd);
      if (config->servers[server].ssl != NULL)
      {
         pgexporter_close_ssl(config->servers[server].ssl);
         pgexporter_disconnect(config->servers[server].fd);
      }
      else
      {
         pgexporter_disconnect(config->servers[server].fd);
      }
      config->servers[server].ssl = NULL;
      config->servers[server].fd = -1;
   }

   ret = pgexporter_connect_db(server, database);
   if (ret != 0)
   {
      goto error;
   }

   return 0;

error:
   return ret;
}

static void
pgexporter_apply_metrics_timeout(int server)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (pgexporter_time_is_valid(config->metrics_query_timeout))
   {
      char* set_query = pgexporter_append(NULL, "SET statement_timeout = ");
      set_query = pgexporter_append_int(set_query, (int)pgexporter_time_convert(config->metrics_query_timeout, FORMAT_TIME_MS));
      set_query = pgexporter_append(set_query, ";");
      if (pgexporter_execute_command(server, set_query) != 0)
      {
         pgexporter_log_debug("Failed to set statement_timeout=%" PRId64 "ms on server '%s'",
                              pgexporter_time_convert(config->metrics_query_timeout, FORMAT_TIME_MS), &config->servers[server].name[0]);
      }
      else
      {
         pgexporter_log_debug("Set statement_timeout=%" PRId64 "ms on server '%s'",
                              pgexporter_time_convert(config->metrics_query_timeout, FORMAT_TIME_MS), &config->servers[server].name[0]);
      }
      free(set_query);
   }
}
