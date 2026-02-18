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
 *
 */

#include <pgexporter.h>
#include <alert_configuration.h>
#include <configuration.h>
#include <memory.h>
#include <shmem.h>
#include <tscommon.h>

#include <mctf.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Build the path to a test config file under test/conf/<subdir>/
 * @param subdir   The numbered subdirectory (e.g. "01")
 * @param filename The config file name (e.g. "alerts.yaml")
 * @param path     Output buffer
 * @param path_size Size of output buffer
 * @return 0 on success, 1 on error
 */
static int
build_test_conf_path(const char* subdir, const char* filename, char* path, size_t path_size)
{
   int n;

   n = snprintf(path, path_size, "%s/test/conf/%s/%s",
                TEST_BASE_DIR, subdir, filename);

   if (n <= 0 || (size_t)n >= path_size)
   {
      return 1;
   }

   return 0;
}

MCTF_TEST_SETUP(alert)
{
   pgexporter_test_config_save();
   pgexporter_memory_init();
}

MCTF_TEST_TEARDOWN(alert)
{
   pgexporter_memory_destroy();
   pgexporter_test_config_restore();
}

// Test alerts are disabled by default
MCTF_TEST(test_alerts_are_disabled_by_default)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   MCTF_ASSERT_INT_EQ(config->alerts_enabled, false, cleanup, "alerts should be disabled by default");

cleanup:
   MCTF_FINISH();
}

// Test parsing a valid YAML with all fields
MCTF_TEST(test_alert_parse_valid_yaml)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "valid.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 2, cleanup, "expected 2 alerts");

   // First alert
   MCTF_ASSERT_STR_EQ(config->alerts[0].name, "test_down", cleanup, "alert[0] name mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[0].description, "Server is down", cleanup, "alert[0] description mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[0].alert_type, ALERT_TYPE_CONNECTION, cleanup, "alert[0] type mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[0].operator, ALERT_OPERATOR_EQ, cleanup, "alert[0] operator mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[0].threshold, -1, cleanup, "alert[0] threshold mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[0].servers_all, true, cleanup, "alert[0] servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[0].number_of_servers, 0, cleanup, "alert[0] number_of_servers mismatch");

   // Second alert
   MCTF_ASSERT_STR_EQ(config->alerts[1].name, "test_lag", cleanup, "alert[1] name mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[1].alert_type, ALERT_TYPE_QUERY, cleanup, "alert[1] type mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[1].query, "SELECT 1", cleanup, "alert[1] query mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[1].operator, ALERT_OPERATOR_GT, cleanup, "alert[1] operator mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[1].threshold, 300, cleanup, "alert[1] threshold mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[1].servers_all, false, cleanup, "alert[1] servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[1].number_of_servers, 1, cleanup, "alert[1] number_of_servers mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[1].servers[0], "replica", cleanup, "alert[1] servers[0] mismatch");

cleanup:
   MCTF_FINISH();
}

// Test empty YAML file produces 0 alerts
MCTF_TEST(test_alert_parse_empty_file)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "empty.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 0, cleanup, "expected 0 alerts");

cleanup:
   MCTF_FINISH();
}

// Test empty alerts_path returns 0 (no-op)
MCTF_TEST(test_alert_parse_no_path)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   memset(config->alerts_path, 0, MAX_PATH);
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "expected 0 for empty path");

cleanup:
   MCTF_FINISH();
}

// Test merge: override threshold of existing alert
MCTF_TEST(test_alert_merge_override_threshold)
{
   struct configuration* config;
   char path[MAX_PATH];
   int original_count;
   int idx;

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT_INT_EQ(pgexporter_read_internal_yaml_alerts(config), 0, cleanup, "read internal alerts failed");
   original_count = config->number_of_alerts;
   MCTF_ASSERT(original_count > 0, cleanup, "expected at least 1 internal alert");

   MCTF_ASSERT(build_test_conf_path("alert", "merge.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, original_count + 1, cleanup, "alert count changed");

   /* Find postgresql_down by name */
   idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "postgresql_down") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "postgresql_down not found");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].threshold, 42, cleanup, "threshold not overridden");

