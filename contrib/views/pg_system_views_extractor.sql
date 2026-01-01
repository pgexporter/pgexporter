
-- Copyright (C) 2026 The pgexporter community

-- Redistribution and use in source and binary forms, with or without modification,
-- are permitted provided that the following conditions are met:

-- 1. Redistributions of source code must retain the above copyright notice, this list
-- of conditions and the following disclaimer.

-- 2. Redistributions in binary form must reproduce the above copyright notice, this
-- list of conditions and the following disclaimer in the documentation and/or other
-- materials provided with the distribution.

-- 3. Neither the name of the copyright holder nor the names of its contributors may
-- be used to endorse or promote products derived from this software without specific
-- prior written permission.

-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
-- EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
-- OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
-- THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
-- OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
-- HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
-- TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
-- SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-- This script extracts definitions of core system views used by pgexporter

\echo '-- PostgreSQL Version Information'
SELECT version();
\echo ''

-- formatting to check diff
\pset format unaligned
\pset tuples_only off
\pset footer off

-- temporary table with pg_* views, excluding unwanted views
CREATE TEMPORARY TABLE temp_result AS
SELECT relname
FROM pg_class
WHERE relkind = 'v'
  AND relname LIKE 'pg\_%'
  AND relname NOT IN (
    'pg_indexes',
    'pg_locks',
    'pg_matviews',
    'pg_prepared_statements',
    'pg_prepared_xacts',
    'pg_rules',
    'pg_settings',
    'pg_shadow',
    'pg_stats',
    'pg_tables',
    'pg_user',
    'pg_views'
  )
ORDER BY relname;

-- views we'll be analyzing
SELECT v.viewname
FROM temp_result t
JOIN pg_views v ON v.viewname = t.relname
WHERE v.schemaname = 'pg_catalog'
ORDER BY v.viewname;

\echo ''
\echo '-- ================================================================================'
\echo '-- SYSTEM VIEW COLUMN INFORMATION'
\echo '-- ================================================================================'

-- column data for all system views
SELECT
    c.relname AS view_name,
    STRING_AGG(a.attname, ', ' ORDER BY a.attnum) AS columns
FROM temp_result t
JOIN pg_catalog.pg_class c ON c.relname = t.relname
JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
JOIN pg_catalog.pg_attribute a ON c.oid = a.attrelid
WHERE n.nspname = 'pg_catalog'
AND c.relkind = 'v'
AND a.attnum > 0
AND NOT a.attisdropped
GROUP BY c.relname
ORDER BY c.relname;

\echo ''
\echo '-- ================================================================================'
\echo '-- DETAILED VIEW DEFINITIONS'
\echo '-- ================================================================================'

-- extract definitions of those views
\pset fieldsep '|\n'
SELECT 
    v.viewname AS view_name,
    pg_get_viewdef(v.viewname, true) AS definition
FROM temp_result t
JOIN pg_catalog.pg_views v ON v.viewname = t.relname
WHERE v.schemaname = 'pg_catalog'
ORDER BY v.viewname;

\echo ''

DROP TABLE temp_result;