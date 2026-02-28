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
#include <logging.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <openssl/pem.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#ifndef EVBACKEND_LINUXAIO
#define EVBACKEND_LINUXAIO 0x00000040U
#endif

#ifndef EVBACKEND_IOURING
#define EVBACKEND_IOURING 0x00000080U
#endif

extern char** environ;
#ifdef HAVE_LINUX
static bool env_changed = false;
static int max_process_title_size = 0;
#endif

static int string_compare(const void* a, const void* b);

static bool is_wal_file(char* file);

int32_t
pgexporter_get_request(struct message* msg)
{
   if (msg == NULL || msg->data == NULL || msg->length < 8)
   {
      return -1;
   }

   return pgexporter_read_int32(msg->data + 4);
}

int
pgexporter_extract_username_database(struct message* msg, char** username, char** database, char** appname)
{
   int start, end;
   int counter = 0;
   signed char c;
   char** array = NULL;
   size_t size;
   char* un = NULL;
   char* db = NULL;
   char* an = NULL;

   *username = NULL;
   *database = NULL;
   *appname = NULL;

   /* We know where the parameters start, and we know that the message is zero terminated */
   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgexporter_read_byte(msg->data + i);
      if (c == 0)
      {
         counter++;
      }
   }

   array = (char**)malloc(sizeof(char*) * counter);

   counter = 0;
   start = 8;
   end = 8;

   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgexporter_read_byte(msg->data + i);
      end++;
      if (c == 0)
      {
         array[counter] = (char*)malloc(end - start);
         memset(array[counter], 0, end - start);
         memcpy(array[counter], msg->data + start, end - start);

         start = end;
         counter++;
      }
   }

   for (int i = 0; i < counter; i++)
   {
      if (!strcmp(array[i], "user"))
      {
         size = strlen(array[i + 1]) + 1;
         un = malloc(size);
         memset(un, 0, size);
         memcpy(un, array[i + 1], size);

         *username = un;
      }
      else if (!strcmp(array[i], "database"))
      {
         size = strlen(array[i + 1]) + 1;
         db = malloc(size);
         memset(db, 0, size);
         memcpy(db, array[i + 1], size);

         *database = db;
      }
      else if (!strcmp(array[i], "application_name"))
      {
         size = strlen(array[i + 1]) + 1;
         an = malloc(size);
         memset(an, 0, size);
         memcpy(an, array[i + 1], size);

         *appname = an;
      }
   }

   if (*database == NULL)
   {
      *database = *username;
   }

   pgexporter_log_trace("Username: %s", *username);
   pgexporter_log_trace("Database: %s", *database);

   for (int i = 0; i < counter; i++)
   {
      free(array[i]);
   }
   free(array);

   return 0;
}

int
pgexporter_extract_message(char type, struct message* msg, struct message** extracted)
{
   int offset;
   int m_length;
   void* data = NULL;
   struct message* result = NULL;

   offset = 0;
   *extracted = NULL;

   while (result == NULL && offset < msg->length)
   {
      char t = (char)pgexporter_read_byte(msg->data + offset);

      if (type == t)
      {
         m_length = pgexporter_read_int32(msg->data + offset + 1);

         result = (struct message*)malloc(sizeof(struct message));
         data = (void*)malloc(1 + m_length);

         memcpy(data, msg->data + offset, 1 + m_length);

         result->kind = pgexporter_read_byte(data);
         result->length = 1 + m_length;
         result->data = data;

         *extracted = result;

         return 0;
      }
      else
      {
         offset += 1;
         offset += pgexporter_read_int32(msg->data + offset);
      }
   }

   return 1;
}

bool
pgexporter_has_message(char type, void* data, size_t data_size)
{
   size_t offset;

   offset = 0;

   while (offset < data_size)
   {
      char t = (char)pgexporter_read_byte(data + offset);

      if (type == t)
      {
         return true;
      }
      else
      {
         offset += 1;
         if (offset + sizeof(int32_t) > data_size)
         {
            pgexporter_log_debug("Not enough bytes left for int32_t");
            break;
         }
         offset += pgexporter_read_int32(data + offset);
      }
   }

   return false;
}

size_t
pgexporter_extract_message_offset(size_t offset, void* data, struct message** extracted)
{
   char type;
   int m_length;
   void* m_data;
   struct message* result = NULL;

   *extracted = NULL;

   type = (char)pgexporter_read_byte(data + offset);
   m_length = pgexporter_read_int32(data + offset + 1);

   result = (struct message*)malloc(sizeof(struct message));
   m_data = malloc(1 + m_length);

   memcpy(m_data, data + offset, 1 + m_length);

   result->kind = type;
   result->length = 1 + m_length;
   result->data = m_data;

   *extracted = result;

   return offset + 1 + m_length;
}

int
pgexporter_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted)
{
   size_t offset;
   void* m_data = NULL;
   int m_length;
   struct message* result = NULL;

   offset = 0;
   *extracted = NULL;

   while (result == NULL && offset < data_size)
   {
      char t = (char)pgexporter_read_byte(data + offset);

      if (type == t)
      {
         m_length = pgexporter_read_int32(data + offset + 1);

         result = (struct message*)malloc(sizeof(struct message));
         m_data = (void*)malloc(1 + m_length);

         memcpy(m_data, data + offset, 1 + m_length);

         result->kind = pgexporter_read_byte(m_data);
         result->length = 1 + m_length;
         result->data = m_data;

         *extracted = result;

         return 0;
      }
      else
      {
         offset += 1;
         offset += pgexporter_read_int32(data + offset);
      }
   }

   return 1;
}

signed char
pgexporter_read_byte(void* data)
{
   return (signed char)*((char*)data);
}

uint8_t
pgexporter_read_uint8(void* data)
{
   return (uint8_t)*((char*)data);
}

int16_t
pgexporter_read_int16(void* data)
{
   int16_t val;
   memcpy(&val, data, sizeof(val));
   return ntohs(val);
}

uint16_t
pgexporter_read_uint16(void* data)
{
   uint16_t val;
   memcpy(&val, data, sizeof(val));
   return ntohs(val);
}

int32_t
pgexporter_read_int32(void* data)
{
   int32_t val;
   memcpy(&val, data, sizeof(val));
   return ntohl(val);
}

uint32_t
pgexporter_read_uint32(void* data)
{
   uint32_t val;
   memcpy(&val, data, sizeof(val));
   return ntohl(val);
}

int64_t
pgexporter_read_int64(void* data)
{
   if (pgexporter_bigendian())
   {
      int64_t val;
      memcpy(&val, data, sizeof(val));
      return val;
   }
   else
   {
      unsigned char* bytes = (unsigned char*)data;
      int64_t res = ((int64_t)bytes[0] << 56) |
                    ((int64_t)bytes[1] << 48) |
                    ((int64_t)bytes[2] << 40) |
                    ((int64_t)bytes[3] << 32) |
                    ((int64_t)bytes[4] << 24) |
                    ((int64_t)bytes[5] << 16) |
                    ((int64_t)bytes[6] << 8) |
                    ((int64_t)bytes[7]);
      return res;
   }
}

void
pgexporter_write_byte(void* data, signed char b)
{
   *((char*)(data)) = b;
}

void
pgexporter_write_uint8(void* data, uint8_t b)
{
   *((uint8_t*)(data)) = b;
}

void
pgexporter_write_int16(void* data, int16_t i)
{
   int16_t n = htons(i);
   memcpy(data, &n, sizeof(n));
}

void
pgexporter_write_uint16(void* data, uint16_t i)
{
   uint16_t n = htons(i);
   memcpy(data, &n, sizeof(n));
}

void
pgexporter_write_int32(void* data, int32_t i)
{
   int32_t n = htonl(i);
   memcpy(data, &n, sizeof(n));
}

void
pgexporter_write_uint32(void* data, uint32_t i)
{
   uint32_t n = htonl(i);
   memcpy(data, &n, sizeof(n));
}

void
pgexporter_write_int64(void* data, int64_t i)
{
   if (pgexporter_bigendian())
   {
      memcpy(data, &i, sizeof(i));
   }
   else
   {
      unsigned char* ptr = (unsigned char*)&i;
      unsigned char* out = (unsigned char*)data;
      out[7] = ptr[0];
      out[6] = ptr[1];
      out[5] = ptr[2];
      out[4] = ptr[3];
      out[3] = ptr[4];
      out[2] = ptr[5];
      out[1] = ptr[6];
      out[0] = ptr[7];
   }
}

char*
pgexporter_read_string(void* data)
{
   return (char*)data;
}

void
pgexporter_write_string(void* data, char* s)
{
   memcpy(data, s, strlen(s));
}

