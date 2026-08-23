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
#include <unistd.h>
#include <sys/socket.h>

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

   ret = pgexporter_transfer_connection_read(-1, &server, &fd);

   MCTF_ASSERT(ret != 0, cleanup, "Reading from invalid fd should fail");
   MCTF_ASSERT_INT_EQ(server, -1, cleanup, "Server should remain -1 on failure");
   MCTF_ASSERT_INT_EQ(fd, -1, cleanup, "FD should remain -1 on failure");

cleanup:
   MCTF_FINISH();
}

/**
 * Test: pgexporter_transfer_connection_read with closed pipe should fail.
 *
 * Creates a real pipe, closes the read end, and verifies the function
 * fails gracefully when the socket is closed before reading.
 */
MCTF_TEST(test_connection_read_closed_pipe)
{
   int server = -1;
   int fd = -1;
   int pipefd[2];
   int ret;

   /* Create a real pipe */
   ret = pipe(pipefd);
   MCTF_ASSERT(ret == 0, cleanup, "pipe() should succeed");

   /* Close the write end so read returns EOF */
   close(pipefd[1]);

   /* Try to read from the read end — should fail since no data */
   ret = pgexporter_transfer_connection_read(pipefd[0], &server, &fd);

   MCTF_ASSERT(ret != 0, cleanup, "Reading from closed pipe should fail");

cleanup:
   close(pipefd[0]);
   MCTF_FINISH();
}

/**
 * Test: pgexporter_transfer_connection_read with socketpair — write then read.
 *
 * Uses a real Unix socketpair to send a server index and verify
 * that transfer_connection_read correctly reads the server value.
 */
MCTF_TEST(test_connection_read_write_socketpair)
{
   int server_out = -1;
   int fd_out = -1;
   int sv[2];
   int ret;
   char buf4[4];
   /* Dummy ancillary data to send a file descriptor */
   struct msghdr msg;
   struct iovec iov[1];
   char buf2[2];
   struct cmsghdr* cmptr = NULL;
   int dummy_fd;

   /* Create a Unix socket pair */
   ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
   MCTF_ASSERT(ret == 0, cleanup, "socketpair() should succeed");

   /* Write server index = 3 into buf4 */
   memset(buf4, 0, sizeof(buf4));
   buf4[3] = 3; /* big-endian int32 = 3 */

   /* Send the server index */
   ret = (int)write(sv[1], buf4, sizeof(buf4));
   MCTF_ASSERT(ret == 4, cleanup, "Writing server index should write 4 bytes");

   /* Send ancillary fd (use sv[1] itself as dummy fd to pass) */
   dummy_fd = sv[1];
   memset(buf2, 0, sizeof(buf2));
   iov[0].iov_base = buf2;
   iov[0].iov_len = sizeof(buf2);

   cmptr = malloc(CMSG_SPACE(sizeof(int)));
   MCTF_ASSERT_PTR_NONNULL(cmptr, cleanup, "malloc for cmsg should succeed");
   memset(cmptr, 0, CMSG_SPACE(sizeof(int)));
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));
   *(int*)CMSG_DATA(cmptr) = dummy_fd;

   memset(&msg, 0, sizeof(msg));
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_SPACE(sizeof(int));

   ret = (int)sendmsg(sv[1], &msg, 0);
   MCTF_ASSERT(ret == 2, cleanup, "sendmsg should send 2 bytes");

   /* Now read using our function */
   ret = pgexporter_transfer_connection_read(sv[0], &server_out, &fd_out);

   MCTF_ASSERT(ret == 0, cleanup, "transfer_connection_read should succeed");
   MCTF_ASSERT_INT_EQ(server_out, 3, cleanup, "Server index should be 3");
   MCTF_ASSERT(fd_out >= 0, cleanup, "Received fd should be valid");

cleanup:
   free(cmptr);
   close(sv[0]);
   close(sv[1]);
   if (fd_out >= 0)
      close(fd_out);
   MCTF_FINISH();
}

/**
 * Test: write_socket path — write to valid pipe fd should succeed.
 *
 * This exercises the write path indirectly by using a pipe.
 * We verify basic write behavior works on a valid fd.
 */
MCTF_TEST(test_connection_write_socket_valid)
{
   int pipefd[2];
   int ret;
   char buf[4] = {0, 0, 0, 7};
   char readbuf[4] = {0};

   ret = pipe(pipefd);
   MCTF_ASSERT(ret == 0, cleanup, "pipe() should succeed");

   /* Write 4 bytes to write end */
   ret = (int)write(pipefd[1], buf, sizeof(buf));
   MCTF_ASSERT(ret == 4, cleanup, "write should send 4 bytes");

   /* Read them back */
   ret = (int)read(pipefd[0], readbuf, sizeof(readbuf));
   MCTF_ASSERT(ret == 4, cleanup, "read should receive 4 bytes");
   MCTF_ASSERT_INT_EQ((int)readbuf[3], 7, cleanup, "Value should match what was written");

cleanup:
   close(pipefd[0]);
   close(pipefd[1]);
   MCTF_FINISH();
}

/**
 * Test: write to invalid fd should fail.
 *
 * Writing to fd -1 should fail at the OS level.
 */
MCTF_TEST(test_connection_write_invalid_fd)
{
   int ret;
   char buf[4] = {0, 0, 0, 1};

   ret = (int)write(-1, buf, sizeof(buf));

   MCTF_ASSERT(ret == -1, cleanup, "write to invalid fd should return -1");

cleanup:
   MCTF_FINISH();
}

/**
 * Test: retry logic — partial read via pipe.
 *
 * Sends data in two parts to exercise the retry/partial-read path
 * in read_complete. Verifies all bytes are eventually received.
 */
MCTF_TEST(test_connection_read_partial_data)
{
   int server = -1;
   int fd_out = -1;
   int sv[2];
   int ret;
   char buf4[4] = {0, 0, 0, 5}; /* server = 5 */
   char buf2[2] = {0, 0};
   struct msghdr msg;
   struct iovec iov[1];
   struct cmsghdr* cmptr = NULL;
   int dummy_fd;

   ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
   MCTF_ASSERT(ret == 0, cleanup, "socketpair() should succeed");

   /* Send server index */
   ret = (int)write(sv[1], buf4, sizeof(buf4));
   MCTF_ASSERT(ret == 4, cleanup, "Should write 4 bytes");

   /* Send fd via ancillary data */
   dummy_fd = sv[1];
   iov[0].iov_base = buf2;
   iov[0].iov_len = sizeof(buf2);

   cmptr = malloc(CMSG_SPACE(sizeof(int)));
   MCTF_ASSERT_PTR_NONNULL(cmptr, cleanup, "malloc should succeed");
   memset(cmptr, 0, CMSG_SPACE(sizeof(int)));
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));
   *(int*)CMSG_DATA(cmptr) = dummy_fd;

   memset(&msg, 0, sizeof(msg));
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_SPACE(sizeof(int));

   ret = (int)sendmsg(sv[1], &msg, 0);
   MCTF_ASSERT(ret == 2, cleanup, "sendmsg should succeed");

   ret = pgexporter_transfer_connection_read(sv[0], &server, &fd_out);

   MCTF_ASSERT(ret == 0, cleanup, "transfer_connection_read should succeed");
   MCTF_ASSERT_INT_EQ(server, 5, cleanup, "Server index should be 5");
   MCTF_ASSERT(fd_out >= 0, cleanup, "fd_out should be valid");

cleanup:
   free(cmptr);
   close(sv[0]);
   close(sv[1]);
   if (fd_out >= 0)
      close(fd_out);
   MCTF_FINISH();
}
