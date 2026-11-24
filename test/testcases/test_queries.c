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
#include <pgexporter.h>
#include <memory.h>
#include <message.h>
#include <queries.h>
#include <utils.h>
#include <mctf.h>

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * A RowDescription is read from the server and its column count is used to
 * fill the fixed-size names[] / type_oids[] arrays of struct query. A count
 * larger than MAX_NUMBER_OF_COLUMNS must be rejected, not written past the
 * end of the arrays.
 */
MCTF_TEST_NEGATIVE(test_query_params_too_many_columns)
{
   int sv[2] = {-1, -1};
   int ncols = MAX_NUMBER_OF_COLUMNS + 8;
   char req[4096];
   char reply[4096];
   size_t off = 0;
   size_t tstart;
   pid_t pid = -1;
   struct query* q = NULL;
   int ret = 0;

   pgexporter_memory_init();

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair failed");

   /* T: kind, length, int16 columns, then per column "c\0" + 18 bytes */
   memset(reply, 0, sizeof(reply));
   tstart = off;
   pgexporter_write_byte(reply + off, 'T');
   off += 5;
   pgexporter_write_int16(reply + off, (int16_t)ncols);
   off += 2;
   for (int i = 0; i < ncols; i++)
   {
      reply[off] = 'c';
      off += 2 + 18;
   }
   pgexporter_write_int32(reply + tstart + 1, (int32_t)(off - tstart - 1));

   /* Z: ReadyForQuery */
   pgexporter_write_byte(reply + off, 'Z');
   pgexporter_write_int32(reply + off + 1, 5);
   pgexporter_write_byte(reply + off + 5, 'I');
   off += 6;

   pid = fork();
   MCTF_ASSERT(pid >= 0, cleanup, "fork failed");

   if (pid == 0)
   {
      close(sv[0]);
      if (read(sv[1], req, sizeof(req)) > 0)
      {
         write(sv[1], reply, off);
      }
      close(sv[1]);
      _exit(0);
   }

   close(sv[1]);
   sv[1] = -1;

   ret = pgexporter_query_execute_params(NULL, sv[0], "SELECT 1", 0, NULL, "t", &q);

   MCTF_ASSERT(ret != 0, cleanup, "column count above MAX_NUMBER_OF_COLUMNS must be rejected");
   MCTF_ASSERT(q == NULL, cleanup, "no query should be returned");

cleanup:
   if (pid > 0)
   {
      waitpid(pid, NULL, 0);
   }
   if (q != NULL)
   {
      pgexporter_free_query(q);
   }
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
   pgexporter_memory_destroy();
   MCTF_FINISH();
}