bool
pgexporter_bigendian(void)
{
   short int word = 0x0001;
   char* b = (char*)&word;
   return (b[0] ? false : true);
}

unsigned int
pgexporter_swap(unsigned int i)
{
   return ((i << 24) & 0xff000000) |
          ((i << 8) & 0x00ff0000) |
          ((i >> 8) & 0x0000ff00) |
          ((i >> 24) & 0x000000ff);
}

void
pgexporter_libev_engines(void)
{
   unsigned int engines = ev_supported_backends();

   if (engines & EVBACKEND_SELECT)
   {
      pgexporter_log_debug("libev available: select");
   }
   if (engines & EVBACKEND_POLL)
   {
      pgexporter_log_debug("libev available: poll");
   }
   if (engines & EVBACKEND_EPOLL)
   {
      pgexporter_log_debug("libev available: epoll");
   }
   if (engines & EVBACKEND_LINUXAIO)
   {
      pgexporter_log_debug("libev available: linuxaio");
   }
   if (engines & EVBACKEND_IOURING)
   {
      pgexporter_log_debug("libev available: iouring");
   }
   if (engines & EVBACKEND_KQUEUE)
   {
      pgexporter_log_debug("libev available: kqueue");
   }
   if (engines & EVBACKEND_DEVPOLL)
   {
      pgexporter_log_debug("libev available: devpoll");
   }
   if (engines & EVBACKEND_PORT)
   {
      pgexporter_log_debug("libev available: port");
   }
}

unsigned int
pgexporter_libev(char* engine)
{
   unsigned int engines = ev_supported_backends();

   if (engine)
   {
      if (!strcmp("select", engine))
      {
         if (engines & EVBACKEND_SELECT)
         {
            return EVBACKEND_SELECT;
         }
         else
         {
            pgexporter_log_warn("libev not available: select");
         }
      }
      else if (!strcmp("poll", engine))
      {
         if (engines & EVBACKEND_POLL)
         {
            return EVBACKEND_POLL;
         }
         else
         {
            pgexporter_log_warn("libev not available: poll");
         }
      }
      else if (!strcmp("epoll", engine))
      {
         if (engines & EVBACKEND_EPOLL)
         {
            return EVBACKEND_EPOLL;
         }
         else
         {
            pgexporter_log_warn("libev not available: epoll");
         }
      }
      else if (!strcmp("linuxaio", engine))
      {
         return EVFLAG_AUTO;
      }
      else if (!strcmp("iouring", engine))
      {
         if (engines & EVBACKEND_IOURING)
         {
            return EVBACKEND_IOURING;
         }
         else
         {
            pgexporter_log_warn("libev not available: iouring");
         }
      }
      else if (!strcmp("devpoll", engine))
      {
         if (engines & EVBACKEND_DEVPOLL)
         {
            return EVBACKEND_DEVPOLL;
         }
         else
         {
            pgexporter_log_warn("libev not available: devpoll");
         }
      }
      else if (!strcmp("port", engine))
      {
         if (engines & EVBACKEND_PORT)
         {
            return EVBACKEND_PORT;
         }
         else
         {
            pgexporter_log_warn("libev not available: port");
         }
      }
      else if (!strcmp("auto", engine) || !strcmp("", engine))
      {
         return EVFLAG_AUTO;
      }
      else
      {
         pgexporter_log_warn("libev unknown option: %s", engine);
      }
   }

   return EVFLAG_AUTO;
}

char*
pgexporter_libev_engine(unsigned int val)
{
   switch (val)
   {
      case EVBACKEND_SELECT:
         return "select";
      case EVBACKEND_POLL:
         return "poll";
      case EVBACKEND_EPOLL:
         return "epoll";
      case EVBACKEND_LINUXAIO:
         return "linuxaio";
      case EVBACKEND_IOURING:
         return "iouring";
      case EVBACKEND_KQUEUE:
         return "kqueue";
      case EVBACKEND_DEVPOLL:
         return "devpoll";
      case EVBACKEND_PORT:
         return "port";
   }

   return "Unknown";
}

char*
pgexporter_get_home_directory(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_dir;
}

char*
pgexporter_get_user_name(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_name;
}

char*
pgexporter_get_password(void)
{
   char p[MAX_PASSWORD_LENGTH];
   struct termios oldt, newt;
   int i = 0;
   int c;
   char* result = NULL;

   memset(&p, 0, sizeof(p));

   tcgetattr(STDIN_FILENO, &oldt);
   newt = oldt;

   newt.c_lflag &= ~(ECHO);

   tcsetattr(STDIN_FILENO, TCSANOW, &newt);

   while ((c = getchar()) != '\n' && c != EOF && i < MAX_PASSWORD_LENGTH)
   {
      p[i++] = c;
   }
   p[i] = '\0';

   tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

   result = malloc(strlen(p) + 1);
   memset(result, 0, strlen(p) + 1);

   memcpy(result, &p, strlen(p));

   return result;
}

int
pgexporter_base64_encode(void* raw, size_t raw_length, char** encoded, size_t* encoded_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   BUF_MEM* mem_bio_mem_ptr;
   char* r = NULL;

   *encoded = NULL;
   *encoded_length = 0;

   if (raw == NULL)
   {
      goto error;
   }

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);
   BIO_write(b64_bio, raw, raw_length);
   BIO_flush(b64_bio);

   BIO_get_mem_ptr(mem_bio, &mem_bio_mem_ptr);

   BIO_set_close(mem_bio, BIO_NOCLOSE);
   BIO_free_all(b64_bio);

   BUF_MEM_grow(mem_bio_mem_ptr, (*mem_bio_mem_ptr).length + 1);
   (*mem_bio_mem_ptr).data[(*mem_bio_mem_ptr).length] = '\0';

   r = malloc(strlen((*mem_bio_mem_ptr).data) + 1);
   memset(r, 0, strlen((*mem_bio_mem_ptr).data) + 1);
   memcpy(r, (*mem_bio_mem_ptr).data, strlen((*mem_bio_mem_ptr).data));

   BUF_MEM_free(mem_bio_mem_ptr);

   *encoded = r;
   *encoded_length = strlen(r);

   return 0;

error:

   *encoded = NULL;

   return 1;
}

int
pgexporter_base64_decode(char* encoded, size_t encoded_length, void** raw, size_t* raw_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   size_t size;
   char* decoded;
   int index;

   *raw = NULL;
   *raw_length = 0;

   if (encoded == NULL)
   {
      goto error;
   }

   size = (encoded_length * 3) / 4 + 1;
   decoded = malloc(size);
   memset(decoded, 0, size);

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_write(mem_bio, encoded, encoded_length);
   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

   index = 0;
   while (0 < BIO_read(b64_bio, decoded + index, 1))
   {
      index++;
   }

   BIO_free_all(b64_bio);

   *raw = decoded;
   *raw_length = index;

   return 0;

error:

   *raw = NULL;
   *raw_length = 0;

   return 1;
}

void
pgexporter_set_proc_title(int argc, char** argv, char* s1, char* s2)
{
#ifdef HAVE_LINUX
   char title[MAX_PROCESS_TITLE_LENGTH];
   size_t size;
   char** env = environ;
   int es = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   // sanity check: if the user does not want to
   // update the process title, do nothing
   if (config->update_process_title == UPDATE_PROCESS_TITLE_NEVER)
   {
      return;
   }

   // compute how long was the command line
   // when the application was started
   if (max_process_title_size == 0)
   {
      char* end_of_area = NULL;

      /* Walk argv */
      for (int i = 0; i < argc; i++)
      {
         if (i == 0 || end_of_area + 1 == argv[i])
         {
            end_of_area = argv[i] + strlen(argv[i]);
         }
      }

      /* Walk original environ */
      if (end_of_area != NULL)
      {
         for (int i = 0; env[i] != NULL; i++)
         {
            if (end_of_area + 1 == env[i])
            {
               end_of_area = env[i] + strlen(env[i]);
            }
         }

         max_process_title_size = end_of_area - argv[0];
      }
   }

   if (!env_changed)
   {
      for (int i = 0; env[i] != NULL; i++)
      {
         es++;
      }

      environ = (char**)malloc(sizeof(char*) * (es + 1));
      if (environ == NULL)
      {
         return;
      }

      for (int i = 0; env[i] != NULL; i++)
      {
         size = strlen(env[i]);
         environ[i] = (char*)malloc(size + 1);

         if (environ[i] == NULL)
         {
            return;
         }

         memset(environ[i], 0, size + 1);
         memcpy(environ[i], env[i], size);
      }
      environ[es] = NULL;
      env_changed = true;
   }

   // compose the new title
   memset(&title, 0, sizeof(title));
   snprintf(title, sizeof(title) - 1, "pgexporter: %s%s%s",
            s1 != NULL ? s1 : "",
            s1 != NULL && s2 != NULL ? "/" : "",
            s2 != NULL ? s2 : "");

   // nuke the command line info
   memset(*argv, 0, max_process_title_size);

   // copy the new title over argv checking
   // the update_process_title policy
   if (config->update_process_title == UPDATE_PROCESS_TITLE_STRICT)
   {
      size = max_process_title_size;
   }
   else
   {
      // here we can set the title to a full description
      size = strlen(title) + 1;
   }

   memcpy(*argv, title, size);
   memset(*argv + size, 0, 1);

   // keep track of how long the title is now
   max_process_title_size = size;

#else
#if (defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
     defined(__OpenBSD__))
   setproctitle("-pgexporter: %s%s%s", s1 != NULL ? s1 : "",
                s1 != NULL && s2 != NULL ? "/" : "", s2 != NULL ? s2 : "");
