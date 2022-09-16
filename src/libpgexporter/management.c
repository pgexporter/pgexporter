/*
 * Copyright (C) 2022 Red Hat
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
#include <logging.h>
#include <network.h>
#include <management.h>
#include <memory.h>
#include <queries.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MANAGEMENT_HEADER_SIZE 1

static int read_int32(char* prefix, int socket, int* i);
static int read_string(char* prefix, int socket, char** str);
static int write_int32(char* prefix, int socket, int i);
static int write_string(char* prefix, int socket, char* str);
static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);
static int write_header(SSL* ssl, int fd, signed char type);

int
pgexporter_management_read_header(int socket, signed char* id)
{
   char header[MANAGEMENT_HEADER_SIZE];

   if (read_complete(NULL, socket, &header[0], sizeof(header)))
   {
      errno = 0;
      goto error;
   }

   *id = pgexporter_read_byte(&(header));

   return 0;

error:

   *id = -1;

   return 1;
}

int
pgexporter_management_read_payload(int socket, signed char id, int* payload_i1, int* payload_i2)
{
   int nr;
   char buf2[2];
   char buf4[4];
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;

   *payload_i1 = -1;
   *payload_i2 = -1;

   switch (id)
   {
      case MANAGEMENT_TRANSFER_CONNECTION:

         memset(&buf4[0], 0, sizeof(buf4));
         if (read_complete(NULL, socket, &buf4, sizeof(buf4)))
         {
            pgexporter_log_warn("pgexporter_management_read_status: read: %d %s", socket, strerror(errno));
            errno = 0;
            goto error;
         }

         *payload_i1 = pgexporter_read_int32(&buf4);

         memset(&buf2[0], 0, sizeof(buf2));

         iov[0].iov_base = &buf2[0];
         iov[0].iov_len = sizeof(buf2);

         cmptr = malloc(CMSG_SPACE(sizeof(int)));
         memset(cmptr, 0, CMSG_SPACE(sizeof(int)));
         cmptr->cmsg_len = CMSG_LEN(sizeof(int));
         cmptr->cmsg_level = SOL_SOCKET;
         cmptr->cmsg_type = SCM_RIGHTS;

         msg.msg_name = NULL;
         msg.msg_namelen = 0;
         msg.msg_iov = iov;
         msg.msg_iovlen = 1;
         msg.msg_control = cmptr;
         msg.msg_controllen = CMSG_SPACE(sizeof(int));
         msg.msg_flags = 0;

         if ((nr = recvmsg(socket, &msg, 0)) < 0)
         {
            goto error;
         }
         else if (nr == 0)
         {
            goto error;
         }

         *payload_i1 = pgexporter_read_int32(&buf4);
         *payload_i2 = *(int*)CMSG_DATA(cmptr);

         free(cmptr);
         break;
      case MANAGEMENT_STOP:
      case MANAGEMENT_STATUS:
      case MANAGEMENT_DETAILS:
      case MANAGEMENT_RESET:
      case MANAGEMENT_RELOAD:
         break;
      default:
         goto error;
         break;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_transfer_connection(int server)
{
   int fd;
   struct configuration* config;
   struct cmsghdr* cmptr = NULL;
   struct iovec iov[1];
   struct msghdr msg;
   char buf2[2];
   char buf4[4];

   config = (struct configuration*)shmem;

   if (pgexporter_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &fd))
   {
      pgexporter_log_warn("pgexporter_management_transfer_connection: connect: %d", fd);
      errno = 0;
      goto error;
   }

   if (write_header(NULL, fd, MANAGEMENT_TRANSFER_CONNECTION))
   {
      pgexporter_log_warn("pgexporter_management_transfer_connection: write: %d", fd);
      errno = 0;
      goto error;
   }

   memset(&buf4[0], 0, sizeof(buf4));
   pgexporter_write_int32(&buf4, server);

   if (write_complete(NULL, fd, &buf4, sizeof(buf4)))
   {
      pgexporter_log_warn("pgexporter_management_transfer_connection: write: %d %s", fd, strerror(errno));
      errno = 0;
      goto error;
   }

   /* Write file descriptor */
   memset(&buf2[0], 0, sizeof(buf2));

   iov[0].iov_base = &buf2[0];
   iov[0].iov_len = sizeof(buf2);

   cmptr = malloc(CMSG_SPACE(sizeof(int)));
   memset(cmptr, 0, CMSG_SPACE(sizeof(int)));
   cmptr->cmsg_level = SOL_SOCKET;
   cmptr->cmsg_type = SCM_RIGHTS;
   cmptr->cmsg_len = CMSG_LEN(sizeof(int));

   msg.msg_name = NULL;
   msg.msg_namelen = 0;
   msg.msg_iov = iov;
   msg.msg_iovlen = 1;
   msg.msg_control = cmptr;
   msg.msg_controllen = CMSG_SPACE(sizeof(int));
   msg.msg_flags = 0;
   *(int*)CMSG_DATA(cmptr) = config->servers[server].fd;

   if (sendmsg(fd, &msg, 0) != 2)
   {
      goto error;
   }

   free(cmptr);
   pgexporter_disconnect(fd);

   return 0;

