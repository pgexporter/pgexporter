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

#include <tsclient.h>

#include "pgexporter_test_3.h"

START_TEST(test_pgexporter_http_metrics)
{
   int found = 0;
   found = !pgexporter_tsclient_test_http_metrics();
   ck_assert_msg(found, "pgexporter HTTP metrics test failed");
}
END_TEST

START_TEST(test_pgexporter_bridge_endpoint)
{
   int found = 0;
   found = !pgexporter_tsclient_test_bridge_endpoint();
   ck_assert_msg(found, "pgexporter bridge endpoint test failed");
}
END_TEST

START_TEST(test_pgexporter_extension_detection)
{
   int found = 0;
   found = !pgexporter_tsclient_test_extension_detection();
   ck_assert_msg(found, "pgexporter extension detection test failed");
}
END_TEST

// Test that shutdown command works (this should be last test)
START_TEST(test_pgexporter_shutdown)
{
   int found = 0;
   found = !pgexporter_tsclient_execute_shutdown();
   ck_assert_msg(found, "pgexporter shutdown failed");
}
END_TEST

Suite*
pgexporter_test3_suite()
{
   Suite* s;
   TCase* tc_core;

   s = suite_create("pgexporter_test3");

   tc_core = tcase_create("Core");

   tcase_set_timeout(tc_core, 60);
   tcase_add_test(tc_core, test_pgexporter_bridge_endpoint);
   tcase_add_test(tc_core, test_pgexporter_http_metrics);
   tcase_add_test(tc_core, test_pgexporter_extension_detection);

   tcase_add_test(tc_core, test_pgexporter_shutdown);

   suite_add_tcase(s, tc_core);

   return s;
}