#elif defined(HAVE_DARWIN)
   // macOS alternative to setproctitle
   // setprogname does not accept formatted strings
   char proc_title[128];
   snprintf(proc_title, sizeof(proc_title), "-pgexporter: %s%s%s",
            s1 != NULL ? s1 : "",
            s1 != NULL && s2 != NULL ? "/" : "",
            s2 != NULL ? s2 : "");
   setprogname(proc_title);
#endif

#endif
}

int
pgexporter_mkdir(char* dir)
{
   char* p;

   for (p = dir + 1; *p; p++)
   {
      if (*p == '/')
      {
         *p = '\0';

         if (mkdir(dir, S_IRWXU) != 0)
         {
            if (errno != EEXIST)
            {
               return 1;
            }

            errno = 0;
         }

         *p = '/';
      }
   }

   if (mkdir(dir, S_IRWXU) != 0)
   {
      if (errno != EEXIST)
      {
         return 1;
      }

      errno = 0;
   }

   return 0;
}

char*
pgexporter_vappend(char* orig, unsigned int n_str, ...)
{
   size_t orig_len, fin_len;
   va_list args;
   char* str = NULL;
   char** strings = NULL;
   char* ptr = NULL;

   if (orig)
   {
      fin_len = orig_len = strlen(orig);
   }
   else
   {
      fin_len = orig_len = 0;
   }

   strings = (char**)malloc(n_str * sizeof(char*));
   if (strings == NULL)
   {
      pgexporter_log_error("malloc failed for strings array");
      return orig;
   }

   va_start(args, n_str);

   for (unsigned int i = 0; i < n_str; i++)
   {
      strings[i] = va_arg(args, char*);
      fin_len += strlen(strings[i]);
   }

   char* new_str = (char*)realloc(orig, fin_len + 1);
   if (new_str == NULL)
   {
      pgexporter_log_error("realloc failed for appended string");
      free(strings);
      va_end(args);
      return orig;
   }

   str = new_str;
   ptr = str + orig_len;

   for (unsigned int i = 0; i < n_str; i++)
   {
      int j = 0;
      while (strings[i][j])
      {
         *ptr++ = strings[i][j++];
      }
   }

   *ptr = 0;

   va_end(args);

   free(strings);

   return str;
}

char*
pgexporter_append(char* orig, char* s)
{
   return pgexporter_vappend(orig, 1, s);
}

char*
pgexporter_format_and_append(char* buf, char* format, ...)
{
   va_list args;
   va_start(args, format);

   // Determine the required buffer size
   int size_needed = vsnprintf(NULL, 0, format, args) + 1;
   va_end(args);

   // Allocate buffer to hold the formatted string
   char* formatted_str = malloc(size_needed);

   va_start(args, format);
   vsnprintf(formatted_str, size_needed, format, args);
   va_end(args);

   buf = pgexporter_append(buf, formatted_str);

   free(formatted_str);

   return buf;
}

char*
pgexporter_append_int(char* orig, int i)
{
   char number[12];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 11, "%d", i);
   orig = pgexporter_append(orig, number);

   return orig;
}

char*
pgexporter_append_ulong(char* orig, unsigned long l)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%lu", l);
   orig = pgexporter_append(orig, number);

   return orig;
}

char*
pgexporter_append_bool(char* orig, bool b)
{
   if (b)
   {
      orig = pgexporter_append(orig, "1");
   }
   else
   {
      orig = pgexporter_append(orig, "0");
   }

   return orig;
}

char*
pgexporter_append_char(char* orig, char c)
{
   char str[2];

   memset(&str[0], 0, sizeof(str));
   snprintf(&str[0], 2, "%c", c);
   orig = pgexporter_append(orig, str);

   return orig;
}

char*
pgexporter_indent(char* str, char* tag, int indent)
{
   for (int i = 0; i < indent; i++)
   {
      str = pgexporter_append(str, " ");
   }
   if (tag != NULL)
   {
      str = pgexporter_append(str, tag);
   }
   return str;
}

bool
pgexporter_compare_string(const char* str1, const char* str2)
{
   if (str1 == NULL && str2 == NULL)
   {
      return true;
   }
   if ((str1 == NULL && str2 != NULL) || (str1 != NULL && str2 == NULL))
   {
      return false;
   }
   return strcmp(str1, str2) == 0;
}

unsigned long
pgexporter_directory_size(char* directory)
{
   unsigned long total_size = 0;
   DIR* dir;
   struct dirent* entry;
   char* p = NULL;
   struct stat st;
   unsigned long l;

   if (!(dir = opendir(directory)))
   {
      return total_size;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         total_size += pgexporter_directory_size(path);
      }
      else if (entry->d_type == DT_REG)
      {
         p = NULL;
         p = pgexporter_vappend(p, 3,
                                directory,
                                "/",
                                entry->d_name);

         memset(&st, 0, sizeof(struct stat));

         stat(p, &st);

         l = st.st_size / st.st_blksize;

         if (st.st_size % st.st_blksize != 0)
         {
            l += 1;
         }

         total_size += (l * st.st_blksize);

         free(p);
      }
      else if (entry->d_type == DT_LNK)
      {
         p = NULL;
         p = pgexporter_vappend(p, 3,
                                directory,
                                "/",
                                entry->d_name);

         memset(&st, 0, sizeof(struct stat));

         stat(p, &st);

         total_size += st.st_blksize;

         free(p);
      }
   }

   closedir(dir);

   return total_size;
}

int
pgexporter_get_directories(char* base, int* number_of_directories, char*** dirs)
{
   char* d = NULL;
   char** array = NULL;
   int nod = 0;
   int n;
   DIR* dir;
   struct dirent* entry;

   *number_of_directories = 0;
   *dirs = NULL;

   nod = 0;

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         nod++;
      }
   }

   closedir(dir);
   dir = NULL;

   dir = opendir(base);

   array = (char**)malloc(sizeof(char*) * nod);
   n = 0;

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         array[n] = (char*)malloc(strlen(entry->d_name) + 1);
         memset(array[n], 0, strlen(entry->d_name) + 1);
         memcpy(array[n], entry->d_name, strlen(entry->d_name));
         n++;
      }
   }

   closedir(dir);
   dir = NULL;

   pgexporter_sort(nod, array);

   free(d);
   d = NULL;

   *number_of_directories = nod;
   *dirs = array;

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   for (int i = 0; i < nod; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   *number_of_directories = 0;
   *dirs = NULL;

   return 1;
}

int
pgexporter_delete_directory(char* path)
{
   DIR* d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;
   int r2 = -1;
   char* buf;
   size_t len;
   struct dirent* entry;

   if (d)
   {
      r = 0;
      while (!r && (entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         len = path_len + strlen(entry->d_name) + 2;
         buf = malloc(len);

         if (buf)
         {
            struct stat statbuf;

            snprintf(buf, len, "%s/%s", path, entry->d_name);
            if (!stat(buf, &statbuf))
            {
               if (S_ISDIR(statbuf.st_mode))
               {
                  r2 = pgexporter_delete_directory(buf);
               }
               else
               {
                  r2 = unlink(buf);
               }
            }
            free(buf);
         }
         r = r2;
      }
      closedir(d);
   }

   if (!r)
   {
      r = rmdir(path);
   }

   return r;
}

int
pgexporter_get_files(char* base, int* number_of_files, char*** files)
{
   char* d = NULL;
   char** array = NULL;
   int nof = 0;
   int n;
   DIR* dir;
   struct dirent* entry;

   *number_of_files = 0;
   *files = NULL;

   nof = 0;

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         nof++;
      }
   }

   closedir(dir);
   dir = NULL;

   dir = opendir(base);

   array = (char**)malloc(sizeof(char*) * nof);
   n = 0;

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         array[n] = (char*)malloc(strlen(entry->d_name) + 1);
         memset(array[n], 0, strlen(entry->d_name) + 1);
         memcpy(array[n], entry->d_name, strlen(entry->d_name));
         n++;
      }
   }

   closedir(dir);
   dir = NULL;

   pgexporter_sort(nof, array);

   free(d);
   d = NULL;

   *number_of_files = nof;
   *files = array;

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   for (int i = 0; i < nof; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   *number_of_files = 0;
   *files = NULL;

   return 1;
}

