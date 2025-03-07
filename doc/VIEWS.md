# PostgreSQL System Views Extractor

We provide scripts to extract different views. These are intended to help pgexporter maintain compatibility across different PostgreSQL versions by seeing `diff` for instance.

## pg_system_views_extractor.sql

The current script extracts PostgreSQL implementation of core system views, recording the PostgreSQL version.

To use the script, simply run it against different PostgreSQL versions and compare the results:

```bash
psql -d database -f pg_system_views_extractor.sql > pg_views_16.txt
diff pg_views_16.txt pg_views_17.txt
```