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

#ifndef PGEXPORTER_INTERNAL_H
#define PGEXPORTER_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#define INTERNAL_YAML "" \
   "version: 10\n" \
   "metrics:\n" \
   "  - queries:\n" \
   "    - query: SELECT datname, pg_database_size(datname) FROM pg_database;\n" \
   "      columns:\n" \
   "        - name: database\n" \
   "          type: label\n" \
   "        - description: Size of the database\n" \
   "          type: gauge\n" \
   "    tag: pg_database_size\n" \
   "    sort: data\n" \
   "    collector: db\n" \
   "\n" \
   "  - queries:\n" \
   "    - query: SELECT pg_database.datname as database, tmp.mode, COALESCE(count, 0) as count\n" \
   "              FROM\n" \
   "              (\n" \
   "              VALUES ('accesssharelock'),\n" \
   "              ('rowsharelock'),\n" \
   "              ('rowexclusivelock'),\n" \
   "              ('shareupdateexclusivelock'),\n" \
   "              ('sharelock'),\n" \
   "              ('sharerowexclusivelock'),\n" \
   "              ('exclusivelock'),\n" \
   "              ('accessexclusivelock'),\n" \
   "              ('sireadlock')\n" \
   "              ) AS tmp(mode) CROSS JOIN pg_database\n" \
   "              LEFT JOIN\n" \
   "              (SELECT database, lower(mode) AS mode, count(*) AS count\n" \
   "              FROM pg_locks WHERE database IS NOT NULL\n" \
   "              GROUP BY database, lower(mode)\n" \
   "              ) AS tmp2\n" \
   "              ON tmp.mode = tmp2.mode and pg_database.oid = tmp2.database ORDER BY 1, 2;\n" \
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
   "  - queries:\n" \
   "    - query: SELECT slot_name,active,temporary FROM pg_replication_slots;\n" \
   "      columns:\n" \
   "      - name: slot_name\n" \
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
   "  - queries:\n" \
   "    - query: SELECT buffers_alloc, buffers_backend, buffers_backend_fsync,\n" \
   "              buffers_checkpoint, buffers_clean, checkpoint_sync_time,\n" \
   "              checkpoint_write_time, checkpoints_req, checkpoints_timed,\n" \
   "              maxwritten_clean\n" \
   "              FROM pg_stat_bgwriter;\n" \
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
   "    tag: pg_stat_bgwriter\n" \
   "    sort: name\n" \
   "    collector: stat_bgwriter\n" \
   "\n" \
   "  - queries:\n" \
   "    - query: SELECT datname, blk_read_time, blk_write_time,\n" \
   "              blks_hit, blks_read, checksum_failures\n" \
   "              deadlocks, temp_files, temp_bytes,\n" \
   "              tup_returned, tup_fetched, tup_inserted,\n" \
   "              tup_updated, tup_deleted, xact_commit,\n" \
   "              xact_rollback, conflicts, numbackends\n" \
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
   "    tag: pg_stat_database\n" \
   "    sort: data\n" \
   "    collector: stat_db\n" \
   "\n" \
   "  - queries:\n" \
   "    - query: SELECT datname, confl_tablespace, confl_lock,\n" \
   "              confl_snapshot, confl_bufferpin, confl_deadlock\n" \
   "              FROM pg_stat_database_conflicts WHERE datname IS NOT NULL ORDER BY datname;\n" \
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
   "    tag: pg_stat_database_conflicts\n" \
   "    sort: data\n" \
   "    collector: stat_conflicts\n"

#ifdef __cplusplus
}
#endif

#endif