char*
pgexporter_get_timestamp_string(time_t start_time, time_t end_time, int32_t* seconds)
{
   int32_t total_seconds;
   int hours;
   int minutes;
   int sec;
   char elapsed[128];
   char* result = NULL;

   *seconds = 0;

   total_seconds = (int32_t)difftime(end_time, start_time);

   *seconds = total_seconds;

   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   sec = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, sec);

   result = pgexporter_append(result, &elapsed[0]);

   return result;
}

int
pgexporter_delete_file(char* file)
{
   int ret;

   ret = unlink(file);

   if (ret != 0)
   {
      pgexporter_log_warn("pgexporter_delete_file: %s (%s)", file, strerror(errno));
      errno = 0;
      ret = 1;
   }

   return ret;
}

int
pgexporter_copy_directory(char* from, char* to)
{
   DIR* d = opendir(from);
   char* from_buffer;
   char* to_buffer;
   size_t from_length;
   size_t to_length;
   struct dirent* entry;
   struct stat statbuf;

   pgexporter_mkdir(to);

   if (d)
   {
      while ((entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         from_length = strlen(from) + strlen(entry->d_name) + 2;
         from_buffer = malloc(from_length);

         to_length = strlen(to) + strlen(entry->d_name) + 2;
         to_buffer = malloc(to_length);

         snprintf(from_buffer, from_length, "%s/%s", from, entry->d_name);
         snprintf(to_buffer, to_length, "%s/%s", to, entry->d_name);

         if (!stat(from_buffer, &statbuf))
         {
            if (S_ISDIR(statbuf.st_mode))
            {
               pgexporter_copy_directory(from_buffer, to_buffer);
            }
            else
            {
               pgexporter_copy_file(from_buffer, to_buffer);
            }
         }

         free(from_buffer);
         free(to_buffer);
      }
      closedir(d);
   }

   return 0;
}

int
pgexporter_copy_file(char* from, char* to)
{
   int fd_from = -1;
   int fd_to = -1;
   char buffer[8192];
   ssize_t nread = -1;
   int saved_errno = -1;

   fd_from = open(from, O_RDONLY);
   if (fd_from < 0)
   {
      goto error;
   }

   /* TODO: Permissions */
   fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0664);
   if (fd_to < 0)
   {
      goto error;
   }

   while ((nread = read(fd_from, buffer, sizeof(buffer))) > 0)
   {
      char* out = &buffer[0];
      ssize_t nwritten;

      do
      {
         nwritten = write(fd_to, out, nread);

         if (nwritten >= 0)
         {
            nread -= nwritten;
            out += nwritten;
         }
         else if (errno != EINTR)
         {
            goto error;
         }
      }
      while (nread > 0);
   }

   if (nread == 0)
   {
      if (close(fd_to) < 0)
      {
         fd_to = -1;
         goto error;
      }
      close(fd_from);
   }

   return 0;

error:
   saved_errno = errno;

   close(fd_from);
   if (fd_to >= 0)
   {
      close(fd_to);
   }

   errno = saved_errno;
   return 1;
}

int
pgexporter_move_file(char* from, char* to)
{
   int ret;

   ret = rename(from, to);
   if (ret != 0)
   {
      pgexporter_log_warn("pgexporter_move_file: %s -> %s (%s)", from, to, strerror(errno));
      errno = 0;
      ret = 1;
   }

   return ret;
}

int
pgexporter_basename_file(char* s, char** basename)
{
   size_t size;
   char* ext = NULL;
   char* r = NULL;

   *basename = NULL;

   ext = strrchr(s, '.');
   if (ext != NULL)
   {
      size = ext - s + 1;

      r = (char*)malloc(size);
      if (r == NULL)
      {
         goto error;
      }

      memset(r, 0, size);
      memcpy(r, s, size - 1);
   }
   else
   {
      size = strlen(s) + 1;

      r = (char*)malloc(size);
      if (r == NULL)
      {
         goto error;
      }

      memset(r, 0, size);
      memcpy(r, s, strlen(s));
   }

   *basename = r;

   return 0;

error:

   return 1;
}

bool
pgexporter_exists(char* f)
{
   if (access(f, F_OK) == 0)
   {
      return true;
   }

   return false;
}

bool
pgexporter_is_file(char* file)
{
   struct stat statbuf;

   memset(&statbuf, 0, sizeof(struct stat));

   if (!lstat(file, &statbuf))
   {
      if (S_ISREG(statbuf.st_mode))
      {
         return true;
      }
   }

   return false;
}

bool
pgexporter_is_directory(char* file)
{
   struct stat statbuf;
   memset(&statbuf, 0, sizeof(struct stat));

   if (!lstat(file, &statbuf))
   {
      if (S_ISDIR(statbuf.st_mode))
      {
         return true;
      }
   }
   return false;
}

bool
pgexporter_compare_files(char* f1, char* f2)
{
   FILE* fp1 = NULL;
   FILE* fp2 = NULL;
   struct stat statbuf1 = {0};
   struct stat statbuf2 = {0};
   size_t fs1;
   size_t fs2;
   char buf1[8192];
   char buf2[8192];
   size_t cs;
   size_t bs;

   fp1 = fopen(f1, "r");

   if (fp1 == NULL)
   {
      goto error;
   }

   fp2 = fopen(f2, "r");

   if (fp2 == NULL)
   {
      goto error;
   }

   memset(&statbuf1, 0, sizeof(struct stat));
   memset(&statbuf2, 0, sizeof(struct stat));

   if (stat(f1, &statbuf1) != 0)
   {
      errno = 0;
      goto error;
   }

   if (stat(f2, &statbuf2) != 0)
   {
      errno = 0;
      goto error;
   }

   if (statbuf1.st_size != statbuf2.st_size)
   {
      goto error;
   }

   cs = sizeof(char);
   bs = sizeof(buf1);

   while (!feof(fp1))
   {
      fs1 = fread(&buf1[0], cs, bs, fp1);
      fs2 = fread(&buf2[0], cs, bs, fp2);

      if (fs1 != fs2)
      {
         goto error;
      }

      if (memcmp(&buf1[0], &buf2[0], fs1) != 0)
      {
         goto error;
      }
   }

   fclose(fp1);
   fclose(fp2);

   return true;

error:

   if (fp1 != NULL)
   {
      fclose(fp1);
   }

   if (fp2 != NULL)
   {
      fclose(fp2);
   }

   return false;
}

int
pgexporter_symlink_file(char* from, char* to)
{
   int ret;

   ret = symlink(to, from);

   if (ret != 0)
   {
      pgexporter_log_warn("pgexporter_symlink_file: %s -> %s (%s)", from, to, strerror(errno));
      errno = 0;
      ret = 1;
   }

   return ret;
}

bool
pgexporter_is_symlink(char* file)
{
   struct stat statbuf;

   memset(&statbuf, 0, sizeof(struct stat));

   if (!lstat(file, &statbuf))
   {
      if (S_ISLNK(statbuf.st_mode))
      {
         return true;
      }
   }

   return false;
}

char*
pgexporter_get_symlink(char* symlink)
{
   ssize_t size;
   char link[1024];
   size_t alloc;
   char* result = NULL;

   memset(&link[0], 0, sizeof(link));
   size = readlink(symlink, &link[0], sizeof(link));
   link[size + 1] = '\0';

   alloc = strlen(&link[0]) + 1;
   result = malloc(alloc);
   memset(result, 0, alloc);
   memcpy(result, &link[0], strlen(&link[0]));

   return result;
}

