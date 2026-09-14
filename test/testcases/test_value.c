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
 */

#include <pgexporter.h>
#include <value.h>
#include <mctf.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* pgexporter_value_create / pgexporter_value_data                     */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_value_create_int32)
{
   struct value* v = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_value_create(ValueInt32, (uintptr_t)42, &v), 0, cleanup,
                      "create ValueInt32 should succeed");
   MCTF_ASSERT_PTR_NONNULL(v, cleanup, "value should not be NULL");
   MCTF_ASSERT_INT_EQ((int)pgexporter_value_data(v), 42, cleanup, "data should be 42");
   MCTF_ASSERT_INT_EQ((int)pgexporter_value_type(v), (int)ValueInt32, cleanup, "type should be ValueInt32");

cleanup:
   pgexporter_value_destroy(v);
   MCTF_FINISH();
}

MCTF_TEST(test_value_create_bool)
{
   struct value* vt = NULL;
   struct value* vf = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_value_create(ValueBool, (uintptr_t)true, &vt), 0, cleanup,
                      "create true should succeed");
   MCTF_ASSERT(pgexporter_value_data(vt) != 0, cleanup, "true value data should be non-zero");

   MCTF_ASSERT_INT_EQ(pgexporter_value_create(ValueBool, (uintptr_t)false, &vf), 0, cleanup,
                      "create false should succeed");
   MCTF_ASSERT_INT_EQ((int)pgexporter_value_data(vf), 0, cleanup, "false value data should be 0");

cleanup:
   pgexporter_value_destroy(vt);
   pgexporter_value_destroy(vf);
   MCTF_FINISH();
}

MCTF_TEST(test_value_create_string)
{
   struct value* v = NULL;

   MCTF_ASSERT_INT_EQ(pgexporter_value_create(ValueString, (uintptr_t)"hello", &v), 0, cleanup,
                      "create ValueString should succeed");
   MCTF_ASSERT_STR_EQ((char*)pgexporter_value_data(v), "hello", cleanup, "string data mismatch");

cleanup:
   pgexporter_value_destroy(v);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* double / float round-trip                                           */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_value_double_roundtrip)
{
   double original = 3.14159265358979;
   uintptr_t encoded = pgexporter_value_from_double(original);
   double decoded = pgexporter_value_to_double(encoded);

   MCTF_ASSERT(fabs(decoded - original) < 1e-10, cleanup, "double round-trip precision lost");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_value_float_roundtrip)
{
   float original = 2.71828f;
   uintptr_t encoded = pgexporter_value_from_float(original);
   float decoded = pgexporter_value_to_float(encoded);

   MCTF_ASSERT(fabsf(decoded - original) < 1e-5f, cleanup, "float round-trip precision lost");

cleanup:
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_value_compare                                            */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_value_compare_int32_ordering)
{
   struct value* lo = NULL;
   struct value* hi = NULL;
   struct value* eq = NULL;

   pgexporter_value_create(ValueInt32, (uintptr_t)1, &lo);
   pgexporter_value_create(ValueInt32, (uintptr_t)2, &hi);
   pgexporter_value_create(ValueInt32, (uintptr_t)1, &eq);

   MCTF_ASSERT(pgexporter_value_compare(lo, hi) < 0, cleanup, "1 < 2 should be negative");
   MCTF_ASSERT(pgexporter_value_compare(hi, lo) > 0, cleanup, "2 > 1 should be positive");
   MCTF_ASSERT_INT_EQ(pgexporter_value_compare(lo, eq), 0, cleanup, "1 == 1 should be 0");

cleanup:
   pgexporter_value_destroy(lo);
   pgexporter_value_destroy(hi);
   pgexporter_value_destroy(eq);
   MCTF_FINISH();
}

MCTF_TEST(test_value_compare_string_ordering)
{
   struct value* a = NULL;
   struct value* b = NULL;

   pgexporter_value_create(ValueString, (uintptr_t)"apple", &a);
   pgexporter_value_create(ValueString, (uintptr_t)"banana", &b);

   MCTF_ASSERT(pgexporter_value_compare(a, b) < 0, cleanup, "apple < banana");
   MCTF_ASSERT(pgexporter_value_compare(b, a) > 0, cleanup, "banana > apple");
   MCTF_ASSERT_INT_EQ(pgexporter_value_compare(a, a), 0, cleanup, "apple == apple");

cleanup:
   pgexporter_value_destroy(a);
   pgexporter_value_destroy(b);
   MCTF_FINISH();
}

MCTF_TEST(test_value_compare_null_sorts_first)
{
   struct value* v = NULL;

   pgexporter_value_create(ValueInt32, (uintptr_t)0, &v);

   MCTF_ASSERT(pgexporter_value_compare(NULL, v) < 0, cleanup, "NULL should sort before non-NULL");
   MCTF_ASSERT(pgexporter_value_compare(v, NULL) > 0, cleanup, "non-NULL should sort after NULL");
   MCTF_ASSERT_INT_EQ(pgexporter_value_compare(NULL, NULL), 0, cleanup, "NULL == NULL");

cleanup:
   pgexporter_value_destroy(v);
   MCTF_FINISH();
}

/* ------------------------------------------------------------------ */
/* pgexporter_value_destroy NULL-safety                                */
/* ------------------------------------------------------------------ */

MCTF_TEST(test_value_destroy_null_safe)
{
   /* Must not crash */
   pgexporter_value_destroy(NULL);

cleanup:
   MCTF_FINISH();
}
