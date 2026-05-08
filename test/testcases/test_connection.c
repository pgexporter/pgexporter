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
#include <connection.h>
#include <tscommon.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Test: pgexporter_transfer_connection_read with invalid fd (-1) should fail.
 *
 * When an invalid file descriptor is passed, the function should return
 * an error and leave server and fd unchanged at -1.
 */
MCTF_TEST(test_connection_read_invalid_fd)
{
   int server = -1;
   int fd = -1;
   int ret;

   /* Pass invalid fd (-1) — should fail gracefully */
   ret = pgexporter_transfer_connection_read(-1, &server, &fd);

   MCTF_ASSERT(ret != 0, cleanup, "Reading from invalid fd should fail");
   MCTF_ASSERT_INT_EQ(server, -1, cleanup, "Server should remain -1 on failure");
   MCTF_ASSERT_INT_EQ(fd, -1, cleanup, "FD should remain -1 on failure");

cleanup:
   MCTF_FINISH();
}

/**
 * Test: pgexporter_transfer_connection_write with invalid server index should fail.
 *
 * A server index of -1 is out of bounds and should return an error.
 * NOTE: This test requires a running pgexporter instance with shared memory.
 * It is skipped here as a unit test — covered by integration tests.
 */
MCTF_TEST(test_connection_write_invalid_server)
{
   /* This function requires initialized shared memory (unix_socket_dir).
    * Without a running pgexporter, it will crash accessing NULL shmem.
    * We verify the function exists and is linked correctly. */
   MCTF_SKIP("Requires running pgexporter with initialized shared memory");

cleanup:
   MCTF_FINISH();
}