int
pgexporter_copy_wal_files(char* from, char* to, char* start)
{
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   char* basename = NULL;
   char* ff = NULL;
   char* tf = NULL;

   pgexporter_get_files(from, &number_of_wal_files, &wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      pgexporter_basename_file(wal_files[i], &basename);

      if (strcmp(wal_files[i], start) >= 0)
      {
         if (pgexporter_ends_with(wal_files[i], ".partial"))
         {
            ff = pgexporter_append(ff, from);
            if (!pgexporter_ends_with(ff, "/"))
            {
               ff = pgexporter_append(ff, "/");
            }
            ff = pgexporter_append(ff, wal_files[i]);

            tf = pgexporter_append(tf, to);
            if (!pgexporter_ends_with(tf, "/"))
            {
               tf = pgexporter_append(tf, "/");
            }
            tf = pgexporter_append(tf, basename);
         }
         else
         {
            ff = pgexporter_append(ff, from);
            if (!pgexporter_ends_with(ff, "/"))
            {
               ff = pgexporter_append(ff, "/");
            }
            ff = pgexporter_append(ff, wal_files[i]);

            tf = pgexporter_append(tf, to);
            if (!pgexporter_ends_with(tf, "/"))
            {
               tf = pgexporter_append(tf, "/");
            }
            tf = pgexporter_append(tf, wal_files[i]);
         }

         pgexporter_copy_file(ff, tf);
      }

      free(basename);
      free(ff);
      free(tf);

      basename = NULL;
      ff = NULL;
      tf = NULL;
   }

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return 0;
}

int
pgexporter_number_of_wal_files(char* directory, char* from, char* to)
{
   int result;
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   char* basename = NULL;

   result = 0;

   pgexporter_get_files(directory, &number_of_wal_files, &wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      pgexporter_basename_file(wal_files[i], &basename);

      if (strcmp(basename, from) >= 0)
      {
         if (to == NULL || strcmp(basename, to) < 0)
         {
            result++;
         }
      }

      free(basename);
      basename = NULL;
   }

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return result;
}

unsigned long
pgexporter_free_space(char* path)
{
   struct statvfs buf;

   if (statvfs(path, &buf))
   {
      errno = 0;
      return 0;
   }

   return buf.f_bsize * buf.f_bavail;
}

unsigned long
pgexporter_total_space(char* path)
{
   struct statvfs buf;

   if (statvfs(path, &buf))
   {
      errno = 0;
      return 0;
   }

   return buf.f_frsize * buf.f_blocks;
}

bool
pgexporter_starts_with(char* str, char* prefix)
{
   return strncmp(prefix, str, strlen(prefix)) == 0;
}

bool
pgexporter_ends_with(char* str, char* suffix)
{
   int str_len = strlen(str);
   int suffix_len = strlen(suffix);

   return (str_len >= suffix_len) && (strcmp(str + (str_len - suffix_len), suffix) == 0);
}

void
pgexporter_sort(size_t size, char** array)
{
   qsort(array, size, sizeof(const char*), string_compare);
}

char*
pgexporter_bytes_to_string(uint64_t bytes)
{
   char* sizes[] = {"EB", "PB", "TB", "GB", "MB", "KB", "B"};
   uint64_t exbibytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
   uint64_t multiplier = exbibytes;
   char* result;

   result = (char*)malloc(sizeof(char) * 20);

   for (unsigned long i = 0; i < sizeof(sizes) / sizeof(*(sizes)); i++, multiplier /= 1024)
   {
      if (bytes < multiplier)
      {
         continue;
      }

      if (bytes % multiplier == 0)
      {
         sprintf(result, "%" PRIu64 " %s", bytes / multiplier, sizes[i]);
      }
      else
      {
         sprintf(result, "%.1f %s", (float)bytes / multiplier, sizes[i]);
      }

      return result;
   }

   strcpy(result, "0");
   return result;
}

int
pgexporter_read_version(char* directory, char** version)
{
   char* result = NULL;
   char* filename = NULL;
   FILE* file = NULL;
   char buf[3];

   *version = NULL;

   filename = pgexporter_vappend(filename, 2,
                                 directory,
                                 "/PG_VERSION");

   file = fopen(filename, "r");
   if (file == NULL)
   {
      goto error;
   }

   memset(&buf[0], 0, sizeof(buf));
   if (fgets(&buf[0], sizeof(buf), file) == NULL)
   {
      goto error;
   }

   result = malloc(strlen(&buf[0]) + 1);
   memset(result, 0, strlen(&buf[0]) + 1);
   memcpy(result, &buf[0], strlen(&buf[0]));

   *version = result;

   fclose(file);

   free(filename);

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   free(filename);

   return 1;
}

int
pgexporter_read_wal(char* directory, char** wal)
{
   bool found = false;
   char* result = NULL;
   char* pgwal = NULL;
   int number_of_wal_files = 0;
   char** wal_files = NULL;

   *wal = NULL;

   pgwal = pgexporter_vappend(pgwal, 2,
                              directory,
                              "/pg_wal/");

   number_of_wal_files = 0;
   wal_files = NULL;

   pgexporter_get_files(pgwal, &number_of_wal_files, &wal_files);

   if (number_of_wal_files == 0)
   {
      goto error;
   }

   for (int i = 0; !found && i < number_of_wal_files; i++)
   {
      if (is_wal_file(wal_files[i]))
      {
         result = malloc(strlen(wal_files[i]) + 1);
         memset(result, 0, strlen(wal_files[i]) + 1);
         memcpy(result, wal_files[i], strlen(wal_files[i]));

         *wal = result;

         found = true;
      }
   }

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return 0;

error:

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return 1;
}

char*
pgexporter_escape_string(char* str)
{
   if (str == NULL)
   {
      return NULL;
   }

   char* translated_ec_string = NULL;
   int len = 0;
   int idx = 0;
   size_t translated_len = 0;

   len = strlen(str);
   for (int i = 0; i < len; i++)
   {
      if (str[i] == '\"' || str[i] == '\\' || str[i] == '\n' || str[i] == '\t' || str[i] == '\r')
      {
         translated_len++;
      }
      translated_len++;
   }
   translated_ec_string = (char*)malloc(translated_len + 1);

   for (int i = 0; i < len; i++, idx++)
   {
      switch (str[i])
      {
         case '\\':
         case '\"':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = str[i];
            break;
         case '\n':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 'n';
            break;
         case '\t':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 't';
            break;
         case '\r':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 'r';
            break;
         default:
            translated_ec_string[idx] = str[i];
            break;
      }
   }
   translated_ec_string[idx] = '\0'; // terminator

   return translated_ec_string;
}

char*
pgexporter_remove_whitespace(char* orig)
{
   size_t length;
   char c = 0;
   char* result = NULL;

   if (orig == NULL || strlen(orig) == 0)
   {
      return orig;
   }

   length = strlen(orig);

   for (size_t i = 0; i < length; i++)
   {
      c = *(orig + i);
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      {
         /* Skip */
      }
      else
      {
         result = pgexporter_append_char(result, c);
      }
   }

   return result;
}

char*
pgexporter_remove_prefix(char* orig, char* prefix)
{
   char* res = NULL;
   int idx = 0;
   int len1 = strlen(orig);
   int len2 = strlen(prefix);
   int len = 0;
   if (orig == NULL)
   {
      return NULL;
   }
   // make a copy of the original one
   if (prefix == NULL || !strcmp(orig, "") || !pgexporter_starts_with(orig, prefix))
   {
      res = pgexporter_append(res, orig);
      return res;
   }
   while (idx < len1 && idx < len2)
   {
      if (orig[idx] == prefix[idx])
      {
         idx++;
      }
   }
   len = len1 - idx + 1;
   res = malloc(len);
   res[len - 1] = 0;
   if (len > 1)
   {
      strcpy(res, orig + idx);
   }
   return res;
}

char*
pgexporter_remove_suffix(char* orig, char* suffix)
{
   char* new_str = NULL;
   if (orig == NULL)
   {
      return new_str;
   }

   if (pgexporter_ends_with(orig, suffix))
   {
      new_str = (char*)malloc(strlen(orig) - strlen(suffix) + 1);

      if (new_str != NULL)
      {
         memset(new_str, 0, strlen(orig) - strlen(suffix) + 1);
         memcpy(new_str, orig, strlen(orig) - strlen(suffix));
      }
   }
   else
   {
      new_str = pgexporter_append(new_str, orig);
   }

   return new_str;
}

bool
pgexporter_is_number(char* str, int base)
{
   if (str == NULL || strlen(str) == 0)
   {
      return false;
   }

   if (base != 10 && base != 16)
   {
      return false;
   }

   for (int i = 0; str[i] != '\0'; i++)
   {
      if (str[i] >= 48 && str[i] <= 57)
      {
         /* Standard numbers */
      }
      else if (str[i] == '\r' || str[i] == '\n')
      {
         /* Feeds */
      }
      else if (base == 16)
      {
         if ((str[i] >= 65 && str[i] <= 70) || (str[i] >= 97 && str[i] <= 102))
         {
            /* Hex */
         }
         else
         {
            return false;
         }
      }
      else
      {
         return false;
      }
   }

   return true;
}

