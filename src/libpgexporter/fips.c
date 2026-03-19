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

/* pgexporter */
#include <pgexporter.h>
#include <extension.h>
#include <fips.h>
#include <logging.h>
#include <queries.h>
#include <utils.h>

/* OpenSSL */
#include <openssl/crypto.h>
#include <openssl/evp.h>

static int query_fips_mode(int server, struct query** query);
static int query_fips_ext(int server, struct query** query);

bool
pgexporter_fips_pgexporter(void)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
   return EVP_default_properties_is_fips_enabled(NULL) == 1;
#elif OPENSSL_VERSION_NUMBER >= 0x10100000L
   return FIPS_mode() == 1;
#endif
   return false;
}

int
pgexporter_fips_server(int server, bool* status)
{
   int ret;
   struct query* query = NULL;
   struct tuple* current = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   // Return cached value if already checked
   if (config->servers[server].fips_enabled != SERVER_FIPS_UNKNOWN)
   {
      *status = config->servers[server].fips_enabled == SERVER_FIPS_ENABLED;
      return 0;
   }

   *status = false;

   // PostgreSQL 18+: Use fips_mode() function from pgcrypto
   if (config->servers[server].version >= 18)
   {
      if (!pgexporter_extension_is_enabled(config, server, "pgcrypto"))
      {
         goto done;
      }

      ret = query_fips_mode(server, &query);
      if (ret != 0)
      {
         pgexporter_log_debug("FIPS: fips_mode() not available for server %s, assuming disabled",
                              config->servers[server].name);
         goto done;
      }

      current = query->tuples;
      if (current == NULL)
      {
         goto done;
      }

      *status = strcmp(pgexporter_get_column(0, current), "t") == 0;
   }
   else if (config->servers[server].version >= 14)
   {
      if (!pgexporter_extension_is_enabled(config, server, "pgexporter_ext"))
      {
         goto done;
      }

      ret = query_fips_ext(server, &query);
      if (ret != 0)
      {
         pgexporter_log_debug("FIPS: pgexporter_ext not available for server %s, FIPS status unknown",
                              config->servers[server].name);
         goto done;
      }

      current = query->tuples;
      if (current == NULL)
      {
         goto done;
      }

      *status = strcmp(pgexporter_get_column(0, current), "t") == 0;
   }

done:
   config->servers[server].fips_enabled = *status ? SERVER_FIPS_ENABLED : SERVER_FIPS_DISABLED;

   pgexporter_free_query(query);
   return 0;
}

static int
query_fips_mode(int server, struct query** query)
{
   return pgexporter_query_execute(server,
                                   "SELECT fips_mode();",
                                   "pg_fips_mode", query);
}

static int
query_fips_ext(int server, struct query** query)
{
   return pgexporter_query_execute(server,
                                   "SELECT pgexporter_ext_fips();",
                                   "pgexporter_ext_fips", query);
}