error:
   if (cmptr)
   {
      free(cmptr);
   }
   pgexporter_disconnect(fd);

   return 1;
}

int
pgexporter_management_stop(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STOP))
   {
      pgexporter_log_warn("pgexporter_management_stop: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_status(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STATUS))
   {
      pgexporter_log_warn("pgexporter_management_status: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_read_status(SSL* ssl, int socket)
{
   char* name = NULL;
   int servers;
   int active;

   if (read_int32("pgexporter_management_read_status", socket, &servers))
   {
      goto error;
   }

   printf("Number of servers: %d\n", servers);

   for (int i = 0; i < servers; i++)
   {
      if (read_string("pgexporter_management_read_status", socket, &name))
      {
         goto error;
      }

      if (read_int32("pgexporter_management_read_status", socket, &active))
      {
         goto error;
      }

      printf("Server           : %s\n", &name[0]);
      printf("  Active         : %s\n", active == 1 ? "Yes" : "No");

      free(name);
      name = NULL;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_write_status(int socket)
{
   struct configuration* config;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   pgexporter_open_connections();

   if (write_int32("pgexporter_management_write_status", socket, config->number_of_servers))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (write_string("pgexporter_management_write_status", socket, config->servers[i].name))
      {
         goto error;
      }

      if (write_int32("pgexporter_management_write_status", socket, config->servers[i].fd != -1 ? 1 : 0))
      {
         goto error;
      }
   }

   pgexporter_close_connections();

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   return 0;

error:

   pgexporter_close_connections();

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   return 1;
}

int
pgexporter_management_details(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_DETAILS))
   {
      pgexporter_log_warn("pgexporter_management_details: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_read_details(SSL* ssl, int socket)
{
   return pgexporter_management_read_status(ssl, socket);
}

int
pgexporter_management_write_details(int socket)
{
   return pgexporter_management_write_status(socket);
}

int
pgexporter_management_isalive(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_ISALIVE))
   {
      pgexporter_log_warn("pgexporter_management_isalive: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_read_isalive(SSL* ssl, int socket, int* status)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgexporter_log_warn("pgexporter_management_read_isalive: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *status = pgexporter_read_int32(&buf);

   return 0;

error:

   return 1;
}

int
pgexporter_management_write_isalive(int socket)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   pgexporter_write_int32(buf, 1);

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgexporter_log_warn("pgexporter_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_reset(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_RESET))
   {
      pgexporter_log_warn("pgexporter_management_reset: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgexporter_management_reload(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_RELOAD))
   {
      pgexporter_log_warn("pgexporter_management_reload: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
read_int32(char* prefix, int socket, int* i)
{
   char buf4[4] = {0};

   *i = 0;

   if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
   {
      pgexporter_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *i = pgexporter_read_int32(&buf4);

   return 0;

error:

   return 1;
}

static int
read_string(char* prefix, int socket, char** str)
{
   char* s = NULL;
   char buf4[4] = {0};
   int size;

   *str = NULL;

   if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
   {
      pgexporter_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   size = pgexporter_read_int32(&buf4);
   if (size > 0)
   {
      s = malloc(size + 1);
      memset(s, 0, size + 1);

      if (read_complete(NULL, socket, s, size))
      {
         pgexporter_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
         errno = 0;
         goto error;
      }

      *str = s;
   }

   return 0;

error:

   return 1;
}

static int
write_int32(char* prefix, int socket, int i)
{
   char buf4[4] = {0};

   pgexporter_write_int32(&buf4, i);
   if (write_complete(NULL, socket, &buf4, sizeof(buf4)))
   {
      pgexporter_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_string(char* prefix, int socket, char* str)
{
   char buf4[4] = {0};

   pgexporter_write_int32(&buf4, str != NULL ? strlen(str) : 0);
   if (write_complete(NULL, socket, &buf4, sizeof(buf4)))
   {
      pgexporter_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (str != NULL)
   {
      if (write_complete(NULL, socket, str, strlen(str)))
      {
         pgexporter_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r < needs)
   {
      /* Sleep for 10ms */
      SLEEP(10000000L);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgexporter_log_trace("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgexporter_log_trace("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgexporter_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgexporter_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_header(SSL* ssl, int socket, signed char type)
{
   char header[MANAGEMENT_HEADER_SIZE];

   pgexporter_write_byte(&(header), type);

   return write_complete(ssl, socket, &(header), MANAGEMENT_HEADER_SIZE);
}
