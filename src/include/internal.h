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
        "\n" \
        "    - query: SELECT\n" \
        "                slot_name,\n" \
        "                slot_type,\n" \
        "                database,\n" \
        "                active,\n" \
        "                temporary,\n" \
        "                two_phase,\n" \
        "                two_phase_at,\n" \
        "                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - inactive_since))::bigint AS inactive_since_seconds,\n" \
        "                conflicting,\n" \
        "                invalidation_reason,\n" \
        "                failover,\n" \
        "                synced\n" \
        "              FROM pg_replication_slots;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "      - name: slot_name\n" \
        "        type: label\n" \
        "      - name: slot_type\n" \
        "        type: label\n" \
        "      - name: database\n" \
        "        type: label\n" \
        "      - name: active\n" \
        "        type: gauge\n" \
        "        description: Is the replication active\n" \
        "      - name: temporary\n" \
        "        type: gauge\n" \
        "        description: Is the replication temporary\n" \
        "      - name: two_phase\n" \
        "        type: gauge\n" \
        "        description: Is two-phase commit enabled for this slot\n" \
        "      - name: two_phase_at\n" \
        "        type: label\n" \
        "        description: LSN at which two-phase state was enabled\n" \
        "      - name: inactive_since_seconds\n" \
        "        type: gauge\n" \
        "        description: Seconds since slot became inactive (-1 if active or NULL)\n" \
        "      - name: conflicting\n" \
        "        type: gauge\n" \
        "        description: Is this slot conflicting with other slots\n" \
        "      - name: invalidation_reason\n" \
        "        type: label\n" \
        "        description: Reason slot was invalidated\n" \
        "      - name: failover\n" \
        "        type: gauge\n" \
        "        description: Is failover enabled for this slot\n" \
        "      - name: synced\n" \
        "        type: gauge\n" \
        "        description: Is this slot synced from primary\n" \
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
        "# Blocked vacuum operations\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                blocked.pid AS blocked_pid,\n" \
        "                blocked.datname AS blocked_database,\n" \
        "                blocked.usename AS blocked_user,\n" \
        "                blocked.query AS blocked_query,\n" \
        "                EXTRACT(EPOCH FROM age(now(), blocked.query_start))::bigint AS blocked_duration_seconds,\n" \
        "                blocking.pid AS blocking_pid,\n" \
        "                blocking.datname AS blocking_database,\n" \
        "                blocking.usename AS blocking_user,\n" \
        "                blocking.query AS blocking_query,\n" \
        "                EXTRACT(EPOCH FROM age(now(), blocking.query_start))::bigint AS blocking_duration_seconds\n" \
        "              FROM pg_stat_activity blocked\n" \
        "              JOIN pg_locks blocked_locks ON blocked.pid = blocked_locks.pid\n" \
        "              JOIN pg_locks blocking_locks ON blocked_locks.relation = blocking_locks.relation\n" \
        "                AND blocked_locks.pid != blocking_locks.pid\n" \
        "              JOIN pg_stat_activity blocking ON blocking.pid = blocking_locks.pid\n" \
        "              WHERE (blocked.query ILIKE '%VACUUM%' OR blocked.query ILIKE '%AUTOVACUUM%')\n" \
        "                AND NOT blocked_locks.granted\n" \
        "                AND blocking_locks.granted\n" \
        "              ORDER BY blocked_duration_seconds DESC;\n" \
        "      version: 10\n" \
        "      columns:\n" \
        "        - name: blocked_pid\n" \
        "          type: label\n" \
        "        - name: blocked_database\n" \
        "          type: label\n" \
        "        - name: blocked_user\n" \
        "          type: label\n" \
        "        - name: blocked_query\n" \
        "          type: label\n" \
        "        - name: blocked_duration_seconds\n" \
        "          type: gauge\n" \
        "          description: Duration in seconds that the vacuum has been blocked\n" \
        "        - name: blocking_pid\n" \
        "          type: label\n" \
        "        - name: blocking_database\n" \
        "          type: label\n" \
        "        - name: blocking_user\n" \
        "          type: label\n" \
        "        - name: blocking_query\n" \
        "          type: label\n" \
        "        - name: blocking_duration_seconds\n" \
        "          type: gauge\n" \
        "          description: Duration in seconds that the blocking query has been running\n" \
        "    tag: pg_blocked_vacuum\n" \
        "    sort: data\n" \
        "    collector: blocked_vacuum\n" \
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
        "# Shared memory allocations per NUMA node\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                numa_node,\n" \
        "                COUNT(*) AS allocations,\n" \
        "                SUM(size) AS total_bytes,\n" \
        "                AVG(size) AS avg_bytes,\n" \
        "                MAX(size) AS max_bytes,\n" \
        "                MIN(size) AS min_bytes\n" \
        "              FROM pg_shmem_allocations_numa\n" \
        "              GROUP BY numa_node\n" \
        "              ORDER BY numa_node;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: numa_node\n" \
        "          type: label\n" \
        "          description: NUMA node number\n" \
        "        - name: allocations\n" \
        "          type: gauge\n" \
        "          description: Number of shared memory allocations on this NUMA node\n" \
        "        - name: total_bytes\n" \
        "          type: gauge\n" \
        "          description: Total bytes allocated on this NUMA node\n" \
        "        - name: avg_bytes\n" \
        "          type: gauge\n" \
        "          description: Average allocation size on this NUMA node\n" \
        "        - name: max_bytes\n" \
        "          type: gauge\n" \
        "          description: Maximum allocation size on this NUMA node\n" \
        "        - name: min_bytes\n" \
        "          type: gauge\n" \
        "          description: Minimum allocation size on this NUMA node\n" \
        "    tag: pg_shmem_allocations_numa\n" \
        "    sort: data\n" \
        "    collector: shmem_numa\n" \
        "\n" \
        "# Vacuum progress information\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                p.pid,\n" \
        "                a.datname,\n" \
        "                a.usename,\n" \
        "                a.query,\n" \
        "                p.phase,\n" \
        "                p.heap_blks_total,\n" \
        "                p.heap_blks_scanned,\n" \
        "                p.heap_blks_vacuumed,\n" \
        "                p.index_vacuum_count,\n" \
        "                p.max_dead_tuples,\n" \
        "                p.num_dead_tuples,\n" \
        "                CASE WHEN p.heap_blks_total > 0\n" \
        "                     THEN round(100 * p.heap_blks_scanned / p.heap_blks_total, 2)\n" \
        "                     ELSE 0\n" \
        "                END AS pct_scan_completed\n" \
        "              FROM pg_stat_progress_vacuum p\n" \
        "              JOIN pg_stat_activity a USING (pid)\n" \
        "              ORDER BY p.pid;\n" \
        "      version: 13\n" \
        "      columns:\n" \
        "        - name: pid\n" \
        "          type: label\n" \
        "        - name: datname\n" \
        "          type: label\n" \
        "        - name: usename\n" \
        "          type: label\n" \
        "        - name: query\n" \
        "          type: label\n" \
        "        - name: phase\n" \
        "          type: label\n" \
        "        - name: heap_blks_total\n" \
        "          type: gauge\n" \
        "          description: Total heap blocks in table\n" \
        "        - name: heap_blks_scanned\n" \
        "          type: gauge\n" \
        "          description: Heap blocks scanned\n" \
        "        - name: heap_blks_vacuumed\n" \
        "          type: gauge\n" \
        "          description: Heap blocks vacuumed\n" \
        "        - name: index_vacuum_count\n" \
        "          type: gauge\n" \
        "          description: Number of index vacuum cycles completed\n" \
        "        - name: max_dead_tuples\n" \
        "          type: gauge\n" \
        "          description: Maximum dead tuples\n" \
        "        - name: num_dead_tuples\n" \
        "          type: gauge\n" \
        "          description: Current dead tuples\n" \
        "        - name: pct_scan_completed\n" \
        "          type: gauge\n" \
        "          description: Vacuum scan progress percentage\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                p.pid,\n" \
        "                a.datname,\n" \
        "                a.usename,\n" \
        "                a.query,\n" \
        "                p.phase,\n" \
        "                p.heap_blks_total,\n" \
        "                p.heap_blks_scanned,\n" \
        "                p.heap_blks_vacuumed,\n" \
        "                p.index_vacuum_count,\n" \
        "                p.max_dead_tuple_bytes,\n" \
        "                p.dead_tuple_bytes,\n" \
        "                p.num_dead_item_ids,\n" \
        "                p.indexes_total,\n" \
        "                p.indexes_processed,\n" \
        "                CASE WHEN p.heap_blks_total > 0\n" \
        "                     THEN round(100 * p.heap_blks_scanned / p.heap_blks_total, 2)\n" \
        "                     ELSE 0\n" \
        "                END AS pct_scan_completed,\n" \
        "                CASE WHEN p.indexes_total > 0\n" \
        "                     THEN round(100 * p.indexes_processed / p.indexes_total, 2)\n" \
        "                     ELSE 0\n" \
        "                END AS pct_index_completed\n" \
        "              FROM pg_stat_progress_vacuum p\n" \
        "              JOIN pg_stat_activity a USING (pid)\n" \
        "              ORDER BY p.pid;\n" \
        "      version: 17\n" \
        "      columns:\n" \
        "        - name: pid\n" \
        "          type: label\n" \
        "        - name: datname\n" \
        "          type: label\n" \
        "        - name: usename\n" \
        "          type: label\n" \
        "        - name: query\n" \
        "          type: label\n" \
        "        - name: phase\n" \
        "          type: label\n" \
        "        - name: heap_blks_total\n" \
        "          type: gauge\n" \
        "          description: Total heap blocks in table\n" \
        "        - name: heap_blks_scanned\n" \
        "          type: gauge\n" \
        "          description: Heap blocks scanned\n" \
        "        - name: heap_blks_vacuumed\n" \
        "          type: gauge\n" \
        "          description: Heap blocks vacuumed\n" \
        "        - name: index_vacuum_count\n" \
        "          type: gauge\n" \
        "          description: Number of index vacuum cycles completed\n" \
        "        - name: max_dead_tuple_bytes\n" \
        "          type: gauge\n" \
        "          description: Maximum dead tuple bytes\n" \
        "        - name: dead_tuple_bytes\n" \
        "          type: gauge\n" \
        "          description: Current dead tuple bytes\n" \
        "        - name: num_dead_item_ids\n" \
        "          type: gauge\n" \
        "          description: Number of dead item identifiers\n" \
        "        - name: indexes_total\n" \
        "          type: gauge\n" \
        "          description: Total number of indexes\n" \
        "        - name: indexes_processed\n" \
        "          type: gauge\n" \
        "          description: Number of indexes processed\n" \
        "        - name: pct_scan_completed\n" \
        "          type: gauge\n" \
        "          description: Vacuum scan progress percentage\n" \
        "        - name: pct_index_completed\n" \
        "          type: gauge\n" \
        "          description: Index processing progress percentage\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                p.pid,\n" \
        "                a.datname,\n" \
        "                a.usename,\n" \
        "                a.query,\n" \
        "                p.phase,\n" \
        "                p.heap_blks_total,\n" \
        "                p.heap_blks_scanned,\n" \
        "                p.heap_blks_vacuumed,\n" \
        "                p.index_vacuum_count,\n" \
        "                p.max_dead_tuple_bytes,\n" \
        "                p.dead_tuple_bytes,\n" \
        "                p.num_dead_item_ids,\n" \
        "                p.indexes_total,\n" \
        "                p.indexes_processed,\n" \
        "                p.delay_time,\n" \
        "                CASE WHEN p.heap_blks_total > 0\n" \
        "                     THEN round(100 * p.heap_blks_scanned / p.heap_blks_total, 2)\n" \
        "                     ELSE 0\n" \
        "                END AS pct_scan_completed,\n" \
        "                CASE WHEN p.indexes_total > 0\n" \
        "                     THEN round(100 * p.indexes_processed / p.indexes_total, 2)\n" \
        "                     ELSE 0\n" \
        "                END AS pct_index_completed\n" \
        "              FROM pg_stat_progress_vacuum p\n" \
        "              JOIN pg_stat_activity a USING (pid)\n" \
        "              ORDER BY p.pid;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: pid\n" \
        "          type: label\n" \
        "        - name: datname\n" \
        "          type: label\n" \
        "        - name: usename\n" \
        "          type: label\n" \
        "        - name: query\n" \
        "          type: label\n" \
        "        - name: phase\n" \
        "          type: label\n" \
        "        - name: heap_blks_total\n" \
        "          type: gauge\n" \
        "          description: Total heap blocks in table\n" \
        "        - name: heap_blks_scanned\n" \
        "          type: gauge\n" \
        "          description: Heap blocks scanned\n" \
        "        - name: heap_blks_vacuumed\n" \
        "          type: gauge\n" \
        "          description: Heap blocks vacuumed\n" \
        "        - name: index_vacuum_count\n" \
        "          type: gauge\n" \
        "          description: Number of index vacuum cycles completed\n" \
        "        - name: max_dead_tuple_bytes\n" \
        "          type: gauge\n" \
        "          description: Maximum dead tuple bytes\n" \
        "        - name: dead_tuple_bytes\n" \
        "          type: gauge\n" \
        "          description: Current dead tuple bytes\n" \
        "        - name: num_dead_item_ids\n" \
        "          type: gauge\n" \
        "          description: Number of dead item identifiers\n" \
        "        - name: indexes_total\n" \
        "          type: gauge\n" \
        "          description: Total number of indexes\n" \
        "        - name: indexes_processed\n" \
        "          type: gauge\n" \
        "          description: Number of indexes processed\n" \
        "        - name: delay_time\n" \
        "          type: gauge\n" \
        "          description: Total time spent in vacuum delay/throttling (milliseconds)\n" \
        "        - name: pct_scan_completed\n" \
        "          type: gauge\n" \
        "          description: Vacuum scan progress percentage\n" \
        "        - name: pct_index_completed\n" \
        "          type: gauge\n" \
        "          description: Index processing progress percentage\n" \
        "    tag: pg_stat_progress_vacuum\n" \
        "    sort: data\n" \
        "    collector: vacuum_progress\n" \
        "\n" \
        "# ANALYZE progress information\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                p.pid,\n" \
        "                a.datname,\n" \
        "                a.usename,\n" \
        "                p.phase,\n" \
        "                p.sample_blks_total,\n" \
        "                p.sample_blks_scanned,\n" \
        "                p.ext_stats_total,\n" \
        "                p.ext_stats_computed,\n" \
        "                p.child_tables_total,\n" \
        "                p.child_tables_done,\n" \
        "                CASE\n" \
        "                  WHEN p.sample_blks_total > 0\n" \
        "                  THEN ROUND(100 * p.sample_blks_scanned / p.sample_blks_total, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS pct_sample_completed,\n" \
        "                CASE\n" \
        "                  WHEN p.child_tables_total > 0\n" \
        "                  THEN ROUND(100 * p.child_tables_done / p.child_tables_total, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS pct_child_tables_completed\n" \
        "              FROM pg_stat_progress_analyze p\n" \
        "              JOIN pg_stat_activity a USING (pid)\n" \
        "              ORDER BY p.pid;\n" \
        "      version: 13\n" \
        "      columns:\n" \
        "        - name: pid\n" \
        "          type: label\n" \
        "        - name: datname\n" \
        "          type: label\n" \
        "        - name: usename\n" \
        "          type: label\n" \
        "        - name: phase\n" \
        "          type: label\n" \
        "        - name: sample_blks_total\n" \
        "          type: gauge\n" \
        "          description: Total blocks to sample\n" \
        "        - name: sample_blks_scanned\n" \
        "          type: gauge\n" \
        "          description: Blocks sampled so far\n" \
        "        - name: ext_stats_total\n" \
        "          type: gauge\n" \
        "          description: Total extended statistics to compute\n" \
        "        - name: ext_stats_computed\n" \
        "          type: gauge\n" \
        "          description: Extended statistics computed\n" \
        "        - name: child_tables_total\n" \
        "          type: gauge\n" \
        "          description: Total child tables to analyze\n" \
        "        - name: child_tables_done\n" \
        "          type: gauge\n" \
        "          description: Child tables analyzed\n" \
        "        - name: pct_sample_completed\n" \
        "          type: gauge\n" \
        "          description: Percentage of sampling completed\n" \
        "        - name: pct_child_tables_completed\n" \
        "          type: gauge\n" \
        "          description: Percentage of child tables completed\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                p.pid,\n" \
        "                a.datname,\n" \
        "                a.usename,\n" \
        "                p.phase,\n" \
        "                p.sample_blks_total,\n" \
        "                p.sample_blks_scanned,\n" \
        "                p.ext_stats_total,\n" \
        "                p.ext_stats_computed,\n" \
        "                p.child_tables_total,\n" \
        "                p.child_tables_done,\n" \
        "                p.delay_time,\n" \
        "                CASE\n" \
        "                  WHEN p.sample_blks_total > 0\n" \
        "                  THEN ROUND(100 * p.sample_blks_scanned / p.sample_blks_total, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS pct_sample_completed,\n" \
        "                CASE\n" \
        "                  WHEN p.child_tables_total > 0\n" \
        "                  THEN ROUND(100 * p.child_tables_done / p.child_tables_total, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS pct_child_tables_completed\n" \
        "              FROM pg_stat_progress_analyze p\n" \
        "              JOIN pg_stat_activity a USING (pid)\n" \
        "              ORDER BY p.pid;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: pid\n" \
        "          type: label\n" \
        "        - name: datname\n" \
        "          type: label\n" \
        "        - name: usename\n" \
        "          type: label\n" \
        "        - name: phase\n" \
        "          type: label\n" \
        "        - name: sample_blks_total\n" \
        "          type: gauge\n" \
        "          description: Total blocks to sample\n" \
        "        - name: sample_blks_scanned\n" \
        "          type: gauge\n" \
        "          description: Blocks sampled so far\n" \
        "        - name: ext_stats_total\n" \
        "          type: gauge\n" \
        "          description: Total extended statistics to compute\n" \
        "        - name: ext_stats_computed\n" \
        "          type: gauge\n" \
        "          description: Extended statistics computed\n" \
        "        - name: child_tables_total\n" \
        "          type: gauge\n" \
        "          description: Total child tables to analyze\n" \
        "        - name: child_tables_done\n" \
        "          type: gauge\n" \
        "          description: Child tables analyzed\n" \
        "        - name: delay_time\n" \
        "          type: gauge\n" \
        "          description: Total time spent in analyze delay/throttling (milliseconds)\n" \
        "        - name: pct_sample_completed\n" \
        "          type: gauge\n" \
        "          description: Percentage of sampling completed\n" \
        "        - name: pct_child_tables_completed\n" \
        "          type: gauge\n" \
        "          description: Percentage of child tables completed\n" \
        "    tag: pg_stat_progress_analyze\n" \
        "    sort: data\n" \
        "    collector: analyze_progress\n" \
        "\n" \
        "# Table-level vacuum and analyze statistics\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                schemaname,\n" \
        "                relname,\n" \
        "                n_live_tup,\n" \
        "                n_dead_tup,\n" \
        "                CASE \n" \
        "                  WHEN n_live_tup > 0 \n" \
        "                  THEN ROUND((n_dead_tup::numeric / n_live_tup::numeric) * 100, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS dead_rows_pct,\n" \
        "                n_mod_since_analyze,\n" \
        "                n_ins_since_vacuum,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_vacuum))::bigint, -1) AS last_vacuum_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_autovacuum))::bigint, -1) AS last_autovacuum_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_analyze))::bigint, -1) AS last_analyze_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_autoanalyze))::bigint, -1) AS last_autoanalyze_seconds,\n" \
        "                vacuum_count,\n" \
        "                autovacuum_count,\n" \
        "                analyze_count,\n" \
        "                autoanalyze_count\n" \
        "              FROM pg_stat_user_tables\n" \
        "              ORDER BY n_dead_tup DESC;\n" \
        "      version: 13\n" \
        "      columns:\n" \
        "        - name: schemaname\n" \
        "          type: label\n" \
        "        - name: relname\n" \
        "          type: label\n" \
        "        - name: n_live_tup\n" \
        "          type: gauge\n" \
        "          description: Estimated number of live rows\n" \
        "        - name: n_dead_tup\n" \
        "          type: gauge\n" \
        "          description: Estimated number of dead rows\n" \
        "        - name: dead_rows_pct\n" \
        "          type: gauge\n" \
        "          description: Percentage of dead rows relative to live rows\n" \
        "        - name: n_mod_since_analyze\n" \
        "          type: gauge\n" \
        "          description: Estimated number of rows modified since last analyze\n" \
        "        - name: n_ins_since_vacuum\n" \
        "          type: gauge\n" \
        "          description: Estimated number of rows inserted since last vacuum\n" \
        "        - name: last_vacuum_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last manual vacuum (-1 if never)\n" \
        "        - name: last_autovacuum_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last autovacuum (-1 if never)\n" \
        "        - name: last_analyze_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last manual analyze (-1 if never)\n" \
        "        - name: last_autoanalyze_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last autoanalyze (-1 if never)\n" \
        "        - name: vacuum_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been manually vacuumed\n" \
        "        - name: autovacuum_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been vacuumed by autovacuum\n" \
        "        - name: analyze_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been manually analyzed\n" \
        "        - name: autoanalyze_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been analyzed by autoanalyze\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                schemaname,\n" \
        "                relname,\n" \
        "                seq_scan,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_seq_scan))::bigint, -1) AS last_seq_scan_seconds,\n" \
        "                seq_tup_read,\n" \
        "                idx_scan,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_idx_scan))::bigint, -1) AS last_idx_scan_seconds,\n" \
        "                idx_tup_fetch,\n" \
        "                n_tup_ins,\n" \
        "                n_tup_upd,\n" \
        "                n_tup_del,\n" \
        "                n_tup_hot_upd,\n" \
        "                n_tup_newpage_upd,\n" \
        "                n_live_tup,\n" \
        "                n_dead_tup,\n" \
        "                CASE \n" \
        "                  WHEN n_live_tup > 0 \n" \
        "                  THEN ROUND((n_dead_tup::numeric / n_live_tup::numeric) * 100, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS dead_rows_pct,\n" \
        "                n_mod_since_analyze,\n" \
        "                n_ins_since_vacuum,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_vacuum))::bigint, -1) AS last_vacuum_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_autovacuum))::bigint, -1) AS last_autovacuum_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_analyze))::bigint, -1) AS last_analyze_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_autoanalyze))::bigint, -1) AS last_autoanalyze_seconds,\n" \
        "                vacuum_count,\n" \
        "                autovacuum_count,\n" \
        "                analyze_count,\n" \
        "                autoanalyze_count\n" \
        "              FROM pg_stat_user_tables\n" \
        "              ORDER BY n_dead_tup DESC;\n" \
        "      version: 16\n" \
        "      columns:\n" \
        "        - name: schemaname\n" \
        "          type: label\n" \
        "        - name: relname\n" \
        "          type: label\n" \
        "        - name: seq_scan\n" \
        "          type: counter\n" \
        "          description: Number of sequential scans initiated on this table\n" \
        "        - name: last_seq_scan_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last sequential scan (-1 if never)\n" \
        "        - name: seq_tup_read\n" \
        "          type: counter\n" \
        "          description: Number of live rows fetched by sequential scans\n" \
        "        - name: idx_scan\n" \
        "          type: counter\n" \
        "          description: Number of index scans initiated on this table\n" \
        "        - name: last_idx_scan_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last index scan (-1 if never)\n" \
        "        - name: idx_tup_fetch\n" \
        "          type: counter\n" \
        "          description: Number of live rows fetched by index scans\n" \
        "        - name: n_tup_ins\n" \
        "          type: counter\n" \
        "          description: Number of rows inserted\n" \
        "        - name: n_tup_upd\n" \
        "          type: counter\n" \
        "          description: Number of rows updated\n" \
        "        - name: n_tup_del\n" \
        "          type: counter\n" \
        "          description: Number of rows deleted\n" \
        "        - name: n_tup_hot_upd\n" \
        "          type: counter\n" \
        "          description: Number of rows HOT updated\n" \
        "        - name: n_tup_newpage_upd\n" \
        "          type: counter\n" \
        "          description: Number of rows updated where successor goes to new heap page\n" \
        "        - name: n_live_tup\n" \
        "          type: gauge\n" \
        "          description: Estimated number of live rows\n" \
        "        - name: n_dead_tup\n" \
        "          type: gauge\n" \
        "          description: Estimated number of dead rows\n" \
        "        - name: dead_rows_pct\n" \
        "          type: gauge\n" \
        "          description: Percentage of dead rows relative to live rows\n" \
        "        - name: n_mod_since_analyze\n" \
        "          type: gauge\n" \
        "          description: Estimated number of rows modified since last analyze\n" \
        "        - name: n_ins_since_vacuum\n" \
        "          type: gauge\n" \
        "          description: Estimated number of rows inserted since last vacuum\n" \
        "        - name: last_vacuum_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last manual vacuum (-1 if never)\n" \
        "        - name: last_autovacuum_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last autovacuum (-1 if never)\n" \
        "        - name: last_analyze_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last manual analyze (-1 if never)\n" \
        "        - name: last_autoanalyze_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last autoanalyze (-1 if never)\n" \
        "        - name: vacuum_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been manually vacuumed\n" \
        "        - name: autovacuum_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been vacuumed by autovacuum\n" \
        "        - name: analyze_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been manually analyzed\n" \
        "        - name: autoanalyze_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been analyzed by autoanalyze\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                schemaname,\n" \
        "                relname,\n" \
        "                seq_scan,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_seq_scan))::bigint, -1) AS last_seq_scan_seconds,\n" \
        "                seq_tup_read,\n" \
        "                idx_scan,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_idx_scan))::bigint, -1) AS last_idx_scan_seconds,\n" \
        "                idx_tup_fetch,\n" \
        "                n_tup_ins,\n" \
        "                n_tup_upd,\n" \
        "                n_tup_del,\n" \
        "                n_tup_hot_upd,\n" \
        "                n_tup_newpage_upd,\n" \
        "                n_live_tup,\n" \
        "                n_dead_tup,\n" \
        "                CASE\n" \
        "                  WHEN n_live_tup > 0\n" \
        "                  THEN ROUND((n_dead_tup::numeric / n_live_tup::numeric) * 100, 2)\n" \
        "                  ELSE 0\n" \
        "                END AS dead_rows_pct,\n" \
        "                n_mod_since_analyze,\n" \
        "                n_ins_since_vacuum,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_vacuum))::bigint, -1) AS last_vacuum_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_autovacuum))::bigint, -1) AS last_autovacuum_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_analyze))::bigint, -1) AS last_analyze_seconds,\n" \
        "                COALESCE(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_autoanalyze))::bigint, -1) AS last_autoanalyze_seconds,\n" \
        "                vacuum_count,\n" \
        "                autovacuum_count,\n" \
        "                analyze_count,\n" \
        "                autoanalyze_count,\n" \
        "                total_vacuum_time,\n" \
        "                total_autovacuum_time,\n" \
        "                total_analyze_time,\n" \
        "                total_autoanalyze_time\n" \
        "              FROM pg_stat_user_tables\n" \
        "              ORDER BY n_dead_tup DESC;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: schemaname\n" \
        "          type: label\n" \
        "        - name: relname\n" \
        "          type: label\n" \
        "        - name: seq_scan\n" \
        "          type: counter\n" \
        "          description: Number of sequential scans initiated on this table\n" \
        "        - name: last_seq_scan_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last sequential scan (-1 if never)\n" \
        "        - name: seq_tup_read\n" \
        "          type: counter\n" \
        "          description: Number of live rows fetched by sequential scans\n" \
        "        - name: idx_scan\n" \
        "          type: counter\n" \
        "          description: Number of index scans initiated on this table\n" \
        "        - name: last_idx_scan_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last index scan (-1 if never)\n" \
        "        - name: idx_tup_fetch\n" \
        "          type: counter\n" \
        "          description: Number of live rows fetched by index scans\n" \
        "        - name: n_tup_ins\n" \
        "          type: counter\n" \
        "          description: Number of rows inserted\n" \
        "        - name: n_tup_upd\n" \
        "          type: counter\n" \
        "          description: Number of rows updated\n" \
        "        - name: n_tup_del\n" \
        "          type: counter\n" \
        "          description: Number of rows deleted\n" \
        "        - name: n_tup_hot_upd\n" \
        "          type: counter\n" \
        "          description: Number of rows HOT updated\n" \
        "        - name: n_tup_newpage_upd\n" \
        "          type: counter\n" \
        "          description: Number of rows updated where successor goes to new heap page\n" \
        "        - name: n_live_tup\n" \
        "          type: gauge\n" \
        "          description: Estimated number of live rows\n" \
        "        - name: n_dead_tup\n" \
        "          type: gauge\n" \
        "          description: Estimated number of dead rows\n" \
        "        - name: dead_rows_pct\n" \
        "          type: gauge\n" \
        "          description: Percentage of dead rows relative to live rows\n" \
        "        - name: n_mod_since_analyze\n" \
        "          type: gauge\n" \
        "          description: Estimated number of rows modified since last analyze\n" \
        "        - name: n_ins_since_vacuum\n" \
        "          type: gauge\n" \
        "          description: Estimated number of rows inserted since last vacuum\n" \
        "        - name: last_vacuum_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last manual vacuum (-1 if never)\n" \
        "        - name: last_autovacuum_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last autovacuum (-1 if never)\n" \
        "        - name: last_analyze_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last manual analyze (-1 if never)\n" \
        "        - name: last_autoanalyze_seconds\n" \
        "          type: gauge\n" \
        "          description: Seconds since last autoanalyze (-1 if never)\n" \
        "        - name: vacuum_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been manually vacuumed\n" \
        "        - name: autovacuum_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been vacuumed by autovacuum\n" \
        "        - name: analyze_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been manually analyzed\n" \
        "        - name: autoanalyze_count\n" \
        "          type: counter\n" \
        "          description: Number of times this table has been analyzed by autoanalyze\n" \
        "        - name: total_vacuum_time\n" \
        "          type: counter\n" \
        "          description: Total time spent vacuuming this table, in milliseconds\n" \
        "        - name: total_autovacuum_time\n" \
        "          type: counter\n" \
        "          description: Total time spent auto-vacuuming this table, in milliseconds\n" \
        "        - name: total_analyze_time\n" \
        "          type: counter\n" \
        "          description: Total time spent analyzing this table, in milliseconds\n" \
        "        - name: total_autoanalyze_time\n" \
        "          type: counter\n" \
        "          description: Total time spent auto-analyzing this table, in milliseconds\n" \
        "    tag: pg_stat_user_tables_vacuum\n" \
        "    sort: data\n" \
        "    collector: stat_user_tables\n" \
        "    database: all\n" \
        "\n" \
        "# Table bloat analysis\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                schemaname,\n" \
        "                tblname,\n" \
        "                real_size AS table_size,\n" \
        "                extra_size AS bloat_size,\n" \
        "                ROUND(extra_ratio::numeric, 2) AS bloat_ratio_pct,\n" \
        "                tblpages,\n" \
        "                est_tblpages,\n" \
        "                bs AS block_size\n" \
        "              FROM (\n" \
        "                SELECT\n" \
        "                  schemaname,\n" \
        "                  tblname,\n" \
        "                  bs*tblpages AS real_size,\n" \
        "                  (tblpages-est_tblpages)*bs AS extra_size,\n" \
        "                  CASE WHEN tblpages > 0\n" \
        "                    THEN 100 * (tblpages-est_tblpages)/tblpages::numeric\n" \
        "                    ELSE 0\n" \
        "                  END AS extra_ratio,\n" \
        "                  tblpages,\n" \
        "                  est_tblpages,\n" \
        "                  bs\n" \
        "                FROM (\n" \
        "                  SELECT\n" \
        "                    n.nspname AS schemaname,\n" \
        "                    c.relname AS tblname,\n" \
        "                    c.relpages AS tblpages,\n" \
        "                    CEIL(c.reltuples/((current_setting('block_size')::integer-24)/100)) AS est_tblpages,\n" \
        "                    current_setting('block_size')::integer AS bs\n" \
        "                  FROM pg_class c\n" \
        "                  JOIN pg_namespace n ON n.oid = c.relnamespace\n" \
        "                  WHERE c.relkind = 'r'\n" \
        "                  AND n.nspname NOT IN ('information_schema', 'pg_catalog', 'pg_toast')\n" \
        "                  AND n.nspname NOT LIKE 'pg_temp_%'\n" \
        "                ) AS bloat_calc\n" \
        "              ) AS bloat_summary\n" \
        "              WHERE tblpages > 0\n" \
        "              ORDER BY extra_size DESC;\n" \
        "      version: 13\n" \
        "      columns:\n" \
        "        - name: schemaname\n" \
        "          type: label\n" \
        "        - name: tblname\n" \
        "          type: label\n" \
        "        - name: table_size\n" \
        "          type: gauge\n" \
        "          description: Actual table size in bytes\n" \
        "        - name: bloat_size\n" \
        "          type: gauge\n" \
        "          description: Estimated bloat size in bytes\n" \
        "        - name: bloat_ratio_pct\n" \
        "          type: gauge\n" \
        "          description: Bloat ratio as percentage\n" \
        "        - name: tblpages\n" \
        "          type: gauge\n" \
        "          description: Actual number of pages used by table\n" \
        "        - name: est_tblpages\n" \
        "          type: gauge\n" \
        "          description: Estimated number of pages needed\n" \
        "        - name: block_size\n" \
        "          type: gauge\n" \
        "          description: Database block size in bytes\n" \
        "    tag: pg_table_bloat\n" \
        "    sort: data\n" \
        "    collector: table_bloat\n" \
        "    database: all\n" \
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
        "\n" \
        "    - query: SELECT\n" \
        "                COUNT(*) AS contexts,\n" \
        "                type,\n" \
        "                SUM(free_bytes) AS free_bytes,\n" \
        "                SUM(used_bytes) AS used_bytes,\n" \
        "                SUM(total_bytes) AS total_bytes\n" \
        "              FROM pg_backend_memory_contexts\n" \
        "              WHERE type!=''\n" \
        "              GROUP BY type;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: contexts\n" \
        "          type: gauge\n" \
        "          description: Number of memory contexts per type.\n" \
        "        - name: type\n" \
        "          type: label\n" \
        "          description: Memory context type\n" \
        "        - name: free_bytes\n" \
        "          type: gauge\n" \
        "          description: Free bytes per memory context type.\n" \
        "        - name: used_bytes\n" \
        "          type: gauge\n" \
        "          description: Used bytes per memory context type.\n" \
        "        - name: total_bytes\n" \
        "          type: gauge\n" \
        "          description: Total bytes per memory context type.\n" \
        "    tag: pg_mem_ctx\n" \
        "    collector: mem_ctx\n" \
        "\n" \
        "# WAL statistics\n" \
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
        "\n" \
        "    - query: WITH wal_io AS (\n" \
        "                SELECT\n" \
        "                  sum(writes) as wal_write,\n" \
        "                  sum(fsyncs) as wal_sync,\n" \
        "                  sum(write_time) as wal_write_time,\n" \
        "                  sum(fsync_time) as wal_sync_time\n" \
        "                FROM pg_stat_io\n" \
        "                WHERE object = 'wal'\n" \
        "              )\n" \
        "              SELECT\n" \
        "                wal_records,\n" \
        "                wal_fpi,\n" \
        "                wal_bytes,\n" \
        "                wal_buffers_full,\n" \
        "                wal_write,\n" \
        "                wal_sync,\n" \
        "                wal_write_time,\n" \
        "                wal_sync_time\n" \
        "              FROM pg_stat_wal, wal_io;\n" \
        "      version: 18\n" \
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
        "          description: Number of times WAL files were written to disk (from pg_stat_io).\n" \
        "        - name: wal_sync\n" \
        "          type: counter\n" \
        "          description: Number of times WAL files were synced to disk (from pg_stat_io).\n" \
        "        - name: wal_write_time\n" \
        "          type: counter\n" \
        "          description: Time taken for WAL files to be written to disk in milliseconds (from pg_stat_io).\n" \
        "        - name: wal_sync_time\n" \
        "          type: counter\n" \
        "          description: Time taken for WAL files to be synced to disk in milliseconds (from pg_stat_io).\n" \
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
        "                sessions_killed,\n" \
        "                parallel_workers_to_launch,\n" \
        "                parallel_workers_launched\n" \
        "              FROM pg_stat_database\n" \
        "              WHERE datname IS NOT NULL\n" \
        "              ORDER BY datname;\n" \
        "      version: 18\n" \
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
        "        - name: parallel_workers_to_launch\n" \
        "          type: gauge\n" \
        "          description: Number of parallel workers PostgreSQL attempted to launch\n" \
        "        - name: parallel_workers_launched\n" \
        "          type: gauge\n" \
        "          description: Number of parallel workers successfully launched\n" \
        "    tag: pg_stat_database\n" \
        "    collector: stat_db\n" \
        "\n" \
        "# Logical replication subscription statistics\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                subid,\n" \
        "                subname,\n" \
        "                apply_error_count,\n" \
        "                sync_error_count,\n" \
        "                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - stats_reset))::bigint AS stats_reset_seconds\n" \
        "              FROM pg_stat_subscription_stats;\n" \
        "      version: 16\n" \
        "      columns:\n" \
        "        - name: subid\n" \
        "          type: label\n" \
        "          description: Subscription OID\n" \
        "        - name: subname\n" \
        "          type: label\n" \
        "          description: Subscription name\n" \
        "        - name: apply_error_count\n" \
        "          type: counter\n" \
        "          description: Number of errors during apply\n" \
        "        - name: sync_error_count\n" \
        "          type: counter\n" \
        "          description: Number of errors during initial sync\n" \
        "        - name: stats_reset_seconds\n" \
        "          type: counter\n" \
        "          description: Seconds since statistics were last reset\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                subid,\n" \
        "                subname,\n" \
        "                apply_error_count,\n" \
        "                sync_error_count,\n" \
        "                confl_insert_exists,\n" \
        "                confl_update_origin_differs,\n" \
        "                confl_update_exists,\n" \
        "                confl_update_missing,\n" \
        "                confl_delete_origin_differs,\n" \
        "                confl_delete_missing,\n" \
        "                confl_multiple_unique_conflicts,\n" \
        "                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - stats_reset))::bigint AS stats_reset_seconds\n" \
        "              FROM pg_stat_subscription_stats;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: subid\n" \
        "          type: label\n" \
        "          description: Subscription OID\n" \
        "        - name: subname\n" \
        "          type: label\n" \
        "          description: Subscription name\n" \
        "        - name: apply_error_count\n" \
        "          type: counter\n" \
        "          description: Number of errors during apply\n" \
        "        - name: sync_error_count\n" \
        "          type: counter\n" \
        "          description: Number of errors during initial sync\n" \
        "        - name: confl_insert_exists\n" \
        "          type: counter\n" \
        "          description: Number of conflicts where INSERT tried to insert existing row\n" \
        "        - name: confl_update_origin_differs\n" \
        "          type: counter\n" \
        "          description: Number of conflicts where UPDATE had different origin\n" \
        "        - name: confl_update_exists\n" \
        "          type: counter\n" \
        "          description: Number of conflicts where UPDATE target already exists\n" \
        "        - name: confl_update_missing\n" \
        "          type: counter\n" \
        "          description: Number of conflicts where UPDATE target is missing\n" \
        "        - name: confl_delete_origin_differs\n" \
        "          type: counter\n" \
        "          description: Number of conflicts where DELETE had different origin\n" \
        "        - name: confl_delete_missing\n" \
        "          type: counter\n" \
        "          description: Number of conflicts where DELETE target is missing\n" \
        "        - name: confl_multiple_unique_conflicts\n" \
        "          type: counter\n" \
        "          description: Number of conflicts with multiple unique constraint violations\n" \
        "        - name: stats_reset_seconds\n" \
        "          type: counter\n" \
        "          description: Seconds since statistics were last reset\n" \
        "    tag: pg_stat_subscription_stats\n" \
        "    sort: data\n" \
        "    collector: subscription_stats\n" \
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
        "\n" \
        "    - query: SELECT\n" \
        "                backend_type,\n" \
        "                SUM(COALESCE(reads, 0)) AS reads,\n" \
        "                SUM(COALESCE(read_bytes, 0)) AS read_bytes,\n" \
        "                SUM(COALESCE(read_time, 0)) AS read_time,\n" \
        "                SUM(COALESCE(writes, 0)) AS writes,\n" \
        "                SUM(COALESCE(write_bytes, 0)) AS write_bytes,\n" \
        "                SUM(COALESCE(write_time, 0)) AS write_time,\n" \
        "                SUM(COALESCE(writebacks, 0)) AS writebacks,\n" \
        "                SUM(COALESCE(writeback_time, 0)) AS writeback_time,\n" \
        "                SUM(COALESCE(extends, 0)) AS extends,\n" \
        "                SUM(COALESCE(extend_bytes, 0)) AS extend_bytes,\n" \
        "                SUM(COALESCE(extend_time, 0)) AS extend_time,\n" \
        "                SUM(COALESCE(hits, 0)) AS hits,\n" \
        "                SUM(COALESCE(evictions, 0)) AS evictions,\n" \
        "                SUM(COALESCE(reuses, 0)) AS reuses,\n" \
        "                SUM(COALESCE(fsyncs, 0)) AS fsyncs,\n" \
        "                SUM(COALESCE(fsync_time, 0)) AS fsync_time\n" \
        "              FROM pg_stat_io\n" \
        "              GROUP BY backend_type;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: backend_type\n" \
        "          type: label\n" \
        "        - name: reads\n" \
        "          type: counter\n" \
        "          description: Number of read operations.\n" \
        "        - name: read_bytes\n" \
        "          type: counter\n" \
        "          description: Total bytes read.\n" \
        "        - name: read_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on read operations in milliseconds.\n" \
        "        - name: writes\n" \
        "          type: counter\n" \
        "          description: Number of write operations.\n" \
        "        - name: write_bytes\n" \
        "          type: counter\n" \
        "          description: Total bytes written.\n" \
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
        "        - name: extend_bytes\n" \
        "          type: counter\n" \
        "          description: Total bytes extended.\n" \
        "        - name: extend_time\n" \
        "          type: counter\n" \
        "          description: Total time spent on relation extend operations in milliseconds.\n" \
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
        "# Checkpointer statistics\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                num_timed,\n" \
        "                num_requested,\n" \
        "                restartpoints_timed,\n" \
        "                restartpoints_req,\n" \
        "                restartpoints_done,\n" \
        "                write_time,\n" \
        "                sync_time,\n" \
        "                buffers_written,\n" \
        "                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - stats_reset))::bigint AS stats_reset_seconds\n" \
        "              FROM pg_stat_checkpointer;\n" \
        "      version: 17\n" \
        "      columns:\n" \
        "        - name: num_timed\n" \
        "          type: counter\n" \
        "          description: Number of scheduled checkpoints performed\n" \
        "        - name: num_requested\n" \
        "          type: counter\n" \
        "          description: Number of requested checkpoints performed\n" \
        "        - name: restartpoints_timed\n" \
        "          type: counter\n" \
        "          description: Number of scheduled restartpoints on replica\n" \
        "        - name: restartpoints_req\n" \
        "          type: counter\n" \
        "          description: Number of requested restartpoints on replica\n" \
        "        - name: restartpoints_done\n" \
        "          type: counter\n" \
        "          description: Number of restartpoints completed on replica\n" \
        "        - name: write_time\n" \
        "          type: counter\n" \
        "          description: Total time spent writing buffers during checkpoints (ms)\n" \
        "        - name: sync_time\n" \
        "          type: counter\n" \
        "          description: Total time spent syncing buffers during checkpoints (ms)\n" \
        "        - name: buffers_written\n" \
        "          type: counter\n" \
        "          description: Number of buffers written during checkpoints\n" \
        "        - name: stats_reset_seconds\n" \
        "          type: counter\n" \
        "          description: Seconds since statistics were last reset\n" \
        "\n" \
        "    - query: SELECT\n" \
        "                num_timed,\n" \
        "                num_requested,\n" \
        "                num_done,\n" \
        "                restartpoints_timed,\n" \
        "                restartpoints_req,\n" \
        "                restartpoints_done,\n" \
        "                write_time,\n" \
        "                sync_time,\n" \
        "                buffers_written,\n" \
        "                slru_written,\n" \
        "                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - stats_reset))::bigint AS stats_reset_seconds\n" \
        "              FROM pg_stat_checkpointer;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: num_timed\n" \
        "          type: counter\n" \
        "          description: Number of scheduled checkpoints performed\n" \
        "        - name: num_requested\n" \
        "          type: counter\n" \
        "          description: Number of requested checkpoints performed\n" \
        "        - name: num_done\n" \
        "          type: counter\n" \
        "          description: Total number of checkpoints completed\n" \
        "        - name: restartpoints_timed\n" \
        "          type: counter\n" \
        "          description: Number of scheduled restartpoints on replica\n" \
        "        - name: restartpoints_req\n" \
        "          type: counter\n" \
        "          description: Number of requested restartpoints on replica\n" \
        "        - name: restartpoints_done\n" \
        "          type: counter\n" \
        "          description: Number of restartpoints completed on replica\n" \
        "        - name: write_time\n" \
        "          type: counter\n" \
        "          description: Total time spent writing buffers during checkpoints (ms)\n" \
        "        - name: sync_time\n" \
        "          type: counter\n" \
        "          description: Total time spent syncing buffers during checkpoints (ms)\n" \
        "        - name: buffers_written\n" \
        "          type: counter\n" \
        "          description: Number of buffers written during checkpoints\n" \
        "        - name: slru_written\n" \
        "          type: counter\n" \
        "          description: Number of SLRU buffers written during checkpoints\n" \
        "        - name: stats_reset_seconds\n" \
        "          type: counter\n" \
        "          description: Seconds since statistics were last reset\n" \
        "    tag: pg_stat_checkpointer\n" \
        "    collector: stat_checkpointer\n" \
        "\n" \
        "# Asynchronous I/O operations\n" \
        "  - queries:\n" \
        "    - query: SELECT\n" \
        "                state,\n" \
        "                operation,\n" \
        "                COUNT(*) AS total_operations,\n" \
        "                SUM(length) AS total_bytes,\n" \
        "                AVG(length) AS avg_bytes_per_op,\n" \
        "                COUNT(*) FILTER (WHERE f_sync) AS sync_operations,\n" \
        "                COUNT(*) FILTER (WHERE f_buffered) AS buffered_operations\n" \
        "              FROM pg_aios\n" \
        "              GROUP BY state, operation;\n" \
        "      version: 18\n" \
        "      columns:\n" \
        "        - name: state\n" \
        "          type: label\n" \
        "          description: State of async I/O operation (in_progress, complete, error)\n" \
        "        - name: operation\n" \
        "          type: label\n" \
        "          description: Type of I/O operation (read, write, fsync, etc.)\n" \
        "        - name: total_operations\n" \
        "          type: gauge\n" \
        "          description: Total number of async I/O operations in this state\n" \
        "        - name: total_bytes\n" \
        "          type: gauge\n" \
        "          description: Total bytes involved in operations\n" \
        "        - name: avg_bytes_per_op\n" \
        "          type: gauge\n" \
        "          description: Average bytes per operation\n" \
        "        - name: sync_operations\n" \
        "          type: gauge\n" \
        "          description: Number of operations with sync flag\n" \
        "        - name: buffered_operations\n" \
        "          type: gauge\n" \
        "          description: Number of buffered operations\n" \
        "    tag: pg_aios\n" \
        "    sort: data\n" \
        "    collector: aios\n" \
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