/* Parser for pgexporter-cli amd pgexporter-admin commands */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgexporter_parsed_command* parsed,
              const struct pgexporter_command command_table[],
              size_t command_count)
{
   char* command = NULL;
   char* subcommand = NULL;
   bool command_match = false;
   int default_command_match = -1;
   int arg_count = -1;
   int command_index = -1;
   int j;

   /* Parse command, and exit if there is no match */
   if (offset < argc)
   {
      command = argv[offset++];
   }
   else
   {
      warnx("A command is required\n");
      return false;
   }

   if (offset < argc)
   {
      subcommand = argv[offset];
   }

   for (size_t i = 0; i < command_count; i++)
   {
      if (strncmp(command, command_table[i].command, MISC_LENGTH) == 0)
      {
         command_match = true;
         if (subcommand && strncmp(subcommand, command_table[i].subcommand, MISC_LENGTH) == 0)
         {
            offset++;
            command_index = i;
            break;
         }
         else if (EMPTY_STR(command_table[i].subcommand))
         {
            /* Default command does not require a subcommand, might be followed by an argument */
            default_command_match = i;
         }
      }
   }

   if (command_match == false)
   {
      warnx("Unknown command '%s'\n", command);
      return false;
   }

   if (command_index == -1 && default_command_match >= 0)
   {
      command_index = default_command_match;
      subcommand = "";
   }
   else if (command_index == -1) /* Command was matched, but subcommand was not */
   {
      if (subcommand)
      {
         warnx("Unknown subcommand '%s' for command '%s'\n", subcommand, command);
      }
      else /* User did not type a subcommand */
      {
         warnx("Command '%s' requires a subcommand\n", command);
      }
      return false;
   }

   parsed->cmd = &command_table[command_index];

   /* Iterate until find an accepted_arg_count that is equal or greater than the typed command arg_count */
   arg_count = argc - offset;
   for (j = 0; j < MISC_LENGTH; j++)
   {
      if (parsed->cmd->accepted_argument_count[j] >= arg_count)
      {
         break;
      }
   }
   if (arg_count < parsed->cmd->accepted_argument_count[0])
   {
      warnx("Too few arguments provided for command '%s%s%s'\n", command,
            (command && !EMPTY_STR(subcommand)) ? " " : "", subcommand);
      return false;
   }
   if (j == MISC_LENGTH || arg_count > parsed->cmd->accepted_argument_count[j])
   {
      warnx("Too many arguments provided for command '%s%s%s'\n", command,
            (command && !EMPTY_STR(subcommand)) ? " " : "", subcommand);
      return false;
   }

   /* Copy argv + offset pointers into parsed->args */
   for (int i = 0; i < arg_count; i++)
   {
      parsed->args[i] = argv[i + offset];
   }
   parsed->args[0] = parsed->args[0] ? parsed->args[0] : (char*)parsed->cmd->default_argument;

   /* Warn the user if there is enough information about deprecation */
   if (parsed->cmd->deprecated && pgexporter_version_ge(parsed->cmd->deprecated_since_major,
                                                        parsed->cmd->deprecated_since_minor, 0))
   {
      warnx("command <%s> has been deprecated by <%s> since version %d.%d",
            parsed->cmd->command,
            parsed->cmd->deprecated_by,
            parsed->cmd->deprecated_since_major,
            parsed->cmd->deprecated_since_minor);
   }

   return true;
}

unsigned int
pgexporter_version_as_number(unsigned int major, unsigned int minor, unsigned int patch)
{
   return (patch % 100) + (minor % 100) * 100 + (major % 100) * 10000;
}

unsigned int
pgexporter_version_number(void)
{
   return pgexporter_version_as_number(PGEXPORTER_MAJOR_VERSION,
                                       PGEXPORTER_MINOR_VERSION,
                                       PGEXPORTER_PATCH_VERSION);
}

bool
pgexporter_version_ge(unsigned int major, unsigned int minor, unsigned int patch)
{
   if (pgexporter_version_number() >= pgexporter_version_as_number(major, minor, patch))
   {
      return true;
   }
   else
   {
      return false;
   }
}

static int
string_compare(const void* a, const void* b)
{
   return strcmp(*(const char**)a, *(const char**)b);
}

static bool
is_wal_file(char* file)
{
   if (pgexporter_ends_with(file, ".history"))
   {
      return false;
   }

   if (strlen(file) != 24)
   {
      return false;
   }

   return true;
}

int
pgexporter_resolve_path(char* orig_path, char** new_path)
{
#if defined(HAVE_DARWIN) || defined(HAVE_OSX)
#define GET_ENV(name) getenv(name)
#else
#define GET_ENV(name) secure_getenv(name)
#endif

   char* res = NULL;
   char* env_res = NULL;
   int len = strlen(orig_path);
   bool double_quote = false;
   bool single_quote = false;
   bool in_env = false;

   *new_path = NULL;

   if (orig_path == NULL)
   {
      goto error;
   }

   for (int idx = 0; idx < len; idx++)
   {
      char* ch = NULL;

      bool valid_env_char = orig_path[idx] == '_' || (orig_path[idx] >= 'A' && orig_path[idx] <= 'Z') || (orig_path[idx] >= 'a' && orig_path[idx] <= 'z') || (orig_path[idx] >= '0' && orig_path[idx] <= '9');
      if (in_env && !valid_env_char)
      {
         in_env = false;
         if (env_res == NULL)
         {
            return 1;
         }
         char* env_value = GET_ENV(env_res);
         free(env_res);
         if (env_value == NULL)
         {
            return 1;
         }
         res = pgexporter_append(res, env_value);
         env_res = NULL;
      }

      if (orig_path[idx] == '\"' && !single_quote)
      {
         double_quote = !double_quote;
         continue;
      }
      else if (orig_path[idx] == '\'' && !double_quote)
      {
         single_quote = !single_quote;
         continue;
      }

      if (orig_path[idx] == '\\')
      {
         if (idx + 1 < len)
         {
            ch = pgexporter_append_char(ch, orig_path[idx + 1]);
            idx++;
         }
         else
         {
            free(ch);
            return 1;
         }
      }
      else if (orig_path[idx] == '$')
      {
         if (single_quote)
         {
            ch = pgexporter_append_char(ch, '$');
         }
         else
         {
            in_env = true;
            continue;
         }
      }
      else
      {
         ch = pgexporter_append_char(ch, orig_path[idx]);
      }

      if (in_env)
      {
         env_res = pgexporter_append(env_res, ch);
      }
      else
      {
         res = pgexporter_append(res, ch);
      }

      free(ch);
   }

   if (strlen(res) > MAX_PATH)
   {
      goto error;
   }

   *new_path = res;
   return 0;

error:
   return 1;
}

__attribute__((unused)) static bool
calculate_offset(uint64_t addr, uint64_t* offset, char** filepath)
{
#if defined(HAVE_LINUX) && defined(HAVE_EXECINFO_H)
   char line[256];
   char *start, *end, *base_offset, *filepath_ptr;
   uint64_t start_addr, end_addr, base_offset_value;
   FILE* fp;
   bool success = false;

   fp = fopen("/proc/self/maps", "r");
   if (fp == NULL)
   {
      goto error;
   }

   while (fgets(line, sizeof(line), fp) != NULL)
   {
      // exmaple line:
      // 7fb60d1ea000-7fb60d20c000 r--p 00000000 103:02 120327460 /usr/lib/libc.so.6
      start = strtok(line, "-");
      end = strtok(NULL, " ");
      strtok(NULL, " "); // skip the next token
      base_offset = strtok(NULL, " ");
      strtok(NULL, " "); // skip the next token
      strtok(NULL, " "); // skip the next token
      filepath_ptr = strtok(NULL, " \n");
      if (start != NULL && end != NULL && base_offset != NULL && filepath_ptr != NULL)
      {
         start_addr = strtoul(start, NULL, 16);
         end_addr = strtoul(end, NULL, 16);
         if (addr >= start_addr && addr < end_addr)
         {
            success = true;
            break;
         }
      }
   }
   if (!success)
   {
      goto error;
   }

   base_offset_value = strtoul(base_offset, NULL, 16);
   *offset = addr - start_addr + base_offset_value;
   *filepath = pgexporter_append(*filepath, filepath_ptr);
   if (fp != NULL)
   {
      fclose(fp);
   }
   return 0;

error:
   if (fp != NULL)
   {
      fclose(fp);
   }
   return 1;

#else
   return 1;

#endif
}

