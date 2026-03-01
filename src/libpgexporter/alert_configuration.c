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
#include <alert_configuration.h>
#include <internal.h>
#include <logging.h>
#include <utils.h>

/* system */
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

static enum alert_operator parse_alert_operator(const char* str);
static enum alert_type parse_alert_type_string(const char* str);
static int parse_alerts_yaml(FILE* file, struct configuration* config, bool merge);

#define ALERT_OVERRIDE_DESCRIPTION 0x01
#define ALERT_OVERRIDE_QUERY       0x02
#define ALERT_OVERRIDE_TYPE        0x04
#define ALERT_OVERRIDE_OPERATOR    0x08
#define ALERT_OVERRIDE_THRESHOLD   0x10
#define ALERT_OVERRIDE_SERVERS     0x20

static enum alert_operator
parse_alert_operator(const char* str)
{
   if (!strcmp(str, ">"))
   {
      return ALERT_OPERATOR_GT;
   }
   else if (!strcmp(str, "<"))
   {
      return ALERT_OPERATOR_LT;
   }
   else if (!strcmp(str, ">="))
   {
      return ALERT_OPERATOR_GE;
   }
   else if (!strcmp(str, "<="))
   {
      return ALERT_OPERATOR_LE;
   }
   else if (!strcmp(str, "=="))
   {
      return ALERT_OPERATOR_EQ;
   }
   else if (!strcmp(str, "!="))
   {
      return ALERT_OPERATOR_NE;
   }

   pgexporter_log_error("Unknown alert operator: %s", str);
   return ALERT_OPERATOR_GT;
}

static enum alert_type
parse_alert_type_string(const char* str)
{
   if (!strcmp(str, "query"))
   {
      return ALERT_TYPE_QUERY;
   }
   else if (!strcmp(str, "connection"))
   {
      return ALERT_TYPE_CONNECTION;
   }

   pgexporter_log_error("Unknown alert type: %s", str);
   return ALERT_TYPE_QUERY;
}

