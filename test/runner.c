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
#include <tscommon.h>
#include <tssuite.h>

#include "logging.h"

int
main(int argc, char* argv[])
{
   int number_failed = 0;
   Suite* cli_suite;
   Suite* database_suite;
   Suite* http_suite;
   Suite* configuration_suite;
   SRunner* sr;

   pgexporter_test_environment_create();

   cli_suite = pgexporter_test_cli_suite();
   database_suite = pgexporter_test_database_suite();
   http_suite = pgexporter_test_http_suite();
   configuration_suite = pgexporter_test_configuration_suite();

   sr = srunner_create(cli_suite);
   srunner_add_suite(sr, database_suite);
   srunner_add_suite(sr, configuration_suite);
   srunner_add_suite(sr, http_suite);
   srunner_set_log(sr, "-");
   srunner_set_fork_status(sr, CK_NOFORK);
   srunner_run(sr, NULL, NULL, CK_VERBOSE);
   number_failed = srunner_ntests_failed(sr);
   srunner_free(sr);
   pgexporter_test_environment_destroy();

   return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}