int
pgexporter_backtrace(void)
{
   char* s = NULL;
   int ret = 0;

   ret = pgexporter_backtrace_string(&s);

   if (s != NULL)
   {
      pgexporter_log_debug(s);
   }

   free(s);

   return ret;
}

int
pgexporter_backtrace_string(char** s)
{
#ifdef HAVE_EXECINFO_H
   void* bt[1024];
   char* log_str = NULL;
   size_t bt_size;

   *s = NULL;

   bt_size = backtrace(bt, 1024);
   if (bt_size == 0)
   {
      goto error;
   }

   log_str = pgexporter_append(log_str, "Backtrace:\n");

   // the first element is ___interceptor_backtrace, so we skip it
   for (size_t i = 1; i < bt_size; i++)
   {
      uint64_t addr = (uint64_t)bt[i];
      uint64_t offset;
      char* filepath = NULL;
      char buffer[256], log_buffer[64];
      bool found_main = false;
      int p[2];
      pid_t pid;

      if (calculate_offset(addr, &offset, &filepath))
      {
         continue;
      }

      if (pipe(p) < 0)
      {
         pgexporter_log_debug("Failed to create pipe: %s", strerror(errno));
         free(filepath);
         continue;
      }

      pid = fork();
      if (pid == -1)
      {
         pgexporter_log_debug("Failed to fork: %s", strerror(errno));
         close(p[0]);
         close(p[1]);
         free(filepath);
         continue;
      }
      else if (pid == 0)
      {
         // Child process
         char addr_hex[32];

         close(p[0]);
         if (dup2(p[1], STDOUT_FILENO) == -1)
         {
            _exit(1);
         }
         close(p[1]);

         memset(&addr_hex[0], 0, sizeof(addr_hex));
         snprintf(&addr_hex[0], sizeof(addr_hex), "0x%lx", offset);

         char* args[] = {"addr2line", "-e", filepath, "-fC", &addr_hex[0], NULL};
         execvp("addr2line", args);
         _exit(1);
      }
      else
      {
         // Parent process
         FILE* fp;

         close(p[1]);
         fp = fdopen(p[0], "r");
         if (fp == NULL)
         {
            pgexporter_log_debug("Failed to open pipe for reading: %s", strerror(errno));
            close(p[0]);
            waitpid(pid, NULL, 0);
            free(filepath);
            continue;
         }

         if (fgets(buffer, sizeof(buffer), fp) == NULL)
         {
            pgexporter_log_debug("Failed to read from command output: %s", strerror(errno));
            fclose(fp);
            waitpid(pid, NULL, 0);
            free(filepath);
            continue;
         }

         buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
         if (strcmp(buffer, "main") == 0)
         {
            found_main = true;
         }
         snprintf(log_buffer, sizeof(log_buffer), "#%zu  0x%lx in ", i - 1, addr);
         log_str = pgexporter_append(log_str, log_buffer);
         log_str = pgexporter_append(log_str, buffer);
         log_str = pgexporter_append(log_str, "\n");

         if (fgets(buffer, sizeof(buffer), fp) == NULL)
         {
            log_str = pgexporter_append(log_str, "\tat ???:??\n");
         }
         else
         {
            buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
            log_str = pgexporter_append(log_str, "\tat ");
            log_str = pgexporter_append(log_str, buffer);
            log_str = pgexporter_append(log_str, "\n");
         }

         fclose(fp);
         waitpid(pid, NULL, 0);
         free(filepath);

         if (found_main)
         {
            break;
         }
      }
   }

   *s = log_str;

   return 0;

error:
   if (log_str != NULL)
   {
      free(log_str);
   }
   return 1;
#else
   return 1;
#endif
}

int
pgexporter_os_kernel_version(char** os, int* kernel_major, int* kernel_minor, int* kernel_patch)
{
   bool bsd = false;
   *os = NULL;
   *kernel_major = 0;
   *kernel_minor = 0;
   *kernel_patch = 0;

#if defined(HAVE_LINUX) || defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD)
   struct utsname buffer;

   if (uname(&buffer) != 0)
   {
      pgexporter_log_debug("Failed to retrieve system information.");
      goto error;
   }

   // Copy system name using pgexporter_append (dynamically allocated)
   *os = pgexporter_append(NULL, buffer.sysname);
   if (*os == NULL)
   {
      pgexporter_log_debug("Failed to allocate memory for OS name.");
      goto error;
   }

   // Parse kernel version based on OS
#if defined(HAVE_LINUX)
   if (sscanf(buffer.release, "%d.%d.%d", kernel_major, kernel_minor, kernel_patch) < 2)
   {
      pgexporter_log_debug("Failed to parse Linux kernel version.");
      goto error;
   }
#elif defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD)
   if (sscanf(buffer.release, "%d.%d", kernel_major, kernel_minor) < 2)
   {
      pgexporter_log_debug("Failed to parse BSD OS kernel version.");
      goto error;
   }
   *kernel_patch = 0; // BSD doesn't use patch version
   bsd = true;

#endif

   if (!bsd)
   {
      pgexporter_log_debug("OS: %s | Kernel Version: %d.%d.%d", *os, *kernel_major, *kernel_minor, *kernel_patch);
   }
   else
   {
      pgexporter_log_debug("OS: %s | Version: %d.%d", *os, *kernel_major, *kernel_minor);
   }

   return 0;
error:
   //Free memory if already allocated
   if (*os != NULL)
   {
      free(*os);
      *os = NULL;
   }

   *os = pgexporter_append(NULL, "Unknown");
   if (*os == NULL)
   {
      pgexporter_log_debug("Failed to allocate memory for unknown OS name.");
   }

   pgexporter_log_debug("Unable to retrieve OS and kernel version.");

   *kernel_major = 0;
   *kernel_minor = 0;
   *kernel_patch = 0;
   return 1;

#else
   *os = pgexporter_append(NULL, "Unknown");
   if (*os == NULL)
   {
      pgexporter_log_debug("Failed to allocate memory for unknown OS name.");
   }

   pgexporter_log_debug("Kernel version not available.");
   return 1;
#endif
}

bool
pgexporter_is_valid_metric_name(char* name)
{
   size_t len;
   size_t i;

   if (!name)
   {
      return false;
   }

   len = strlen(name);
   if (len == 0)
   {
      return false;
   }

   for (i = 0; i < len; i++)
   {
      char c = name[i];
      if (!((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            (c == '_')))
      {
         return false;
      }
   }

   return true;
}

int
pgexporter_normalize_path(char* directory_path, char* filename, char* default_path, char* path_buffer, size_t buffer_size)
{
   char* temp_path = NULL;

   if (path_buffer == NULL || buffer_size == 0 || filename == NULL)
   {
      return 1; // Invalid input
   }

   memset(path_buffer, 0, buffer_size);

   // If directory_path is provided, try to find the file there first
   if (directory_path != NULL)
   {
      temp_path = pgexporter_append(NULL, directory_path);
      if (temp_path == NULL)
      {
         return 1;
      }

      // Add "/" if needed
      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         temp_path = pgexporter_append(temp_path, "/");
         if (temp_path == NULL)
         {
            return 1;
         }
      }

      // Add filename
      temp_path = pgexporter_append(temp_path, filename);
      if (temp_path == NULL)
      {
         return 1;
      }

      // Check if the result fits in our buffer
      if (strlen(temp_path) >= buffer_size)
      {
         pgexporter_log_error("Configuration directory path is too long: %s (maximum %zu characters)",
                              temp_path, buffer_size - 1);
         free(temp_path);
         return 1;
      }

      // Check if file exists in the specified directory
      if (access(temp_path, F_OK) == 0)
      {
         pgexporter_log_debug("Using config file: %s", temp_path);
         strcpy(path_buffer, temp_path);
         free(temp_path);
         return 0;
      }
      else
      {
         pgexporter_log_info("Config file %s not found in directory %s", filename, directory_path);
         free(temp_path);
      }
   }

   // Now check if default file exists
   if (default_path != NULL)
   {
      if (access(default_path, F_OK) == 0)
      {
         if (strlen(default_path) >= buffer_size)
         {
            pgexporter_log_error("Default configuration path is too long: %s (maximum %zu characters)",
                                 default_path, buffer_size - 1);
            return 1;
         }
         pgexporter_log_info("Using default config file: %s", default_path);
         strcpy(path_buffer, default_path);
         return 0; // Default file exists and is being used
      }
      else
      {
         pgexporter_log_info("Default config file %s not found, continuing without %s", default_path, filename);
         return 0; // Default doesn't exist, but that's okay for optional files
      }
   }

   // No directory specified and no default path provided
   pgexporter_log_warn("No path specified for config file %s", filename);
   return 1;
}