static int
parse_alerts_yaml(FILE* file, struct configuration* config, bool merge)
{
   yaml_parser_t parser;
   yaml_event_t event;
   int done = 0;
   int in_alert_list = 0;
   int in_alert_item = 0;
   int in_servers_seq = 0;
   char* current_key = NULL;
   struct alert_definition current_alert;
   int alert_idx;
   uint8_t overrides = 0;

   if (!yaml_parser_initialize(&parser))
   {
      pgexporter_log_error("Failed to initialize YAML parser for alerts");
      return 1;
   }

   yaml_parser_set_input_file(&parser, file);

   memset(&current_alert, 0, sizeof(struct alert_definition));

   while (!done)
   {
      if (!yaml_parser_parse(&parser, &event))
      {
         pgexporter_log_error("YAML parse error in alerts file");
         yaml_parser_delete(&parser);
         if (current_key)
         {
            free(current_key);
         }
         return 1;
      }

      switch (event.type)
      {
         case YAML_SCALAR_EVENT:
         {
            char* val = (char*)event.data.scalar.value;

            if (in_servers_seq)
            {
               /* Inside servers: sequence - "all" anywhere means target all */
               if (!strcmp(val, "all"))
               {
                  current_alert.servers_all = true;
                  current_alert.number_of_servers = 0;
                  memset(current_alert.servers, 0, sizeof(current_alert.servers));
               }
               else if (!current_alert.servers_all && current_alert.number_of_servers < NUMBER_OF_SERVERS)
               {
                  memset(current_alert.servers[current_alert.number_of_servers], 0, MISC_LENGTH);
                  memcpy(current_alert.servers[current_alert.number_of_servers], val,
                         MIN((int)strlen(val), MISC_LENGTH - 1));
                  current_alert.number_of_servers++;
               }
            }
            else if (!in_alert_list && !strcmp(val, "alerts"))
            {
               in_alert_list = 1;
            }
            else if (in_alert_item && current_key == NULL)
            {
               current_key = strdup(val);
            }
            else if (in_alert_item && current_key != NULL)
            {
               if (!strcmp(current_key, "name"))
               {
                  memset(current_alert.name, 0, PROMETHEUS_LENGTH);
                  memcpy(current_alert.name, val, MIN((int)strlen(val), PROMETHEUS_LENGTH - 1));
               }
               else if (!strcmp(current_key, "description"))
               {
                  memset(current_alert.description, 0, PROMETHEUS_LENGTH);
                  memcpy(current_alert.description, val, MIN((int)strlen(val), PROMETHEUS_LENGTH - 1));
                  overrides |= ALERT_OVERRIDE_DESCRIPTION;
               }
               else if (!strcmp(current_key, "query"))
               {
                  memset(current_alert.query, 0, MAX_QUERY_LENGTH);
                  memcpy(current_alert.query, val, MIN((int)strlen(val), MAX_QUERY_LENGTH - 1));
                  overrides |= ALERT_OVERRIDE_QUERY;
               }
               else if (!strcmp(current_key, "type"))
               {
                  current_alert.alert_type = parse_alert_type_string(val);
                  overrides |= ALERT_OVERRIDE_TYPE;
               }
               else if (!strcmp(current_key, "operator"))
               {
                  current_alert.operator = parse_alert_operator(val);
                  overrides |= ALERT_OVERRIDE_OPERATOR;
               }
               else if (!strcmp(current_key, "threshold"))
               {
                  current_alert.threshold = atof(val);
                  overrides |= ALERT_OVERRIDE_THRESHOLD;
               }
               else if (!strcmp(current_key, "servers"))
               {
                  if (!strcmp(val, "all"))
                  {
                     current_alert.servers_all = true;
                     current_alert.number_of_servers = 0;
                  }
                  else
                  {
                     current_alert.servers_all = false;
                     current_alert.number_of_servers = 1;
                     memset(current_alert.servers[0], 0, MISC_LENGTH);
                     memcpy(current_alert.servers[0], val, MIN((int)strlen(val), MISC_LENGTH - 1));
                  }
                  overrides |= ALERT_OVERRIDE_SERVERS;
               }

               free(current_key);
               current_key = NULL;
            }
            break;
         }

         case YAML_SEQUENCE_START_EVENT:
            if (in_alert_item && current_key != NULL && !strcmp(current_key, "servers"))
            {
               in_servers_seq = 1;
               current_alert.number_of_servers = 0;
            }
            break;

         case YAML_SEQUENCE_END_EVENT:
            if (in_servers_seq)
            {
               in_servers_seq = 0;
               overrides |= ALERT_OVERRIDE_SERVERS;
               free(current_key);
               current_key = NULL;
            }
            break;

         case YAML_MAPPING_START_EVENT:
            if (in_alert_list)
            {
               in_alert_item = 1;
               memset(&current_alert, 0, sizeof(struct alert_definition));
               overrides = 0;
            }
            break;

         case YAML_MAPPING_END_EVENT:
            if (in_alert_item)
            {
               in_alert_item = 0;

               if (strlen(current_alert.name) > 0)
               {
                  if (merge)
                  {
                     /* Merge: find existing alert by name and overwrite threshold */
                     bool found = false;
                     for (int i = 0; i < config->number_of_alerts; i++)
                     {
                        if (!strcmp(config->alerts[i].name, current_alert.name))
                        {
                           if (overrides & ALERT_OVERRIDE_DESCRIPTION)
                           {
                              memcpy(config->alerts[i].description, current_alert.description, PROMETHEUS_LENGTH);
                           }
                           if (overrides & ALERT_OVERRIDE_QUERY)
                           {
                              memcpy(config->alerts[i].query, current_alert.query, MAX_QUERY_LENGTH);
                           }
                           if (overrides & ALERT_OVERRIDE_TYPE)
                           {
                              config->alerts[i].alert_type = current_alert.alert_type;
                           }
                           if (overrides & ALERT_OVERRIDE_OPERATOR)
                           {
                              config->alerts[i].operator = current_alert.operator;
                           }
                           if (overrides & ALERT_OVERRIDE_THRESHOLD)
                           {
                              config->alerts[i].threshold = current_alert.threshold;
                           }
                           if (overrides & ALERT_OVERRIDE_SERVERS)
                           {
                              config->alerts[i].servers_all = current_alert.servers_all;
                              config->alerts[i].number_of_servers = current_alert.number_of_servers;
                              memcpy(config->alerts[i].servers, current_alert.servers, sizeof(current_alert.servers));
                           }
                           found = true;
                           break;
                        }
                     }

                     if (!found)
                     {
                        /* New alert, append */
                        alert_idx = config->number_of_alerts;
                        if (alert_idx < NUMBER_OF_ALERTS)
                        {
                           memcpy(&config->alerts[alert_idx], &current_alert, sizeof(struct alert_definition));
                           config->number_of_alerts++;
                        }
                        else
                        {
                           pgexporter_log_error("Maximum number of alerts exceeded (%d)", NUMBER_OF_ALERTS);
                        }
                     }
                  }
                  else
                  {
                     /* Initial load: append */
                     alert_idx = config->number_of_alerts;
                     if (alert_idx < NUMBER_OF_ALERTS)
                     {
                        memcpy(&config->alerts[alert_idx], &current_alert, sizeof(struct alert_definition));
                        config->number_of_alerts++;
                     }
                     else
                     {
                        pgexporter_log_error("Maximum number of alerts exceeded (%d)", NUMBER_OF_ALERTS);
                     }
                  }
               }
            }
            break;

         case YAML_STREAM_END_EVENT:
            done = 1;
            break;

         default:
            break;
      }

      yaml_event_delete(&event);
   }

   yaml_parser_delete(&parser);

   if (current_key)
   {
      free(current_key);
   }

   return 0;
}

int
pgexporter_read_internal_yaml_alerts(struct configuration* config)
{
   FILE* file = NULL;

   if (!config->alerts_enabled)
   {
      return 0;
   }

   file = fmemopen(INTERNAL_ALERTS_YAML, strlen(INTERNAL_ALERTS_YAML), "r");
   if (file == NULL)
   {
      pgexporter_log_error("Failed to open INTERNAL_ALERTS_YAML");
      return 1;
   }

   config->number_of_alerts = 0;

   if (parse_alerts_yaml(file, config, false))
   {
      fclose(file);
      return 1;
   }

   fclose(file);

   pgexporter_log_info("Loaded %d built-in alert definitions", config->number_of_alerts);
   return 0;
}

int
pgexporter_read_alerts_configuration(void* shm)
{
   struct configuration* config;
   FILE* file = NULL;

   config = (struct configuration*)shm;

   if (!config->alerts_enabled)
   {
      return 0;
   }

   if (strlen(config->alerts_path) == 0)
   {
      return 0;
   }

   if (!pgexporter_is_file(config->alerts_path))
   {
      pgexporter_log_error("Alerts path is not a file: %s", config->alerts_path);
      return 1;
   }

   file = fopen(config->alerts_path, "r");
   if (file == NULL)
   {
      pgexporter_log_error("Failed to open alerts file: %s", config->alerts_path);
      return 1;
   }

   if (parse_alerts_yaml(file, config, true))
   {
      fclose(file);
      return 1;
   }

   fclose(file);

   pgexporter_log_info("Merged alert overrides from: %s", config->alerts_path);
   return 0;
}