cleanup:
   MCTF_FINISH();
}

// Test merge: override operator of existing alert
MCTF_TEST(test_alert_merge_override_operator)
{
   struct configuration* config;
   char path[MAX_PATH];
   int idx;

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT_INT_EQ(pgexporter_read_internal_yaml_alerts(config), 0, cleanup, "read internal alerts failed");

   MCTF_ASSERT(build_test_conf_path("alert", "merge.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");

   /* Find postgresql_down by name */
   idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "postgresql_down") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "postgresql_down not found");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].operator, ALERT_OPERATOR_NE, cleanup, "operator not overridden");

cleanup:
   MCTF_FINISH();
}

// Test merge: append new alert
MCTF_TEST(test_alert_merge_append_new)
{
   struct configuration* config;
   char path[MAX_PATH];
   int original_count;

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT_INT_EQ(pgexporter_read_internal_yaml_alerts(config), 0, cleanup, "read internal alerts failed");
   original_count = config->number_of_alerts;

   MCTF_ASSERT(build_test_conf_path("alert", "merge.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, original_count + 1, cleanup, "alert not appended");
   MCTF_ASSERT_STR_EQ(config->alerts[original_count].name, "custom_combined_alert", cleanup, "appended alert name mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[original_count].threshold, 50, cleanup, "appended alert threshold mismatch");

cleanup:
   MCTF_FINISH();
}

// Test merge: override servers filter of existing alert
MCTF_TEST(test_alert_merge_override_servers)
{
   struct configuration* config;
   char path[MAX_PATH];
   int original_count;
   int idx;

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT_INT_EQ(pgexporter_read_internal_yaml_alerts(config), 0, cleanup, "read internal alerts failed");
   original_count = config->number_of_alerts;

   MCTF_ASSERT(build_test_conf_path("alert", "merge.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, original_count + 1, cleanup, "alert count changed");

   /* Find postgresql_down by name */
   idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "postgresql_down") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "postgresql_down not found");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, false, cleanup, "servers_all not updated");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 1, cleanup, "number_of_servers mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].servers[0], "primary", cleanup, "server name mismatch");

cleanup:
   MCTF_FINISH();
}

// Test servers: all
MCTF_TEST(test_alert_servers_scalar_all)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "servers.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 5, cleanup, "expected 5 alerts");

   int idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "test_scalar_all") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "not found");

   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, true, cleanup, "servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 0, cleanup, "number_of_servers mismatch");

cleanup:
   MCTF_FINISH();
}

// Test servers: primary
MCTF_TEST(test_alert_servers_scalar_primary)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "servers.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 5, cleanup, "expected 5 alerts");

   int idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "test_scalar_primary") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "not found");

   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, false, cleanup, "servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 1, cleanup, "number_of_servers mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].servers[0], "primary", cleanup, "server name mismatch");

cleanup:
   MCTF_FINISH();
}

// Test servers: [primary, replica]
MCTF_TEST(test_alert_servers_list)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "servers.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 5, cleanup, "expected 5 alerts");

   int idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "test_list") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "not found");

   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, false, cleanup, "servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 2, cleanup, "number_of_servers mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].servers[0], "primary", cleanup, "server[0] mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].servers[1], "replica", cleanup, "server[1] mismatch");

cleanup:
   MCTF_FINISH();
}

// Test servers: [all, primary] - "all" in list means servers_all = true
MCTF_TEST(test_alert_servers_list_with_all)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "servers.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 5, cleanup, "expected 5 alerts");

   int idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "test_list_all") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "not found");

   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, true, cleanup, "servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 0, cleanup, "number_of_servers mismatch");

cleanup:
   MCTF_FINISH();
}

// Test servers: [primary, replica] (flow-style brackets)
MCTF_TEST(test_alert_servers_list_brackets)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT(build_test_conf_path("alert", "servers.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));
   config->number_of_alerts = 0;

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, 5, cleanup, "expected 5 alerts");

   int idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "test_list_brackets") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "not found");

   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, false, cleanup, "servers_all mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 2, cleanup, "number_of_servers mismatch");