static void
append_bounded(char** out, const char* s, size_t current_len, size_t cap)
{
   if (s == NULL || cap <= current_len)
   {
      return;
   }

   size_t remain = cap - current_len;
   size_t slen = strlen(s);
   size_t to_copy = (slen > remain) ? remain : slen;

   if (to_copy == 0)
   {
      return;
   }

   char* chunk = (char*)malloc(to_copy + 1);
   if (chunk == NULL)
   {
      return;
   }

   memcpy(chunk, s, to_copy);
   chunk[to_copy] = '\0';
   *out = pgexporter_append(*out, chunk);
   free(chunk);
}

static void
append_char_bounded(char** out, char c, size_t current_len, size_t cap)
{
   if (current_len < cap)
   {
      *out = pgexporter_append_char(*out, c);
   }
}

static int
hvsnprintf(char* buf, size_t n, const char* fmt, va_list ap)
{
   size_t cap = 8192;
   if (n > 0 && (n - 1) < cap)
   {
      cap = n - 1;
   }

   char* out = NULL;
   const char* p = (fmt != NULL) ? fmt : "";
   char scratch[128];

   while (*p != '\0')
   {
      if (*p != '%')
      {
         size_t cur = (out != NULL) ? strlen(out) : 0;
         append_char_bounded(&out, *p, cur, cap);
         p++;
         continue;
      }

      p++;
      if (*p == '%')
      {
         size_t cur = (out != NULL) ? strlen(out) : 0;
         append_char_bounded(&out, '%', cur, cap);
         p++;
         continue;
      }

      /* Parse flags (support '0' for zero-padding) */
      bool flag_zero = false;
      while (*p == '0')
      {
         flag_zero = true;
         p++;
      }

      /* Parse width */
      int width = -1;
      if (isdigit((unsigned char)*p))
      {
         width = 0;
         while (isdigit((unsigned char)*p))
         {
            width = width * 10 + (*p - '0');
            p++;
         }
      }

      /* Parse precision */
      int precision = -1;
      if (*p == '.')
      {
         p++;
         if (*p == '*')
         {
            precision = va_arg(ap, int);
            p++;
         }
         else
         {
            precision = 0;
            while (isdigit((unsigned char)*p))
            {
               precision = precision * 10 + (*p - '0');
               p++;
            }
         }
         if (precision < 0)
         {
            precision = -1;
         }
      }

      /* Length modifier */
      enum { LM_NONE,
             LM_L,
             LM_LL,
             LM_Z } lm = LM_NONE;
      if (*p == 'l')
      {
         p++;
         if (*p == 'l')
         {
            lm = LM_LL;
            p++;
         }
         else
         {
            lm = LM_L;
         }
      }
      else if (*p == 'z')
      {
         lm = LM_Z;
         p++;
      }

      char conv = *p;
      if (conv == '\0')
      {
         break;
      }
      p++;

      scratch[0] = '\0';

      switch (conv)
      {
         case 's':
         {
            char* s = va_arg(ap, char*);
            if (s == NULL)
            {
               s = "(null)";
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            if (precision >= 0)
            {
               for (int i = 0; s[i] != '\0' && i < precision && cur < cap; i++)
               {
                  append_char_bounded(&out, s[i], cur, cap);
                  cur++;
               }
            }
            else
            {
               append_bounded(&out, s, cur, cap);
            }
            break;
         }
         case 'c':
         {
            int ch = va_arg(ap, int);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, (char)ch, cur, cap);
            break;
         }
         case 'd':
         case 'i':
         {
            long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, long);
            }
            else if (lm == LM_Z)
            {
               v = (ssize_t)va_arg(ap, ssize_t);
            }
            else
            {
               v = va_arg(ap, int);
            }

            if (width >= 0)
            {
               (void)snprintf(scratch, sizeof(scratch),
                              flag_zero ? "%0*lld" : "%*lld", width, v);
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch), "%lld", v);
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'u':
         {
            unsigned long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, unsigned long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, unsigned long);
            }
            else if (lm == LM_Z)
            {
               v = (size_t)va_arg(ap, size_t);
            }
            else
            {
               v = va_arg(ap, unsigned int);
            }

            if (width >= 0)
            {
               (void)snprintf(scratch, sizeof(scratch),
                              flag_zero ? "%0*llu" : "%*llu", width, v);
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch), "%llu", v);
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'x':
         case 'X':
         {
            unsigned long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, unsigned long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, unsigned long);
            }
            else if (lm == LM_Z)
            {
               v = (size_t)va_arg(ap, size_t);
            }
            else
            {
               v = va_arg(ap, unsigned int);
            }

            if (width >= 0)
            {
               if (conv == 'x')
               {
                  (void)snprintf(scratch, sizeof(scratch),
                                 flag_zero ? "%0*llx" : "%*llx", width, v);
               }
               else
               {
                  (void)snprintf(scratch, sizeof(scratch),
                                 flag_zero ? "%0*llX" : "%*llX", width, v);
               }
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch),
                              (conv == 'x') ? "%llx" : "%llX", v);
            }

            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'p':
         {
            void* ptr = va_arg(ap, void*);
            (void)snprintf(scratch, sizeof(scratch), "%p", ptr);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'f':
         case 'F':
         case 'g':
         case 'G':
         case 'e':
         case 'E':
         {
            double dv = va_arg(ap, double);
            (void)snprintf(scratch, sizeof(scratch), "%g", dv);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         default:
         {
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, '%', cur, cap);
            cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, conv, cur, cap);
            break;
         }
      }
   }

   size_t produced_len = (out != NULL) ? strlen(out) : 0;

   if (buf != NULL && n > 0)
   {
      size_t to_copy = (produced_len < (n - 1)) ? produced_len : (n - 1);
      if (to_copy > 0 && out != NULL)
      {
         memcpy(buf, out, to_copy);
      }
      if (n > 0)
      {
         buf[to_copy] = '\0';
      }
   }

   if (out != NULL)
   {
      free(out);
   }

   return (int)produced_len;
}

int
pgexporter_snprintf(char* buf, size_t n, const char* fmt, ...)
{
   va_list ap;
   va_list ap_copy;
   int ret;

   va_start(ap, fmt);
   va_copy(ap_copy, ap);
   ret = vsnprintf(buf, n, fmt, ap_copy);
   va_end(ap_copy);
   if (ret < 0)
   {
      ret = hvsnprintf(buf, n, fmt, ap);
   }
   va_end(ap);

   return ret;
}

int64_t
pgexporter_time_convert(pgexporter_time_t t, enum pgexporter_time_format_t fmt)
{
   switch (fmt)
   {
      case FORMAT_TIME_MS:
         return t.ms;
      case FORMAT_TIME_S:
         return t.ms / 1000;
      case FORMAT_TIME_MIN:
         return t.ms / 60000;
      case FORMAT_TIME_HOUR:
         return t.ms / 3600000;
      case FORMAT_TIME_DAY:
         return t.ms / 86400000;
      default:
         return t.ms;
   }
}

bool
pgexporter_time_is_valid(pgexporter_time_t t)
{
   return t.ms > 0;
}

int
pgexporter_time_format(pgexporter_time_t t, enum pgexporter_time_format_t fmt, char** output)
{
   char* str = NULL;
   int64_t val;

   if (output == NULL)
   {
      return 1;
   }

   str = malloc(64);
   if (str == NULL)
   {
      return 1;
   }
   memset(str, 0, 64);

   if (fmt == FORMAT_TIME_TIMESTAMP)
   {
      /* Format as ISO 8601 UTC timestamp */
      time_t secs = (time_t)(t.ms / 1000);
      int ms_remainder = (int)(t.ms % 1000);
      struct tm tm_buf;
      gmtime_r(&secs, &tm_buf);
      strftime(str, 48, "%Y-%m-%dT%H:%M:%S", &tm_buf);
      sprintf(str + strlen(str), ".%03dZ", ms_remainder);
   }
   else
   {
      val = pgexporter_time_convert(t, fmt);

      switch (fmt)
      {
         case FORMAT_TIME_MS:
            sprintf(str, "%" PRId64 "ms", val);
            break;
         case FORMAT_TIME_S:
            sprintf(str, "%" PRId64 "s", val);
            break;
         case FORMAT_TIME_MIN:
            sprintf(str, "%" PRId64 "m", val);
            break;
         case FORMAT_TIME_HOUR:
            sprintf(str, "%" PRId64 "h", val);
            break;
         case FORMAT_TIME_DAY:
            sprintf(str, "%" PRId64 "d", val);
            break;
         default:
            sprintf(str, "%" PRId64, val);
            break;
      }
   }

   *output = str;
   return 0;
}
