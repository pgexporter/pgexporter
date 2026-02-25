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

#ifndef PGEXPORTER_TSCOMMON_H
#define PGEXPORTER_TSCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pgexporter.h"

#define PRIMARY_SERVER   0
#define ENV_VAR_BASE_DIR "PGEXPORTER_TEST_BASE_DIR"

extern char TEST_BASE_DIR[MAX_PATH];

/**
 * Create the testing environment
 */
void
pgexporter_test_environment_create(void);

/**
 * Destroy the testing environment
 */
void
pgexporter_test_environment_destroy(void);

/**
 * Basic setup before each forked unit test
 */
void
pgexporter_test_setup(void);

/**
 * Basic teardown after each forked unit test
 */
void
pgexporter_test_teardown(void);

/**
 * Snapshot the current shared-memory configuration.
 * Call this at the start of a test (or in MCTF_TEST_SETUP) to preserve
 * the original config before a test modifies it.
 */
void
pgexporter_test_config_save(void);

/**
 * Restore the shared-memory configuration from the last snapshot taken
 * by pgexporter_test_config_save().
 * Call this at the end of a test (or in MCTF_TEST_TEARDOWN) to roll back
 * any changes made during the test.
 */
void
pgexporter_test_config_restore(void);

/**
 * Conf set succeeds and the response matches the expected value.
 * @return 0 on success, -1 on failure
 */
int
pgexporter_test_assert_conf_set_ok(char* key, char* value, int64_t expected);

/**
 * Conf set fails for the given key/value.
 * @return 0 on success, -1 on failure
 */
int
pgexporter_test_assert_conf_set_fail(char* key, char* value);

/**
 * Conf get returns the expected value for the given key.
 * @return 0 on success, -1 on failure
 */
int
pgexporter_test_assert_conf_get_ok(char* key, int64_t expected);

#ifdef __cplusplus
}
#endif

#endif