cleanup:
   MCTF_FINISH();
}

// Test that alerts = off in pgexporter.conf disables alerting
MCTF_TEST(test_alerts_disabled_via_conf)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   MCTF_ASSERT(build_test_conf_path("alert", "alerts_off.conf", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   MCTF_ASSERT_INT_EQ(pgexporter_read_configuration(shmem, path), 0, cleanup, "read_configuration failed");
   MCTF_ASSERT_INT_EQ(config->alerts_enabled, false, cleanup, "alerts should be disabled");

cleanup:
   MCTF_FINISH();
}

// Test that alerts = on in pgexporter.conf enables alerting
MCTF_TEST(test_alerts_enabled_via_conf)
{
   struct configuration* config;
   char path[MAX_PATH];

   config = (struct configuration*)shmem;

   MCTF_ASSERT(build_test_conf_path("alert", "alerts_on.conf", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   MCTF_ASSERT_INT_EQ(pgexporter_read_configuration(shmem, path), 0, cleanup, "read_configuration failed");
   MCTF_ASSERT_INT_EQ(config->alerts_enabled, true, cleanup, "alerts should be enabled");

cleanup:
   MCTF_FINISH();
}

// Test merge: combined override of two internal alerts and append one new alert
MCTF_TEST(test_alert_merge_combined)
{
   struct configuration* config;
   char path[MAX_PATH];
   int original_count;
   int idx;

   config = (struct configuration*)shmem;

   config->alerts_enabled = true;
   MCTF_ASSERT_INT_EQ(pgexporter_read_internal_yaml_alerts(config), 0, cleanup, "read internal alerts failed");
   original_count = config->number_of_alerts;
   MCTF_ASSERT(original_count > 0, cleanup, "expected at least 1 internal alert");

   MCTF_ASSERT(build_test_conf_path("alert", "merge.yaml", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   memset(config->alerts_path, 0, MAX_PATH);
   memcpy(config->alerts_path, path, strlen(path));

   MCTF_ASSERT_INT_EQ(pgexporter_read_alerts_configuration(shmem), 0, cleanup, "read_alerts_configuration failed");
   MCTF_ASSERT_INT_EQ(config->number_of_alerts, original_count + 1, cleanup, "expected original+1 alerts after merge");

   /* Find postgresql_down by name */
   idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "postgresql_down") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "postgresql_down not found");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].threshold, 42, cleanup, "threshold not overridden");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].operator, ALERT_OPERATOR_NE, cleanup, "operator not overridden");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, false, cleanup, "servers_all not overridden");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].number_of_servers, 1, cleanup, "number_of_servers mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].servers[0], "primary", cleanup, "server name mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].alert_type, ALERT_TYPE_CONNECTION, cleanup, "type should be preserved");

   /* Find connections_high by name */
   idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "connections_high") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "connections_high not found");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].threshold, 95, cleanup, "threshold not overridden");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].description, "Overridden description", cleanup, "description not overridden");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].operator, ALERT_OPERATOR_GT, cleanup, "operator should be preserved");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].alert_type, ALERT_TYPE_QUERY, cleanup, "type should be preserved");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, true, cleanup, "servers_all should be preserved");

   /* Find custom_combined_alert by name */
   idx = -1;
   for (int i = 0; i < config->number_of_alerts; i++)
   {
      if (strcmp(config->alerts[i].name, "custom_combined_alert") == 0)
      {
         idx = i;
         break;
      }
   }
   MCTF_ASSERT(idx >= 0, cleanup, "custom_combined_alert not found");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].description, "Combined custom alert", cleanup, "description mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].alert_type, ALERT_TYPE_QUERY, cleanup, "type mismatch");
   MCTF_ASSERT_STR_EQ(config->alerts[idx].query, "SELECT 99", cleanup, "query mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].operator, ALERT_OPERATOR_GE, cleanup, "operator mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].threshold, 50, cleanup, "threshold mismatch");
   MCTF_ASSERT_INT_EQ(config->alerts[idx].servers_all, true, cleanup, "servers_all mismatch");

cleanup:
   MCTF_FINISH();
}
