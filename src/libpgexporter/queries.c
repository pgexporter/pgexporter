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
#include <logging.h>
#include <management.h>
#include <message.h>
#include <network.h>
#include <queries.h>
#include <security.h>
#include <server.h>
#include <utils.h>

/* system */
#include <stdlib.h>

static int query_execute(int server, char* qs, char* tag, int columns, char* names[], struct query** query);
static void* data_append(void* orig, size_t orig_size, void* n, size_t n_size);
static int create_D_tuple(int server, int number_of_columns, struct message* msg, struct tuple** tuple);
static int get_number_of_columns(struct message* msg);
static int get_column_name(struct message* msg, int index, char** name);

void
pgexporter_open_connections(void)
{
   int ret;
   int user;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd != -1)
      {
         if (!pgexporter_connection_isvalid(config->servers[server].fd))
         {
            pgexporter_disconnect(config->servers[server].fd);
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

         config->servers[server].new = false;

         ret = pgexporter_server_authenticate(server, "postgres",
                                              &config->users[user].username[0], &config->users[user].password[0],
                                              &config->servers[server].fd);
         if (ret == AUTH_SUCCESS)
         {
            config->servers[server].new = true;
            pgexporter_server_info(server);
            pgexporter_server_version(server);
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
   int ret;
   bool nuke;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int server = 0; server < config->number_of_servers; server++)
   {
      if (config->servers[server].fd != -1)
      {
         nuke = true;

         if (config->cache)
         {
            if (config->servers[server].new)
            {
               ret = pgexporter_management_transfer_connection(server);

               if (ret == 0)
               {
                  config->servers[server].new = false;
               }
            }

            if (!config->servers[server].new)
            {
               nuke = false;
            }
         }

         if (nuke)
         {
            pgexporter_write_terminate(NULL, config->servers[server].fd);
            pgexporter_disconnect(config->servers[server].fd);
            config->servers[server].fd = -1;
            config->servers[server].new = false;
            config->servers[server].state = SERVER_UNKNOWN;
         }
      }
   }
}

int
pgexporter_server_version(int server)
{
   struct query* q;
   int ret;
   struct configuration* config;

   config = (struct configuration*)shmem;

   ret = query_execute(server, "SELECT split_part(split_part(version(), ' ', 2), '.', 1);", "version", 1, NULL, &q);

   if (q)
   {
      struct tuple* t = q->tuples;
      if (t && t->data[0])
      {
         config->servers[server].version = atoi(t->data[0]);
      }
   }

   pgexporter_free_query(q);

   return ret;
}

int
pgexporter_query_get_functions(int server, struct query** query)
{
   char* d = NULL;
   int ret;

   d = pgexporter_append(d, "SELECT * FROM pgexporter_get_functions();");

   ret = pgexporter_query_execute(server, d, "pgexporter_ext", query);

   free(d);

   return ret;
}

int
pgexporter_query_execute(int server, char* sql, char* tag, struct query** query)
{
   return query_execute(server, sql, tag, -1, NULL, query);
}

int
pgexporter_query_used_disk_space(int server, bool data, struct query** query)
{
   char* d = NULL;
   int ret;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgexporter_append(d, "SELECT * FROM pgexporter_used_space(\'");
   if (data)
   {
      d = pgexporter_append(d, &config->servers[server].data[0]);
   }
   else
   {
      d = pgexporter_append(d, &config->servers[server].wal[0]);
   }
   d = pgexporter_append(d, "\');");

   ret = query_execute(server, d, "pgexporter_ext", 1, NULL, query);

   free(d);

   return ret;
}

int
pgexporter_query_free_disk_space(int server, bool data, struct query** query)
{
   char* d = NULL;
   int ret;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgexporter_append(d, "SELECT * FROM pgexporter_free_space(\'");
   if (data)
   {
      d = pgexporter_append(d, &config->servers[server].data[0]);
   }
   else
   {
      d = pgexporter_append(d, &config->servers[server].wal[0]);
   }
   d = pgexporter_append(d, "\');");

   ret = query_execute(server, d, "pgexporter_ext", 1, NULL, query);

   free(d);

   return ret;
}

int
pgexporter_query_total_disk_space(int server, bool data, struct query** query)
{
   char* d = NULL;
   int ret;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgexporter_append(d, "SELECT * FROM pgexporter_total_space(\'");
   if (data)
   {
      d = pgexporter_append(d, &config->servers[server].data[0]);
   }
   else
   {
      d = pgexporter_append(d, &config->servers[server].wal[0]);
   }
   d = pgexporter_append(d, "\');");

   ret = query_execute(server, d, "pgexporter_ext", 1, NULL, query);

   free(d);

   return ret;
}

int
pgexporter_query_version(int server, struct query** query)
{
   return query_execute(server, "SELECT version();",
                        "pg_version", 1, NULL, query);
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
      "maxwritten_clean"
   };

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
      "numbackends"
   };

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
      "confl_deadlock"
   };

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
   }
   else
   {
      while (ct1 != NULL)
      {
         if (ct2 != NULL && !strcmp(ct1->data[0], ct2->data[0]))
         {
            while (ct1->next != NULL && !strcmp(ct1->next->data[0], ct2->data[0]))
            {
               ct1 = ct1->next;
            }

            tmp1 = ct1->next;
            tmp2 = ct2->next;

            ct1->next = ct2;
            ct2->next = tmp1;
            ct2 = tmp2;
         }

         last = ct1;
         ct1 = ct1->next;
      }
   }

   while (ct2 != NULL)
   {
      last->next = ct2;

      last = last->next;
      ct2 = ct2->next;
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

   config = (struct configuration*)shmem;

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

   status = pgexporter_write_message(NULL, config->servers[server].fd, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   cont = true;
   while (cont)
   {
      status = pgexporter_read_block_message(NULL, config->servers[server].fd, &msg);

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

      pgexporter_free_message(msg);
      msg = NULL;
   }

   if (pgexporter_has_message('E', data, data_size))
   {
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
   memcpy(&q->tag[0], tag, strlen(tag));

   for (int i = 0; i < cols; i++)
   {
      if (names != NULL)
      {
         memcpy(&q->names[i][0], names[i], strlen(names[i]));
      }
      else
      {
         if (get_column_name(tmsg, i, &name))
         {
            goto error;
         }

         memcpy(&q->names[i][0], name, strlen(name));

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

      pgexporter_free_copy_message(msg);
      msg = NULL;
   }

   *query = q;

   pgexporter_free_copy_message(tmsg);

   free(content);
   free(data);

   return 0;

error:

   pgexporter_free_message(msg);
   pgexporter_free_copy_message(tmsg);
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
