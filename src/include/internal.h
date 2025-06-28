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

#ifndef PGEXPORTER_INTERNAL_H
#define PGEXPORTER_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#define INTERNAL_YAML "" \
        "# This is the default minimum version for queries, unless specified otherwise\n" \
        "version: 16\n" \
        "metrics:\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 10\n" \
        "#\n" \
        "\n" \
        "# primary_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "              (\n" \
        "                CASE pg_is_in_recovery() WHEN 'f'\n" \
        "                  THEN 't'\n" \
        "                  ELSE 'f'\n" \
        "                END\n" \
        "              );\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - description: Is the PostgreSQL instance the primary\n" \
        "          type: gauge\n" \
        "    tag: postgresql_primary\n" \
        "    sort: name\n" \
        "    collector: primary\n" \
        "    server: both\n" \
        "\n" \
        "# database_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                datname,\n" \
        "                pg_database_size(datname)\n" \
        "              FROM pg_database;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - description: Size of the database\n" \
        "          type: gauge\n" \
        "    tag: pg_database_size\n" \
        "    sort: data\n" \
        "    collector: db\n" \
        "\n" \
        "# locks_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                pg_database.datname as database,\n" \
        "                tmp.mode,\n" \
        "                COALESCE(count, 0) as count\n" \
        "              FROM (\n" \
        "                VALUES\n" \
        "                  ('accesssharelock'),\n" \
        "                  ('rowsharelock'),\n" \
        "                  ('rowexclusivelock'),\n" \
        "                  ('shareupdateexclusivelock'),\n" \
        "                  ('sharelock'),\n" \
        "                  ('sharerowexclusivelock'),\n" \
        "                  ('exclusivelock'),\n" \
        "                  ('accessexclusivelock'),\n" \
        "                  ('sireadlock')\n" \
        "              ) AS tmp(mode)\n" \
        "              CROSS JOIN pg_database\n" \
        "              LEFT JOIN\n" \
        "              (\n" \
        "                SELECT\n" \
        "                  database,\n" \
        "                  lower(mode) AS mode,\n" \
        "                  count(*) AS count\n" \
        "                FROM pg_locks\n" \
        "                WHERE database IS NOT NULL\n" \
        "                GROUP BY database, lower(mode)\n" \
        "              ) AS tmp2\n" \
        "              ON tmp.mode = tmp2.mode AND pg_database.oid = tmp2.database\n" \
        "              ORDER BY 1, 2;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - name: mode\n" \
        "          type: label\n" \
        "        - description: Lock count of a database\n" \
        "          type: gauge\n" \
        "    tag: pg_locks_count\n" \
        "    sort: data\n" \
        "    collector: locks\n" \
        "\n" \
        "# replication_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                slot_name,\n" \
        "                slot_type,\n" \
        "                database,\n" \
        "                active,\n" \
        "                temporary\n" \
        "              FROM pg_replication_slots;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "      - name: slot_name\n" \
        "        type: label\n" \
        "      - name: slot_type\n" \
        "        type: label\n" \
        "      - name: database\n" \
        "        type: label\n" \
        "      - description: Is the replication active\n" \
        "        name: active\n" \
        "        type: gauge\n" \
        "      - description: Is the replication temporary\n" \
        "        name: temporary\n" \
        "        type: gauge\n" \
        "    tag: pg_replication_slots\n" \
        "    sort: data\n" \
        "    collector: replication\n" \
        "\n" \
        "# stat_bgwriter_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                buffers_alloc,\n" \
        "                buffers_backend,\n" \
        "                buffers_backend_fsync,\n" \
        "                buffers_checkpoint,\n" \
        "                buffers_clean,\n" \
        "                checkpoint_sync_time,\n" \
        "                checkpoint_write_time,\n" \
        "                checkpoints_req,\n" \
        "                checkpoints_timed,\n" \
        "                maxwritten_clean\n" \
        "              FROM pg_stat_bgwriter;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - description: pg_stat_bgwriter_buffers_alloc\n" \
        "          name: buffers_alloc\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_buffers_backend\n" \
        "          name: buffers_backend\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_buffers_backend_fsync\n" \
        "          name: buffers_backend_fsync\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_buffers_checkpoint\n" \
        "          name: buffers_checkpoint\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_buffers_clean\n" \
        "          name: buffers_clean\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_checkpoint_sync_time\n" \
        "          name: checkpoint_sync_time\n" \
        "          type: counter\n" \
        "        - description: pg_stat_bgwriter_checkpoint_write_time\n" \
        "          name: checkpoint_write_time\n" \
        "          type: counter\n" \
        "        - description: pg_stat_bgwriter_checkpoints_req\n" \
        "          name: checkpoints_req\n" \
        "          type: counter\n" \
        "        - description: pg_stat_bgwriter_checkpoints_timed\n" \
        "          name: checkpoints_timed\n" \
        "          type: counter\n" \
        "        - description: pg_stat_bgwriter_maxwritten_clean\n" \
        "          name: maxwritten_clean\n" \
        "          type: counter\n" \
        "    - query: SELECT\n" \
        "                buffers_alloc,\n" \
        "                buffers_clean,\n" \
        "                maxwritten_clean\n" \
        "              FROM pg_stat_bgwriter;\n" \
        "      version: 17\n" \
        "      columns:\n" \
        "        - description: pg_stat_bgwriter_buffers_alloc\n" \
        "          name: buffers_alloc\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_buffers_clean\n" \
        "          name: buffers_clean\n" \
        "          type: gauge\n" \
        "        - description: pg_stat_bgwriter_maxwritten_clean\n" \
        "          name: maxwritten_clean\n" \
        "          type: counter\n" \
        "    tag: pg_stat_bgwriter\n" \
        "    collector: stat_bgwriter\n" \
        "\n" \
        "# histogram sample\n" \
        "  - queries:\n" \
        "    - query: WITH\n" \
        "              metrics AS (\n" \
        "                SELECT\n" \
        "                  application_name,\n" \
        "                  SUM(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - state_change))::bigint)::float AS process_idle_seconds_sum,\n" \
        "                  COUNT(*) AS process_idle_seconds_count\n" \
        "                FROM pg_stat_activity\n" \
        "                WHERE state = 'idle'\n" \
        "                GROUP BY application_name\n" \
        "              ),\n" \
        "              buckets AS (\n" \
        "                SELECT\n" \
        "                  application_name,\n" \
        "                  le,\n" \
        "                  SUM(\n" \
        "                    CASE WHEN EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - state_change)) <= le\n" \
        "                      THEN 1\n" \
        "                      ELSE 0\n" \
        "                    END\n" \
        "                  )::bigint AS bucket\n" \
        "                FROM\n" \
        "                  pg_stat_activity,\n" \
        "                  UNNEST(ARRAY[1, 2, 5, 15, 30, 60, 90, 120, 300]) AS le\n" \
        "                GROUP BY application_name, le\n" \
        "                ORDER BY application_name, le\n" \
        "              )\n" \
        "              SELECT\n" \
        "                application_name,\n" \
        "                process_idle_seconds_sum as seconds_sum,\n" \
        "                process_idle_seconds_count as seconds_count,\n" \
        "                ARRAY_AGG(le) AS seconds,\n" \
        "                ARRAY_AGG(bucket) AS seconds_bucket\n" \
        "              FROM metrics JOIN buckets USING (application_name)\n" \
        "              GROUP BY 1, 2, 3;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: application_name\n" \
        "          type: label\n" \
        "        - name: seconds\n" \
        "          type: histogram\n" \
        "          description: Histogram of idle processes\n" \
        "    tag: pg_process_idle_seconds\n" \
        "    collector: idle_procs\n" \
        "\n" \
        "# Get number of available extensions for installations.\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*) AS extensions\n" \
        "              FROM pg_available_extensions;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of available extensions for installation.\n" \
        "    tag: pg_available_extensions\n" \
        "    collector: available_extensions\n" \
        "\n" \
        "# Get number of installed extensions.\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                array_agg(extname) AS extensions,\n" \
        "                count(*)\n" \
        "              FROM pg_extension;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: extensions\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Number of installed extensions.\n" \
        "    tag: pg_installed_extensions\n" \
        "    collector: installed_extensions\n" \
        "\n" \
        "# Get applied settings\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                sourcefile,\n" \
        "                COUNT(*),\n" \
        "                (\n" \
        "                  CASE applied WHEN 't'\n" \
        "                    THEN 'true'\n" \
        "                    ELSE 'false'\n" \
        "                  END\n" \
        "                ) as applied\n" \
        "              FROM pg_file_settings\n" \
        "              WHERE applied = 't'\n" \
        "              GROUP BY sourcefile, applied;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: sourcefile\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Settings that are applied.\n" \
        "        - name: applied\n" \
        "          type: label\n" \
        "    tag: pg_file_settings\n" \
        "    sort: data\n" \
        "    collector: file_settings\n" \
        "\n" \
        "# Get indexes for schemas.\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                schemaname,\n" \
        "                tablename,\n" \
        "                COUNT(*)\n" \
        "              FROM pg_indexes\n" \
        "              GROUP BY schemaname,tablename;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: schemaname\n" \
        "          type: label\n" \
        "        - name: tablename\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Indexes for each schemaname for each tablename.\n" \
        "    tag: pg_indexes\n" \
        "    sort: data\n" \
        "    collector: indexes\n" \
        "\n" \
        "# Get materialized views\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*)\n" \
        "              FROM pg_matviews\n" \
        "              WHERE ispopulated = 't'\n" \
        "              GROUP BY ispopulated;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of applied Materialized views.\n" \
        "    tag: pg_matviews\n" \
        "    collector: matviews\n" \
        "\n" \
        "# Get number of roles\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*)\n" \
        "              FROM pg_roles;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of roles.\n" \
        "    tag: pg_role\n" \
        "    collector: roles\n" \
        "\n" \
        "# Get number of rules\n" \
        "  - queries:\n" \
        "    - query: SELECT tablename, COUNT(*)\n" \
        "              FROM pg_rules\n" \
        "              GROUP BY tablename;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: tablename\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Number of rules in table.\n" \
        "    tag: pg_rule\n" \
        "    sort: data\n" \
        "    collector: rules\n" \
        "\n" \
        "# Get user auth data\n" \
        "  - queries:\n" \
        "    - query: SELECT (\n" \
        "                CASE\n" \
        "                  WHEN rolpassword LIKE 'md5%' THEN 'MD5'\n" \
        "                  WHEN rolpassword LIKE 'SCRAM-SHA-256$%' THEN 'SCRAM-SHA-256'\n" \
        "                  ELSE 'UNENCRYPTED'\n" \
        "                END\n" \
        "              ) AS encryption_type, COUNT(*)\n" \
        "              FROM pg_authid\n" \
        "              WHERE rolname != 'postgres'\n" \
        "              GROUP BY encryption_type;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: encryption_type\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Number of users with authentication type.\n" \
        "    tag: pg_shadow\n" \
        "    sort: data\n" \
        "    collector: auth_type\n" \
        "\n" \
        "# Get user defined event-triggers\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*)\n" \
        "              FROM pg_event_trigger\n" \
        "              JOIN pg_authid\n" \
        "              ON pg_event_trigger.oid = pg_authid.oid\n" \
        "              WHERE rolname != 'postgres';\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of user defined event triggers.\n" \
        "    tag: pg_usr_evt_trigger\n" \
        "    collector: usr_evt_trigger\n" \
        "\n" \
        "# Get number of database connections\n" \
        "  - queries:\n" \
        "    - query: SELECT datname, count(*)\n" \
        "              FROM pg_stat_activity\n" \
        "              WHERE datname IS NOT NULL\n" \
        "              GROUP BY datname;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Number of database connections.\n" \
        "    tag: pg_db_conn\n" \
        "    sort: data\n" \
        "    collector: connections\n" \
        "\n" \
        "# Get DB connections with SSL\n" \
        "  - queries:\n" \
        "    - query: SELECT datname, count(*)\n" \
        "              FROM pg_stat_ssl\n" \
        "              JOIN pg_stat_activity\n" \
        "              ON pg_stat_activity.pid = pg_stat_ssl.pid\n" \
        "              WHERE ssl = 't' AND datname IS NOT NULL\n" \
        "              GROUP BY datname;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - type: gauge\n" \
        "          description: Number of DB connections with SSL.\n" \
        "    tag: pg_db_conn_ssl\n" \
        "    sort: data\n" \
        "    collector: connections\n" \
        "\n" \
        "# All tables statio\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                COUNT(heap_blks_read) AS heap_blks_read,\n" \
        "                COUNT(heap_blks_hit) AS heap_blks_hit,\n" \
        "                COUNT(idx_blks_read) as idx_blks_read,\n" \
        "                COUNT(idx_blks_hit) AS idx_blks_hit,\n" \
        "                COUNT(toast_blks_read) AS toast_blks_read,\n" \
        "                COUNT(toast_blks_hit) AS toast_blks_hit,\n" \
        "                COUNT(tidx_blks_read) AS tidx_blks_read,\n" \
        "                COUNT(tidx_blks_hit) AS tidx_blks_hit\n" \
        "              FROM pg_statio_all_tables;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: heap_blks_read\n" \
        "          type: counter\n" \
        "          description: Number of disk blocks read in postgres db.\n" \
        "        - name: heap_blks_hit\n" \
        "          type: counter\n" \
        "          description: Number of buffer hits read in postgres db.\n" \
        "        - name: idx_blks_read\n" \
        "          type: counter\n" \
        "          description: Number of disk blocks reads from all indexes in postgres db.\n" \
        "        - name: idx_blks_hit\n" \
        "          type: counter\n" \
        "          description: Number of buffer hits read from all indexes in postgres db.\n" \
        "        - name: toast_blks_read\n" \
        "          type: counter\n" \
        "          description: Number of disk blocks reads from postgres db's TOAST tables.\n" \
        "        - name: toast_blks_hit\n" \
        "          type: counter\n" \
        "          description: Number of buffer hits read from postgres db's TOAST tables.\n" \
        "        - name: tidx_blks_read\n" \
        "          type: counter\n" \
        "          description: Number of disk blocks reads from postgres db's TOAST table indexes.\n" \
        "        - name: tidx_blks_hit\n" \
        "          type: counter\n" \
        "          description: Number of buffer hits read from postgres db's TOAST table indexes.\n" \
        "    tag: pg_statio_all_tables\n" \
        "    collector: statio_all_tables\n" \
        "\n" \
        "# All sequences statio\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                COUNT(blks_read) AS blks_read,\n" \
        "                COUNT(blks_hit) AS blks_hit\n" \
        "              FROM pg_statio_all_sequences;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: blks_read\n" \
        "          type: counter\n" \
        "          description: Number of disk blocks read from sequences in postgres db.\n" \
        "        - name: blks_hit\n" \
        "          type: counter\n" \
        "          description: Number of buffer hits read from sequences in postgres db.\n" \
        "    tag: pg_statio_all_sequences\n" \
        "    collector: statio_all_sequences\n" \
        "\n" \
        "# Stat user functions\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                funcname,\n" \
        "                calls,\n" \
        "                self_time,\n" \
        "                total_time\n" \
        "              FROM pg_stat_user_functions;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: funcname\n" \
        "          type: label\n" \
        "        - name: calls\n" \
        "          type: counter\n" \
        "          description: Number of times function is called.\n" \
        "        - name: self_time\n" \
        "          type: counter\n" \
        "          description: Total time spent in milliseconds on the function itself.\n" \
        "        - name: total_time\n" \
        "          type: counter\n" \
        "          description: Total time spent in milliseconds by the function and any other functions called by it.\n" \
        "    tag: pg_stat_user_functions\n" \
        "    sort: data\n" \
        "    collector: stat_user_functions\n" \
        "\n" \
        "# Number of streaming WAL connections per application.\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*), application_name\n" \
        "              FROM pg_stat_replication\n" \
        "              WHERE state = 'streaming'\n" \
        "              GROUP BY application_name;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of streaming WAL connections per application.\n" \
        "        - name: application_name\n" \
        "          type: label\n" \
        "    tag: pg_stat_replication\n" \
        "    sort: data\n" \
        "    collector: stat_replication\n" \
        "\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                archived_count,\n" \
        "                (EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_archived_time)) * 1000)::bigint AS success_time_elapsed_ms,\n" \
        "                failed_count,\n" \
        "                (EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_failed_time)) * 1000)::bigint AS failure_time_elapsed_ms\n" \
        "              FROM pg_stat_archiver;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: counter\n" \
        "          name: archived_count\n" \
        "          description: Number of successful archived WAL files.\n" \
        "        - type: counter\n" \
        "          name: success_time_elapsed_ms\n" \
        "          description: Milliseconds since successful archived WAL file.\n" \
        "        - type: counter\n" \
        "          name: failed_count\n" \
        "          description: Number of failed archival operation on WAL files.\n" \
        "        - type: counter\n" \
        "          name: failure_time_elapsed_ms\n" \
        "          description: Milliseconds since last failed archival operation on WAL files.\n" \
        "    tag: pg_stat_archiver\n" \
        "    sort: data\n" \
        "    collector: stat_archiver\n" \
        "\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                datname,\n" \
        "                age(datfrozenxid) as age\n" \
        "              FROM pg_database;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: label\n" \
        "          name: datname\n" \
        "          description: Database name.\n" \
        "        - type: counter\n" \
        "          name: age\n" \
        "          description: Age since last vaccum.\n" \
        "    tag: pg_db_vacuum\n" \
        "    sort: data\n" \
        "    collector: db_vacuum\n" \
        "\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                c.oid::regclass as table_name,\n" \
        "                greatest(age(c.relfrozenxid),age(t.relfrozenxid)) as age\n" \
        "              FROM pg_class c\n" \
        "              LEFT JOIN pg_class t ON c.reltoastrelid = t.oid\n" \
        "              WHERE c.relkind IN ('r', 'm');\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - type: label\n" \
        "          name: datname\n" \
        "          description: View name.\n" \
        "        - type: counter\n" \
        "          name: age\n" \
        "          description: Age since last vaccum.\n" \
        "    tag: pg_view_vacuum\n" \
        "    sort: data\n" \
        "    collector: db_vacuum\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 11\n" \
        "#\n" \
        "\n" \
        "# WAL last sent\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "              CASE\n" \
        "                WHEN sender_host LIKE '/%' THEN sender_host\n" \
        "                ELSE sender_host || ':' || sender_port::text\n" \
        "              END AS sender,\n" \
        "              (EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_msg_send_time)) * 1000)::bigint AS time_elapsed_ms\n" \
        "              FROM pg_stat_wal_receiver;\n" \
        "      version: 11\n" \
        "      columns:\n" \
        "        - name: sender\n" \
        "          type: label\n" \
        "        - type: counter\n" \
        "          description: Time since last message received from WAL sender\n" \
        "    tag: pg_wal_last_received\n" \
        "    collector: wal_last_received\n" \
        "    server: replica\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 12\n" \
        "#\n" \
        "\n" \
        "# GSS Authenticated DB Connections\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*)\n" \
        "              FROM pg_stat_gssapi\n" \
        "              JOIN pg_stat_activity\n" \
        "              ON pg_stat_gssapi.pid = pg_stat_activity.pid\n" \
        "              WHERE gss_authenticated = 't' AND datname IS NOT NULL;\n" \
        "      version: 12\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of GSSAPI authenticated DB connections.\n" \
        "    tag: pg_gss_auth\n" \
        "    collector: gssapi\n" \
        "\n" \
        "# Encrypted DB Connections\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*)\n" \
        "              FROM pg_stat_gssapi\n" \
        "              JOIN pg_stat_activity\n" \
        "              ON pg_stat_gssapi.pid = pg_stat_activity.pid\n" \
        "              WHERE encrypted = 't' AND datname IS NOT NULL;\n" \
        "      version: 12\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of encrypted DB connections.\n" \
        "    tag: pg_encrypted_conn\n" \
        "    collector: encryted_conns\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 13\n" \
        "#\n" \
        "\n" \
        "# Shared memory histogram\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                SUM(sum) AS size_sum,\n" \
        "                '1' AS size_count,\n" \
        "                array_agg((bucket - 1) * (50000))::int[] AS size,\n" \
        "                array_agg(count)::int[] AS size_bucket\n" \
        "              FROM (\n" \
        "                  SELECT width_bucket(allocated_size, 0, 5000000, 100) AS bucket, COUNT(*), SUM(allocated_size)\n" \
        "                  FROM pg_shmem_allocations\n" \
        "                  GROUP BY bucket\n" \
        "                  ORDER BY bucket\n" \
        "              ) t;\n" \
        "      version: 13\n" \
        "      columns:\n" \
        "        - name: size\n" \
        "          type: histogram\n" \
        "          description: Histogram of shared memory sizes.\n" \
        "    tag: pg_shmem_allocations\n" \
        "    collector: shmem_size\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 14\n" \
        "#\n" \
        "\n" \
        "# Memory context information\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                COUNT(*) AS contexts,\n" \
        "                parent,\n" \
        "                SUM(free_bytes) AS free_bytes,\n" \
        "                SUM(used_bytes) AS used_bytes,\n" \
        "                SUM(total_bytes) AS total_bytes\n" \
        "              FROM pg_backend_memory_contexts\n" \
        "              WHERE parent!=''\n" \
        "              GROUP BY parent;\n" \
        "      version: 14\n" \
        "      columns:\n" \
        "        - name: contexts\n" \
        "          type: gauge\n" \
        "          description: Number of memory contexts per parent.\n" \
        "        - name: parent\n" \
        "          type: label\n" \
        "        - name: free_bytes\n" \
        "          type: gauge\n" \
        "          description: Free bytes per memory context.\n" \
        "        - name: used_bytes\n" \
        "          type: gauge\n" \
        "          description: Used bytes per memory context.\n" \
        "        - name: total_bytes\n" \
        "          type: gauge\n" \
        "          description: Total bytes per memory context.\n" \
        "    tag: pg_mem_ctx\n" \
        "    collector: mem_ctx\n" \
        "\n" \
        "# Memory context information\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                wal_records,\n" \
        "                wal_fpi,\n" \
        "                wal_bytes,\n" \
        "                wal_buffers_full,\n" \
        "                wal_write,\n" \
        "                wal_sync,\n" \
        "                wal_write_time,\n" \
        "                wal_sync_time\n" \
        "              FROM pg_stat_wal;\n" \
        "      version: 14\n" \
        "      columns:\n" \
        "        - name: wal_records\n" \
        "          type: counter\n" \
        "          description: Number of WAL records generated.\n" \
        "        - name: wal_fpi\n" \
        "          type: counter\n" \
        "          description: Number of WAL full page images generated.\n" \
        "        - name: wal_bytes\n" \
        "          type: counter\n" \
        "          description: Total bytes of generated WAL.\n" \
        "        - name: wal_buffers_full\n" \
        "          type: counter\n" \
        "          description: Number of disk writes due to WAL buffers being full.\n" \
        "        - name: wal_write\n" \
        "          type: counter\n" \
        "          description: Number of times WAL files were written to disk.\n" \
        "        - name: wal_sync\n" \
        "          type: counter\n" \
        "          description: Number of times WAL files were synced to disk.\n" \
        "        - name: wal_write_time\n" \
        "          type: counter\n" \
        "          description: Time taken for WAL files to be written to disk.\n" \
        "        - name: wal_sync_time\n" \
        "          type: counter\n" \
        "          description: Time taken for WAL files to be synced to disk.\n" \
        "    tag: pg_stat_wal\n" \
        "    collector: stat_wal\n" \
        "\n" \
        "# stat_database_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                datname,\n" \
        "                blk_read_time,\n" \
        "                blk_write_time,\n" \
        "                blks_hit,\n" \
        "                blks_read,\n" \
        "                deadlocks,\n" \
        "                temp_files,\n" \
        "                temp_bytes,\n" \
        "                tup_returned,\n" \
        "                tup_fetched,\n" \
        "                tup_inserted,\n" \
        "                tup_updated,\n" \
        "                tup_deleted,\n" \
        "                xact_commit,\n" \
        "                xact_rollback,\n" \
        "                conflicts,\n" \
        "                numbackends\n" \
        "              FROM pg_stat_database\n" \
        "              WHERE datname IS NOT NULL\n" \
        "              ORDER BY datname;\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - name: blk_read_time\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blk_read_time\n" \
        "        - name: blk_write_time\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blk_write_time\n" \
        "        - name: blks_hit\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blks_hit\n" \
        "        - name: blks_read\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blks_read\n" \
        "        - name: deadlocks\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_deadlocks\n" \
        "        - name: temp_files\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_temp_files\n" \
        "        - name: temp_bytes\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_temp_bytes\n" \
        "        - name: tup_returned\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_returned\n" \
        "        - name: tup_fetched\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_fetched\n" \
        "        - name: tup_inserted\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_inserted\n" \
        "        - name: tup_updated\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_updated\n" \
        "        - name: tup_deleted\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_deleted\n" \
        "        - name: xact_commit\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_xact_commit\n" \
        "        - name: xact_rollback\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_xact_rollback\n" \
        "        - name: conflicts\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts\n" \
        "        - name: numbackends\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_numbackends\n" \
        "      version: 10\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                datname,\n" \
        "                blk_read_time,\n" \
        "                blk_write_time,\n" \
        "                blks_hit,\n" \
        "                blks_read,\n" \
        "                deadlocks,\n" \
        "                temp_files,\n" \
        "                temp_bytes,\n" \
        "                tup_returned,\n" \
        "                tup_fetched,\n" \
        "                tup_inserted,\n" \
        "                tup_updated,\n" \
        "                tup_deleted,\n" \
        "                xact_commit,\n" \
        "                xact_rollback,\n" \
        "                conflicts,\n" \
        "                numbackends,\n" \
        "                checksum_failures\n" \
        "              FROM pg_stat_database WHERE datname IS NOT NULL ORDER BY datname;\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - name: blk_read_time\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blk_read_time\n" \
        "        - name: blk_write_time\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blk_write_time\n" \
        "        - name: blks_hit\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blks_hit\n" \
        "        - name: blks_read\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blks_read\n" \
        "        - name: deadlocks\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_deadlocks\n" \
        "        - name: temp_files\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_temp_files\n" \
        "        - name: temp_bytes\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_temp_bytes\n" \
        "        - name: tup_returned\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_returned\n" \
        "        - name: tup_fetched\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_fetched\n" \
        "        - name: tup_inserted\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_inserted\n" \
        "        - name: tup_updated\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_updated\n" \
        "        - name: tup_deleted\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_deleted\n" \
        "        - name: xact_commit\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_xact_commit\n" \
        "        - name: xact_rollback\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_xact_rollback\n" \
        "        - name: conflicts\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts\n" \
        "        - name: numbackends\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_numbackends\n" \
        "        - name: checksum_failures\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_checksum_failures\n" \
        "      version: 12\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                datname,\n" \
        "                blk_read_time,\n" \
        "                blk_write_time,\n" \
        "                blks_hit,\n" \
        "                blks_read,\n" \
        "                deadlocks,\n" \
        "                temp_files,\n" \
        "                temp_bytes,\n" \
        "                tup_returned,\n" \
        "                tup_fetched,\n" \
        "                tup_inserted,\n" \
        "                tup_updated,\n" \
        "                tup_deleted,\n" \
        "                xact_commit,\n" \
        "                xact_rollback,\n" \
        "                conflicts,\n" \
        "                numbackends,\n" \
        "                checksum_failures,\n" \
        "                session_time,\n" \
        "                active_time,\n" \
        "                idle_in_transaction_time,\n" \
        "                sessions,\n" \
        "                sessions_abandoned,\n" \
        "                sessions_fatal,\n" \
        "                sessions_killed\n" \
        "              FROM pg_stat_database WHERE datname IS NOT NULL ORDER BY datname;\n" \
        "      version: 14\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - name: blk_read_time\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blk_read_time\n" \
        "        - name: blk_write_time\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blk_write_time\n" \
        "        - name: blks_hit\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blks_hit\n" \
        "        - name: blks_read\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_blks_read\n" \
        "        - name: deadlocks\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_deadlocks\n" \
        "        - name: temp_files\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_temp_files\n" \
        "        - name: temp_bytes\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_temp_bytes\n" \
        "        - name: tup_returned\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_returned\n" \
        "        - name: tup_fetched\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_fetched\n" \
        "        - name: tup_inserted\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_inserted\n" \
        "        - name: tup_updated\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_updated\n" \
        "        - name: tup_deleted\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_tup_deleted\n" \
        "        - name: xact_commit\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_xact_commit\n" \
        "        - name: xact_rollback\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_xact_rollback\n" \
        "        - name: conflicts\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts\n" \
        "        - name: numbackends\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_numbackends\n" \
        "        - name: checksum_failures\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_checksum_failures\n" \
        "        - name: session_time\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_session_time\n" \
        "        - name: active_time\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_active_time\n" \
        "        - name: idle_in_transaction_time\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_idle_in_transaction_time\n" \
        "        - name: sessions\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_sessions\n" \
        "        - name: sessions_abandoned\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_sessions_abandoned\n" \
        "        - name: sessions_fatal\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_sessions_fatal\n" \
        "        - name: sessions_killed\n" \
        "          type: gauge\n" \
        "          description: pg_stat_database_sessions_killed\n" \
        "    tag: pg_stat_database\n" \
        "    collector: stat_db\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 15\n" \
        "#\n" \
        "\n" \
        "# WAL prefetch last stat reset\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                FLOOR(\n" \
        "                  EXTRACT(\n" \
        "                    EPOCH FROM (now() - stats_reset)\n" \
        "                  )\n" \
        "                )\n" \
        "              FROM pg_stat_recovery_prefetch;\n" \
        "      version: 15\n" \
        "      columns:\n" \
        "        - type: counter\n" \
        "          description: Seconds from last WAL prefetch stats reset.\n" \
        "    tag: pg_wal_prefetch_reset\n" \
        "    collector: wal_prefetch_reset\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 16\n" \
        "#\n" \
        "\n" \
        "# GSS API Credentials Delegated to DB Connection\n" \
        "  - queries:\n" \
        "    - query: SELECT COUNT(*)\n" \
        "              FROM pg_stat_gssapi\n" \
        "              JOIN pg_stat_activity\n" \
        "              ON pg_stat_gssapi.pid = pg_stat_activity.pid\n" \
        "              WHERE credentials_delegated = 't' AND datname IS NOT NULL;\n" \
        "      columns:\n" \
        "        - type: gauge\n" \
        "          description: Number of DB connections with delegated GSSAPI credentials.\n" \
        "      version: 16\n" \
        "    tag: pg_gssapi_credentials_delegated\n" \
        "    collector: gssapi_creds_delegated\n" \
        "\n" \
        "# pg_stat_io\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                backend_type,\n" \
        "                SUM(COALESCE(reads, 0)) AS reads,\n" \
        "                SUM(COALESCE(read_time, 0)) AS read_time,\n" \
        "                SUM(COALESCE(writes, 0)) AS writes,\n" \
        "                SUM(COALESCE(write_time, 0)) AS write_time,\n" \
        "                SUM(COALESCE(writebacks, 0)) AS writebacks,\n" \
        "                SUM(COALESCE(writeback_time, 0)) AS writeback_time,\n" \
        "                SUM(COALESCE(extends, 0)) AS extends,\n" \
        "                SUM(COALESCE(extend_time, 0)) AS extend_time,\n" \
        "                AVG(COALESCE(op_bytes, 0)) AS op_bytes,\n" \
        "                SUM(COALESCE(hits, 0)) AS hits,\n" \
        "                SUM(COALESCE(evictions, 0)) AS evictions,\n" \
        "                SUM(COALESCE(reuses, 0)) AS reuses,\n" \
        "                SUM(COALESCE(fsyncs, 0)) AS fsyncs,\n" \
        "                SUM(COALESCE(fsync_time, 0)) AS fsync_time\n" \
        "              FROM pg_stat_io\n" \
        "              GROUP BY backend_type;\n" \
        "      columns:\n" \
        "        - name: backend_type\n" \
        "          type: label\n" \
        "        - name: reads\n" \
        "          type: counter\n" \
        "          description: Number of read operations.\n" \
        "        - name: read_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on read operations in milliseconds.\n" \
        "        - name: writes\n" \
        "          type: counter\n" \
        "          description: Number of write operations.\n" \
        "        - name: write_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on write operations in milliseconds.\n" \
        "        - name: writebacks\n" \
        "          type: counter\n" \
        "          description: Number of writeback to permanent storage requests sent to kernel.\n" \
        "        - name: writeback_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on writeback operations in milliseconds.\n" \
        "        - name: extends\n" \
        "          type: counter\n" \
        "          description: Number of relation extend operations.\n" \
        "        - name: extend_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on relation extend operations in milliseconds.\n" \
        "        - name: op_bytes\n" \
        "          type: gauge\n" \
        "          description: Bytes per unit of I/O read, written or extended.\n" \
        "        - name: hits\n" \
        "          type: counter\n" \
        "          description: The number of times a desired block was found in shared buffer.\n" \
        "        - name: evictions\n" \
        "          type: counter\n" \
        "          description: The number of times a block has been written out from shared or local buffer in order to make it available for another use.\n" \
        "        - name: reuses\n" \
        "          type: counter\n" \
        "          description: The number of times an existing buffer in a size-limited ring buffer outside of shared buffers was reused as part of an I/O operation.\n" \
        "        - name: fsyncs\n" \
        "          type: counter\n" \
        "          description: Number of fsync calls.\n" \
        "        - name: fsync_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on fsync operations in milliseconds.\n" \
        "      version: 16\n" \
        "    tag: pg_stat_io\n" \
        "    collector: stat_io\n" \
        "\n" \
        "# stat_database_conflicts_information()\n" \
        "  - queries:\n" \
        "    - query: SELECT datname,\n" \
        "                confl_tablespace,\n" \
        "                confl_lock,\n" \
        "                confl_snapshot,\n" \
        "                confl_bufferpin,\n" \
        "                confl_deadlock\n" \
        "              FROM pg_stat_database_conflicts\n" \
        "              WHERE datname IS NOT NULL\n" \
        "              ORDER BY datname;\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - name: confl_tablespace\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_tablespace\n" \
        "        - name: confl_lock\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_lock\n" \
        "        - name: confl_snapshot\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_snapshot\n" \
        "        - name: confl_bufferpin\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_bufferpin\n" \
        "        - name: confl_deadlock\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_deadlock\n" \
        "      version: 10\n" \
        "\n" \
        "    - query: SELECT datname,\n" \
        "                confl_tablespace,\n" \
        "                confl_lock,\n" \
        "                confl_snapshot,\n" \
        "                confl_bufferpin,\n" \
        "                confl_deadlock,\n" \
        "                confl_active_logicalslot\n" \
        "              FROM pg_stat_database_conflicts\n" \
        "              WHERE datname IS NOT NULL\n" \
        "              ORDER BY datname;\n" \
        "      columns:\n" \
        "        - name: database\n" \
        "          type: label\n" \
        "        - name: confl_tablespace\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_tablespace\n" \
        "        - name: confl_lock\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_lock\n" \
        "        - name: confl_snapshot\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_snapshot\n" \
        "        - name: confl_bufferpin\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_bufferpin\n" \
        "        - name: confl_deadlock\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_dead\n" \
        "        - name: confl_active_logicalslot\n" \
        "          type: counter\n" \
        "          description: pg_stat_database_conflicts_confl_active_logicalslot\n" \
        "      version: 16\n" \
        "    tag: pg_stat_database_conflicts\n" \
        "    sort: data\n" \
        "    collector: stat_conflicts\n" \
        "\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                SUM(idx_scan) AS idx_scans,\n" \
        "                SUM(idx_tup_read) AS idx_tup_reads,\n" \
        "                SUM(idx_tup_fetch) AS idx_tup_fetchs,\n" \
        "                relname\n" \
        "              FROM pg_stat_all_indexes\n" \
        "              GROUP BY relname;\n" \
        "      columns:\n" \
        "        - type: counter\n" \
        "          name: idx_scans\n" \
        "          description: Number of index scans on the table's indexes.\n" \
        "        - type: counter\n" \
        "          name: idx_tup_reads\n" \
        "          description: Number of index entries returned by scans on the table's indexes.\n" \
        "        - type: counter\n" \
        "          name: idx_tup_fetchs\n" \
        "          description: Number of rows fetched by simple index scans on the table's indexes.\n" \
        "        - type: label\n" \
        "          name: relname\n" \
        "      version: 10\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                SUM(idx_scan) AS idx_scans,\n" \
        "                SUM(idx_tup_read) AS idx_tup_reads,\n" \
        "                SUM(idx_tup_fetch) AS idx_tup_fetchs,\n" \
        "                COALESCE(MIN((EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_idx_scan)) * 1000)::bigint), 0) AS time_elapsed_ms,\n" \
        "                relname\n" \
        "              FROM pg_stat_all_indexes\n" \
        "              WHERE (current_database() = 'postgres' OR schemaname NOT LIKE 'pg_%')\n" \
        "              GROUP BY relname;\n" \
        "      columns:\n" \
        "        - type: counter\n" \
        "          name: idx_scans\n" \
        "          description: Number of index scans on the table's indexes.\n" \
        "        - type: counter\n" \
        "          name: idx_tup_reads\n" \
        "          description: Number of index entries returned by scans on the table's indexes.\n" \
        "        - type: counter\n" \
        "          name: idx_tup_fetchs\n" \
        "          description: Number of rows fetched by simple index scans on the table's indexes.\n" \
        "        - type: counter\n" \
        "          name: time_elapsed_ms\n" \
        "          description: Milliseconds since last scan of an index in the table.\n" \
        "        - type: label\n" \
        "          name: relname\n" \
        "      version: 16\n" \
        "    tag: pg_stat_all_indexes\n" \
        "    sort: data\n" \
        "    collector: stat_all_indexes\n" \
        "    database: all\n" \
        "\n" \
        "#\n" \
        "# PostgreSQL 17\n" \
        "#\n" \
        "\n" \
        "\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                       type,\n" \
        "                       count(*) as count\n" \
        "               FROM pg_wait_events\n" \
        "               GROUP BY type\n" \
        "               ORDER BY count DESC;\n" \
        "      columns:\n" \
        "        - name: type\n" \
        "          type: label\n" \
        "        - name: count\n" \
        "          type: gauge\n" \
        "          description: pg_wait_events_count\n" \
        "      version: 17\n" \
        "    tag: pg_wait_events\n" \
        "    sort: data\n" \
        "    collector: wait_events \n" \
        "\n"
#ifdef __cplusplus
}
#endif

#endif
