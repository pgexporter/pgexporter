\newpage

# Prometheus metrics

[**pgexporter**][pgexporter] has the following [Prometheus][prometheus] built-in metrics.

## Core PostgreSQL Metrics

The following metrics are collected from core PostgreSQL system views and statistics:

* `postgresql_primary`
* `pg_database_size`
* `pg_locks_count`
* `pg_replication_slots`
* `pg_stat_bgwriter`
* `pg_process_idle_seconds`
* `pg_available_extensions`
* `pg_installed_extensions`
* `pg_file_settings`
* `pg_indexes`
* `pg_matviews`
* `pg_role`
* `pg_rule`
* `pg_shadow`
* `pg_usr_evt_trigger`
* `pg_db_conn`
* `pg_db_conn_ssl`
* `pg_statio_all_tables`
* `pg_statio_all_sequences`
* `pg_stat_user_functions`
* `pg_stat_replication`
* `pg_stat_archiver`
* `pg_db_vacuum`
* `pg_view_vacuum`
* `pg_blocked_vacuum`
* `pg_wal_last_received`
* `pg_gss_auth`
* `pg_encrypted_conn`
* `pg_shmem_allocations`
* `pg_stat_progress_vacuum`
* `pg_stat_user_tables_vacuum`
* `pg_table_bloat`
* `pg_mem_ctx`
* `pg_stat_wal`
* `pg_stat_database`
* `pg_wal_prefetch_reset`
* `pg_gssapi_credentials_delegated`
* `pg_stat_io`
* `pg_stat_database_conflicts`
* `pg_stat_all_indexes`
* `pg_wait_events`

## PostgreSQL Extension Metrics

[**pgexporter**][pgexporter] also provides comprehensive metrics for popular PostgreSQL extensions when they are installed and enabled:

### pg_stat_statements

Query performance and execution statistics:

* `pg_stat_statements_most_executed`
* `pg_stat_statements_most_planned`
* `pg_stat_statements_most_rows`
* `pg_stat_statements_slowest_execution`
* `pg_stat_statements_highest_wal`
* `pg_stat_statements_planning_stats`
* `pg_stat_statements_exec_time_detailed`
* `pg_stat_statements_io_stats`
* `pg_stat_statements_io_timing`
* `pg_stat_statements_performance_summary`
* `pg_stat_statements_extension_info`
* `pg_stat_statements_query_hierarchy`
* `pg_stat_statements_extension_metadata`
* `pg_stat_statements_hierarchy_performance`
* `pg_stat_statements_detailed_io_timing`
* `pg_stat_statements_jit_stats`
* `pg_stat_statements_jit_summary`
* `pg_stat_statements_granular_io_timing`
* `pg_stat_statements_jit_deform_stats`
* `pg_stat_statements_stats_reset_info`
* `pg_stat_statements_advanced_jit_summary`
* `pg_stat_statements_io_timing_summary`

### pg_buffercache

Shared buffer cache utilization and effectiveness:

* `pg_buffercache_buffer_utilization`
* `pg_buffercache_dirty_buffers`
* `pg_buffercache_top_cached_relations`
* `pg_buffercache_cache_effectiveness`
* `pg_buffercache_cache_pressure`
* `pg_buffercache_buffer_summary`
* `pg_buffercache_usage_distribution`

### pgcrypto

Cryptographic function usage patterns:

* `pgcrypto_function_usage_by_category`
* `pgcrypto_user_crypto_function_usage`

**Note:** To see metrics from `user_crypto_function_usage`, you need to enable function tracking:
```sql
ALTER SYSTEM SET track_functions = 'all';
SELECT pg_reload_conf();
```

### postgis

Spatial data and geometry/geography column statistics:

* `postgis_spatial_tables`
* `postgis_srid_usage`
* `postgis_geometry_columns_detail`
* `postgis_geography_columns_detail`
* `postgis_spatial_indexes`
* `postgis_spatial_index_summary`
* `postgis_geometry_type_stats`
* `postgis_coordinate_dimensions`
* `postgis_schema_spatial_stats`
* `postgis_common_srids`
* `postgis_spatial_storage_stats`

### postgis_raster

Raster data storage and processing metrics:

* `postgis_raster_raster_table_count`
* `postgis_raster_raster_columns_info`
* `postgis_raster_raster_srid_distribution`
* `postgis_raster_raster_band_stats`
* `postgis_raster_raster_pixel_types`
* `postgis_raster_raster_nodata_stats`
* `postgis_raster_raster_overview_count`
* `postgis_raster_raster_overview_factors`
* `postgis_raster_raster_storage_types`
* `postgis_raster_raster_constraints`
* `postgis_raster_raster_schema_distribution`
* `postgis_raster_raster_system_stats`
* `postgis_raster_raster_scale_stats`
* `postgis_raster_raster_block_sizes`

### postgis_topology

Topology element counts and health monitoring:

* `postgis_topology_topology_inventory`
* `postgis_topology_topology_details`
* `postgis_topology_topology_node_counts`
* `postgis_topology_topology_edge_counts`
* `postgis_topology_topology_face_counts`
* `postgis_topology_topology_primitives_summary`
* `postgis_topology_topology_system_stats`
* `postgis_topology_topology_srid_distribution`
* `postgis_topology_topology_health`
* `postgis_topology_topology_storage_stats`

### timescaledb

Hypertable, chunk, and compression statistics:

* `timescaledb_hypertable_overview`
* `timescaledb_job_health`
* `timescaledb_job_execution_history`
* `timescaledb_compression_effectiveness`
* `timescaledb_data_node_health`
* `timescaledb_compression_config`
* `timescaledb_columnstore_health`
* `timescaledb_recent_job_failures`
* `timescaledb_chunk_age_distribution`

### vector

Vector similarity search index performance and storage:

* `vector_vector_column_inventory`
* `vector_hnsw_index_performance`
* `vector_ivfflat_index_performance`
* `vector_vector_table_storage`
* `vector_pgvector_configuration`
* `vector_vector_operators`
* `vector_vector_index_distribution`