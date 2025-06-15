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
 *
 */

#ifndef PGEXPORTER_TSCLIENT_H
#define PGEXPORTER_TSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192

#define PGEXPORTER_LOG_FILE_TRAIL      "/log/pgexporter.log"
#define PGEXPORTER_EXECUTABLE_TRAIL    "/src/pgexporter-cli"
#define PGEXPORTER_CONFIGURATION_TRAIL "/pgexporter-testsuite/conf/pgexporter.conf"

extern char project_directory[BUFFER_SIZE];

/**
 * Initialize the tsclient API
 * @param base_dir path to base
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_init(char* base_dir);

/**
 * Destroy the tsclient (must be used after pgexporter_tsclient_init)
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_destroy();

/**
 * Execute ping command on the server
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_execute_ping();

/**
 * Execute shutdown command on the server
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_execute_shutdown();

/**
 * Execute status command on the server
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_execute_status();

/**
 * Test database connection establishment
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_test_db_connection();

/**
 * Test PostgreSQL version query directly
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_test_version_query();

/**
 * Test extension path setup
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_test_extension_path();

/**
 * Test HTTP metrics endpoint functionality
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_test_http_metrics();

/**
 * Test bridge endpoint functionality
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_test_bridge_endpoint();

/**
 * Test extension detection functionality
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tsclient_test_extension_detection();

#ifdef __cplusplus
}
#endif

#endif