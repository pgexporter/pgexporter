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

/* pgexporter */
#include <pgexporter.h>
#include <aes.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define SECURITY_INVALID  -2
#define SECURITY_REJECT   -1
#define SECURITY_TRUST     0
#define SECURITY_PASSWORD  3
#define SECURITY_MD5       5
#define SECURITY_SCRAM256 10
#define SECURITY_ALL      99

#define NUMBER_OF_SECURITY_MESSAGES    5
#define SECURITY_BUFFER_SIZE        1024

static signed char has_security;
static ssize_t security_lengths[NUMBER_OF_SECURITY_MESSAGES];
static char security_messages[NUMBER_OF_SECURITY_MESSAGES][SECURITY_BUFFER_SIZE];

static int get_auth_type(struct message* msg, int* auth_type);
static int get_salt(void* data, char** salt);
static int generate_md5(char* str, int length, char** md5);

static int client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot);

static int server_trust(void);
static int server_password(char* username, char* password, SSL* ssl, int server_fd);
static int server_md5(char* username, char* password, SSL* ssl, int server_fd);
static int server_scram256(char* username, char* password, SSL* ssl, int server_fd);

static char* get_admin_password(char* username);

static int sasl_prep(char* password, char** password_prep);
static int generate_nounce(char** nounce);
static int get_scram_attribute(char attribute, char* input, size_t size, char** value);
static int client_proof(char* password, char* salt, int salt_length, int iterations,
                        char* client_first_message_bare, size_t client_first_message_bare_length,
                        char* server_first_message, size_t server_first_message_length,
                        char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                        unsigned char** result, size_t* result_length);
static int  salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length);
static int  salted_password_key(unsigned char* salted_password, int salted_password_length, char* key,
                                unsigned char** result, int* result_length);
static int  stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length);
static int  generate_salt(char** salt, int* size);
static int  server_signature(char* password, char* salt, int salt_length, int iterations,
                             char* server_key, int server_key_length,
                             char* client_first_message_bare, size_t client_first_message_bare_length,
                             char* server_first_message, size_t server_first_message_length,
                             char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                             unsigned char** result, size_t* result_length);

static int  create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl);

int
pgexporter_remote_management_auth(int client_fd, char* address, SSL** client_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* request_msg = NULL;
   int32_t request;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   char* password = NULL;
   SSL* c_ssl = NULL;

   config = (struct configuration*)shmem;

   *client_ssl = NULL;

   /* Receive client calls - at any point if client exits return AUTH_ERROR */
   status = pgexporter_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   request = pgexporter_get_request(msg);

   /* SSL request: 80877103 */
   if (request == 80877103)
   {
      pgexporter_log_debug("SSL request from client: %d", client_fd);

      if (config->tls)
      {
         SSL_CTX* ctx = NULL;

         /* We are acting as a server against the client */
         if (pgexporter_create_ssl_ctx(false, &ctx))
         {
            goto error;
         }

         if (pgexporter_create_ssl_server(ctx, config->tls_key_file, config->tls_cert_file, config->tls_ca_file, client_fd, &c_ssl))
         {
            goto error;
         }

         *client_ssl = c_ssl;

         /* Switch to TLS mode */
         status = pgexporter_write_tls(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgexporter_clear_message();

         status = SSL_accept(c_ssl);
         if (status != 1)
         {
            unsigned long err;

            err = ERR_get_error();
            pgexporter_log_error("SSL failed: %s", ERR_reason_error_string(err));
            goto error;
         }

         status = pgexporter_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgexporter_get_request(msg);
      }
      else
      {
         status = pgexporter_write_notice(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgexporter_clear_message();

         status = pgexporter_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgexporter_get_request(msg);
      }
   }

   /* 196608 -> Ok */
   if (request == 196608)
   {
      request_msg = pgexporter_copy_message(msg);

      /* Extract parameters: username / database */
      pgexporter_log_trace("remote_management_auth: username/database (%d)", client_fd);
      pgexporter_extract_username_database(request_msg, &username, &database, &appname);

      /* Must be admin database */
      if (strcmp("admin", database) != 0)
      {
         pgexporter_log_debug("remote_management_auth: admin: %s / %s", username, address);
         pgexporter_write_connection_refused(c_ssl, client_fd);
         pgexporter_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      password = get_admin_password(username);
      if (password == NULL)
      {
         pgexporter_log_debug("remote_management_auth: password: %s / admin / %s", username, address);
         pgexporter_write_connection_refused(c_ssl, client_fd);
         pgexporter_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      status = client_scram256(c_ssl, client_fd, username, password, -1);
      if (status == AUTH_BAD_PASSWORD)
      {
         pgexporter_write_connection_refused(c_ssl, client_fd);
         pgexporter_write_empty(c_ssl, client_fd);
         goto bad_password;
      }
      else if (status == AUTH_ERROR)
      {
         pgexporter_write_connection_refused(c_ssl, client_fd);
         pgexporter_write_empty(c_ssl, client_fd);
         goto error;
      }

      status = pgexporter_write_auth_success(c_ssl, client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      pgexporter_free_message(request_msg);
      free(username);
      free(database);
      free(appname);

      pgexporter_log_debug("remote_management_auth: SUCCESS");
      return AUTH_SUCCESS;
   }
   else if (request == -1)
   {
      goto error;
   }
   else
   {
      pgexporter_log_debug("remote_management_auth: old version: %d (%s)", request, address);
      pgexporter_write_connection_refused_old(c_ssl, client_fd);
      pgexporter_write_empty(c_ssl, client_fd);
      goto bad_password;
   }

bad_password:
   pgexporter_clear_message();
   pgexporter_free_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgexporter_log_debug("remote_management_auth: BAD_PASSWORD");
   return AUTH_BAD_PASSWORD;

error:
   pgexporter_clear_message();
   pgexporter_free_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgexporter_log_debug("remote_management_auth: ERROR");
   return AUTH_ERROR;
}

int
pgexporter_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   SSL* ssl = NULL;
   char key_file[MAX_PATH];
   char cert_file[MAX_PATH];
   char root_file[MAX_PATH];
   struct stat st = {0};
   char* salt = NULL;
   size_t salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   size_t proof_length;
   char* proof_base = NULL;
   size_t proof_base_length;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   size_t server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   size_t server_signature_calc_length;
   struct message* sslrequest_msg = NULL;
   struct message* startup_msg = NULL;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;

   pgexporter_memory_init();

   if (pgexporter_get_home_directory() == NULL)
   {
      goto error;
   }

   memset(&key_file, 0, sizeof(key_file));
   snprintf(&key_file[0], sizeof(key_file), "%s/.pgexporter/pgexporter.key", pgexporter_get_home_directory());

   memset(&cert_file, 0, sizeof(cert_file));
   snprintf(&cert_file[0], sizeof(cert_file), "%s/.pgexporter/pgexporter.crt", pgexporter_get_home_directory());

   memset(&root_file, 0, sizeof(root_file));
   snprintf(&root_file[0], sizeof(root_file), "%s/.pgexporter/root.crt", pgexporter_get_home_directory());

   if (stat(&key_file[0], &st) == 0)
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         if (stat(&cert_file[0], &st) == 0)
         {
            if (S_ISREG(st.st_mode))
            {
               SSL_CTX* ctx = NULL;

               status = pgexporter_create_ssl_message(&sslrequest_msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               status = pgexporter_write_message(NULL, server_fd, sslrequest_msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               status = pgexporter_read_block_message(NULL, server_fd, &msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               if (msg->kind == 'S')
               {
                  if (pgexporter_create_ssl_ctx(true, &ctx))
                  {
                     goto error;
                  }

                  if (stat(&root_file[0], &st) == -1)
                  {
                     memset(&root_file, 0, sizeof(root_file));
                  }

                  if (create_ssl_client(ctx, &key_file[0], &cert_file[0], &root_file[0], server_fd, &ssl))
                  {
                     goto error;
                  }

                  *s_ssl = ssl;

                  do
                  {
                     status = SSL_connect(ssl);

                     if (status != 1)
                     {
                        int err = SSL_get_error(ssl, status);
                        switch (err)
                        {
                           case SSL_ERROR_ZERO_RETURN:
                           case SSL_ERROR_WANT_READ:
                           case SSL_ERROR_WANT_WRITE:
                           case SSL_ERROR_WANT_CONNECT:
                           case SSL_ERROR_WANT_ACCEPT:
                           case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
                           case SSL_ERROR_WANT_ASYNC:
                           case SSL_ERROR_WANT_ASYNC_JOB:
                           case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
                              break;
                           case SSL_ERROR_SYSCALL:
                              pgexporter_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), server_fd);
                              errno = 0;
                              goto error;
                              break;
                           case SSL_ERROR_SSL:
                              pgexporter_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), server_fd);
                              pgexporter_log_error("%s", ERR_error_string(err, NULL));
                              pgexporter_log_error("%s", ERR_lib_error_string(err));
                              pgexporter_log_error("%s", ERR_reason_error_string(err));
                              errno = 0;
                              goto error;
                              break;
                        }
                        ERR_clear_error();
                     }
                  }
                  while (status != 1);
               }
            }
         }
      }
   }

   status = pgexporter_create_startup_message(username, "admin", &startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_write_message(ssl, server_fd, startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (msg->kind != 'R')
   {
      goto error;
   }

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgexporter_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_write_message(ssl, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgexporter_copy_message(msg);

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      goto error;
   }

   pgexporter_base64_decode(base64_salt, strlen(base64_salt), (void**)&salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = sasl_response->data + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = sasl_continue->data + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, sasl_response->length - 26,
                    server_first_message, sasl_continue->length - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgexporter_base64_encode((char*)proof, proof_length, &proof_base, &proof_base_length);

   status = pgexporter_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_write_message(ssl, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (pgexporter_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgexporter_base64_decode(base64_server_signature, sasl_final->length - 11, (void**)&server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, sasl_response->length - 26,
                        server_first_message, sasl_continue->length - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   if (msg->length == 55)
   {
      status = pgexporter_read_block_message(ssl, server_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
   }

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgexporter_clear_message();
   pgexporter_free_message(sslrequest_msg);
   pgexporter_free_message(startup_msg);
   pgexporter_free_message(sasl_response);
   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_continue_response);
   pgexporter_free_message(sasl_final);

   pgexporter_memory_destroy();

   return AUTH_SUCCESS;

bad_password:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgexporter_clear_message();
   pgexporter_free_message(sslrequest_msg);
   pgexporter_free_message(startup_msg);
   pgexporter_free_message(sasl_response);
   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_continue_response);
   pgexporter_free_message(sasl_final);

   pgexporter_memory_destroy();

   return AUTH_BAD_PASSWORD;

error:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgexporter_clear_message();
   pgexporter_free_message(sslrequest_msg);
   pgexporter_free_message(startup_msg);
   pgexporter_free_message(sasl_response);
   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_continue_response);
   pgexporter_free_message(sasl_final);

   pgexporter_memory_destroy();

   return AUTH_ERROR;
}

static int
get_auth_type(struct message* msg, int* auth_type)
{
   int32_t length;
   int32_t type = -1;
   int offset;

   if (msg->kind != 'R')
   {
      return 1;
   }

   length = pgexporter_read_int32(msg->data + 1);
   type = pgexporter_read_int32(msg->data + 5);
   offset = 9;

   if (type == 0 && msg->length > 8)
   {
      if ('E' == pgexporter_read_byte(msg->data + 9))
      {
         *auth_type = -1;
         return 0;
      }
   }

   switch (type)
   {
      case 0:
         pgexporter_log_trace("Backend: R - Success");
         break;
      case 2:
         pgexporter_log_trace("Backend: R - KerberosV5");
         break;
      case 3:
         pgexporter_log_trace("Backend: R - CleartextPassword");
         break;
      case 5:
         pgexporter_log_trace("Backend: R - MD5Password");
         pgexporter_log_trace("             Salt %02hhx%02hhx%02hhx%02hhx",
                              (signed char)(pgexporter_read_byte(msg->data + 9) & 0xFF),
                              (signed char)(pgexporter_read_byte(msg->data + 10) & 0xFF),
                              (signed char)(pgexporter_read_byte(msg->data + 11) & 0xFF),
                              (signed char)(pgexporter_read_byte(msg->data + 12) & 0xFF));
         break;
      case 6:
         pgexporter_log_trace("Backend: R - SCMCredential");
         break;
      case 7:
         pgexporter_log_trace("Backend: R - GSS");
         break;
      case 8:
         pgexporter_log_trace("Backend: R - GSSContinue");
         break;
      case 9:
         pgexporter_log_trace("Backend: R - SSPI");
         break;
      case 10:
         pgexporter_log_trace("Backend: R - SASL");
         while (offset < length - 8)
         {
            char* mechanism = pgexporter_read_string(msg->data + offset);
            pgexporter_log_trace("             %s", mechanism);
            offset += strlen(mechanism) + 1;
         }
         break;
      case 11:
         pgexporter_log_trace("Backend: R - SASLContinue");
         break;
      case 12:
         pgexporter_log_trace("Backend: R - SASLFinal");
         offset += length - 8;

         if (offset < msg->length)
         {
            signed char peek = pgexporter_read_byte(msg->data + offset);
            switch (peek)
            {
               case 'R':
                  type = pgexporter_read_int32(msg->data + offset + 5);
                  break;
               default:
                  break;
            }
         }

         break;
      default:
         break;
   }

   *auth_type = type;

   return 0;
}

static int
get_salt(void* data, char** salt)
{
   char* result;

   result = malloc(4);
   memset(result, 0, 4);

   memcpy(result, data + 9, 4);

   *salt = result;

   return 0;
}

static int
generate_md5(char* str, int length, char** md5)
{
   int n;
   MD5_CTX c;
   unsigned char digest[16];
   char* out;

   out = malloc(33);

   memset(out, 0, 33);

   MD5_Init(&c);
   MD5_Update(&c, str, length);
   MD5_Final(digest, &c);

   for (n = 0; n < 16; ++n)
   {
      snprintf(&(out[n * 2]), 32, "%02x", (unsigned int)digest[n]);
   }

   *md5 = out;

   return 0;
}

static int
client_scram256(SSL* c_ssl, int client_fd, char* username __attribute__((unused)), char* password, int slot)
{
   int status;
   time_t start_time;
   bool non_blocking;
   char* password_prep = NULL;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char* client_final_message_without_proof = NULL;
   char* client_nounce = NULL;
   char* server_nounce = NULL;
   char* salt = NULL;
   int salt_length = 0;
   char* base64_salt = NULL;
   size_t base64_salt_length;
   char* base64_client_proof = NULL;
   char* client_proof_received = NULL;
   size_t client_proof_received_length = 0;
   unsigned char* client_proof_calc = NULL;
   size_t client_proof_calc_length = 0;
   unsigned char* server_signature_calc = NULL;
   size_t server_signature_calc_length = 0;
   char* base64_server_signature_calc = NULL;
   size_t base64_server_signature_calc_length;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_final = NULL;

   pgexporter_log_debug("client_scram256 %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgexporter_write_auth_scram256(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgexporter_socket_is_nonblocking(client_fd);
   pgexporter_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgexporter_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgexporter_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            SLEEP_AND_GOTO(100000000L, retry);
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgexporter_socket_nonblocking(client_fd, false);
   }

   client_first_message_bare = malloc(msg->length - 25);
   memset(client_first_message_bare, 0, msg->length - 25);
   memcpy(client_first_message_bare, msg->data + 26, msg->length - 26);

   get_scram_attribute('r', (char*)msg->data + 26, msg->length - 26, &client_nounce);
   generate_nounce(&server_nounce);
   generate_salt(&salt, &salt_length);
   pgexporter_base64_encode(salt, salt_length, &base64_salt, &base64_salt_length);

   server_first_message = malloc(89);
   memset(server_first_message, 0, 89);
   snprintf(server_first_message, 89, "r=%s%s,s=%s,i=4096", client_nounce, server_nounce, base64_salt);

   status = pgexporter_create_auth_scram256_continue(client_nounce, server_nounce, base64_salt, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgexporter_copy_message(msg);

   pgexporter_free_message(msg);
   msg = NULL;

   status = pgexporter_write_message(c_ssl, client_fd, sasl_continue);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_scram_attribute('p', (char*)msg->data + 5, msg->length - 5, &base64_client_proof);
   pgexporter_base64_decode(base64_client_proof, strlen(base64_client_proof), (void**)&client_proof_received, &client_proof_received_length);

   client_final_message_without_proof = malloc(58);
   memset(client_final_message_without_proof, 0, 58);
   memcpy(client_final_message_without_proof, msg->data + 5, 57);

   sasl_prep(password, &password_prep);

   if (client_proof(password_prep, salt, salt_length, 4096,
                    client_first_message_bare, strlen(client_first_message_bare),
                    server_first_message, strlen(server_first_message),
                    client_final_message_without_proof, strlen(client_final_message_without_proof),
                    &client_proof_calc, &client_proof_calc_length))
   {
      goto error;
   }

   if (client_proof_received_length != client_proof_calc_length ||
       memcmp(client_proof_received, client_proof_calc, client_proof_calc_length) != 0)
   {
      goto bad_password;
   }

   if (server_signature(password_prep, salt, salt_length, 4096,
                        NULL, 0,
                        client_first_message_bare, strlen(client_first_message_bare),
                        server_first_message, strlen(server_first_message),
                        client_final_message_without_proof, strlen(client_final_message_without_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   pgexporter_base64_encode((char*)server_signature_calc, server_signature_calc_length, &base64_server_signature_calc, &base64_server_signature_calc_length);

   status = pgexporter_create_auth_scram256_final(base64_server_signature_calc, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_final = pgexporter_copy_message(msg);

   pgexporter_free_message(msg);
   msg = NULL;

   status = pgexporter_write_message(c_ssl, client_fd, sasl_final);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgexporter_log_debug("client_scram256 done");

   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:
   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:
   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_final);

   return AUTH_ERROR;
}

int
pgexporter_server_authenticate(int server, char* database, char* username, char* password, SSL** ssl, int* fd)
{
   int server_fd;
   int auth_type;
   int ret;
   int status = AUTH_ERROR;
   int connect;
   SSL* c_ssl = NULL;
   struct message* ssl_msg = NULL;
   struct message* startup_msg = NULL;
   struct message* msg = NULL;
   struct configuration* config;

   *ssl = NULL;
   *fd = -1;

   auth_type = SECURITY_INVALID;
   server_fd = -1;
   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      memset(&security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   if (config->servers[server].host[0] == '/')
   {
      char pgsql[MISC_LENGTH];

      memset(&pgsql, 0, sizeof(pgsql));
      snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
      ret = pgexporter_connect_unix_socket(config->servers[server].host, &pgsql[0], &server_fd);
   }
   else
   {
      ret = pgexporter_connect(config->servers[server].host, config->servers[server].port, &server_fd);
   }

   if (ret != 0)
   {
      goto error;
   }

   ret = pgexporter_create_ssl_message(&ssl_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgexporter_write_message(NULL, server_fd, ssl_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgexporter_read_block_message(NULL, server_fd, &msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (msg->kind == 'S')
   {
      SSL_CTX* ctx = NULL;

      if (pgexporter_create_ssl_ctx(true, &ctx))
      {
         goto error;
      }

      pgexporter_log_trace("%s: Key file @ %s", config->servers[server].name, config->servers[server].tls_key_file);
      pgexporter_log_trace("%s: Certificate file @ %s", config->servers[server].name, config->servers[server].tls_cert_file);
      pgexporter_log_trace("%s: CA file @ %s", config->servers[server].name, config->servers[server].tls_ca_file);

      if (create_ssl_client(ctx, config->servers[server].tls_key_file, config->servers[server].tls_cert_file, config->servers[server].tls_ca_file, server_fd, &c_ssl))
      {
         goto error;
      }

      do
      {
         connect = SSL_connect(c_ssl);

         if (connect != 1)
         {
            int err = SSL_get_error(c_ssl, connect);
            switch (err)
            {
               case SSL_ERROR_ZERO_RETURN:
               case SSL_ERROR_WANT_READ:
               case SSL_ERROR_WANT_WRITE:
               case SSL_ERROR_WANT_CONNECT:
               case SSL_ERROR_WANT_ACCEPT:
               case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
               case SSL_ERROR_WANT_ASYNC:
               case SSL_ERROR_WANT_ASYNC_JOB:
               case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
                  break;
               case SSL_ERROR_SYSCALL:
                  pgexporter_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), server_fd);
                  errno = 0;
                  goto error;
                  break;
               case SSL_ERROR_SSL:
                  pgexporter_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), server_fd);
                  pgexporter_log_error("%s", ERR_error_string(err, NULL));
                  pgexporter_log_error("%s", ERR_lib_error_string(err));
                  pgexporter_log_error("%s", ERR_reason_error_string(err));
                  errno = 0;
                  goto error;
                  break;
            }
            ERR_clear_error();
         }
      }
      while (connect != 1);
   }

   ret = pgexporter_create_startup_message(username, database, &startup_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgexporter_write_message(c_ssl, server_fd, startup_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgexporter_read_block_message(c_ssl, server_fd, &msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_auth_type(msg, &auth_type);

   if (auth_type == -1)
   {
      goto error;
   }
   else if (auth_type != SECURITY_TRUST && auth_type != SECURITY_PASSWORD && auth_type != SECURITY_MD5 && auth_type != SECURITY_SCRAM256)
   {
      goto error;
   }

   security_lengths[0] = msg->length;
   memcpy(&security_messages[0], msg->data, msg->length);

   if (auth_type == SECURITY_TRUST)
   {
      status = server_trust();
   }
   else if (auth_type == SECURITY_PASSWORD)
   {
      status = server_password(username, password, c_ssl, server_fd);
   }
   else if (auth_type == SECURITY_MD5)
   {
      status = server_md5(username, password, c_ssl, server_fd);
   }
   else if (auth_type == SECURITY_SCRAM256)
   {
      status = server_scram256(username, password, c_ssl, server_fd);
   }

   if (status == AUTH_BAD_PASSWORD)
   {
      goto bad_password;
   }
   else if (status == AUTH_ERROR)
   {
      goto error;
   }

   *ssl = c_ssl;
   *fd = server_fd;

   pgexporter_free_message(ssl_msg);
   pgexporter_free_message(startup_msg);
   pgexporter_clear_message();

   return AUTH_SUCCESS;

bad_password:

   pgexporter_free_message(ssl_msg);
   pgexporter_free_message(startup_msg);
   pgexporter_clear_message();

   pgexporter_close_ssl(c_ssl);
   if (server_fd != -1)
   {
      pgexporter_disconnect(server_fd);
   }

   return AUTH_BAD_PASSWORD;

error:

   pgexporter_free_message(ssl_msg);
   pgexporter_free_message(startup_msg);
   pgexporter_clear_message();

   pgexporter_close_ssl(c_ssl);
   if (server_fd != -1)
   {
      pgexporter_disconnect(server_fd);
   }

   return AUTH_ERROR;
}

void
pgexporter_close_ssl(SSL* ssl)
{
   int res;
   SSL_CTX* ctx;

   if (ssl != NULL)
   {
      ctx = SSL_get_SSL_CTX(ssl);
      res = SSL_shutdown(ssl);
      if (res == 0)
      {
         SSL_shutdown(ssl);
      }
      SSL_free(ssl);
      SSL_CTX_free(ctx);
   }
}

static int
server_trust(void)
{
   pgexporter_log_trace("server_trust");

   has_security = SECURITY_TRUST;

   return AUTH_SUCCESS;
}

static int
server_password(char* username, char* password, SSL* ssl, int server_fd)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int auth_response = -1;
   struct message* auth_msg = NULL;
   struct message* password_msg = NULL;

   pgexporter_log_trace("server_password");

   status = pgexporter_create_auth_password_response(password, &password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_write_message(ssl, server_fd, password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = password_msg->length;
   memcpy(&security_messages[auth_index], password_msg->data, password_msg->length);
   auth_index++;

   status = pgexporter_read_block_message(ssl, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgexporter_log_message(auth_msg);
      pgexporter_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgexporter_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgexporter_log_message(auth_msg);
         pgexporter_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }

      security_lengths[auth_index] = auth_msg->length;
      memcpy(&security_messages[auth_index], auth_msg->data, auth_msg->length);

      has_security = SECURITY_PASSWORD;
   }
   else
   {
      goto bad_password;
   }

   pgexporter_free_message(password_msg);
   pgexporter_clear_message();

   return AUTH_SUCCESS;

bad_password:

   pgexporter_log_warn("Wrong password for user: %s", username);

   pgexporter_free_message(password_msg);
   pgexporter_clear_message();

   return AUTH_BAD_PASSWORD;

error:

   pgexporter_free_message(password_msg);
   pgexporter_clear_message();

   return AUTH_ERROR;
}

static int
server_md5(char* username, char* password, SSL* ssl, int server_fd)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int auth_response = -1;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   char md5str[36];
   char* salt = NULL;
   struct message* auth_msg = NULL;
   struct message* md5_msg = NULL;

   pgexporter_log_trace("server_md5");

   if (get_salt(security_messages[0], &salt))
   {
      goto error;
   }

   size = strlen(username) + strlen(password) + 1;
   pwdusr = malloc(size);
   memset(pwdusr, 0, size);

   snprintf(pwdusr, size, "%s%s", password, username);

   if (generate_md5(pwdusr, strlen(pwdusr), &shadow))
   {
      goto error;
   }

   md5_req = malloc(36);
   memset(md5_req, 0, 36);
   memcpy(md5_req, shadow, 32);
   memcpy(md5_req + 32, salt, 4);

   if (generate_md5(md5_req, 36, &md5))
   {
      goto error;
   }

   memset(&md5str, 0, sizeof(md5str));
   snprintf(&md5str[0], 36, "md5%s", md5);

   status = pgexporter_create_auth_md5_response(md5str, &md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_write_message(ssl, server_fd, md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = md5_msg->length;
   memcpy(&security_messages[auth_index], md5_msg->data, md5_msg->length);
   auth_index++;

   status = pgexporter_read_block_message(ssl, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgexporter_log_message(auth_msg);
      pgexporter_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgexporter_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgexporter_log_message(auth_msg);
         pgexporter_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }

      security_lengths[auth_index] = auth_msg->length;
      memcpy(&security_messages[auth_index], auth_msg->data, auth_msg->length);

      has_security = SECURITY_MD5;
   }
   else
   {
      goto bad_password;
   }

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgexporter_free_message(md5_msg);
   pgexporter_clear_message();

   return AUTH_SUCCESS;

bad_password:

   pgexporter_log_warn("Wrong password for user: %s", username);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgexporter_free_message(md5_msg);
   pgexporter_clear_message();

   return AUTH_BAD_PASSWORD;

error:

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgexporter_free_message(md5_msg);
   pgexporter_clear_message();

   return AUTH_ERROR;
}

static int
server_scram256(char* username, char* password, SSL* ssl, int server_fd)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   char* salt = NULL;
   size_t salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   size_t proof_length;
   char* proof_base = NULL;
   size_t proof_base_length;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   size_t server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   size_t server_signature_calc_length;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;

   pgexporter_log_trace("server_scram256");

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgexporter_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = sasl_response->length;
   memcpy(&security_messages[auth_index], sasl_response->data, sasl_response->length);
   auth_index++;

   status = pgexporter_write_message(ssl, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_read_block_message(ssl, server_fd, &msg);
   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgexporter_log_message(msg);
      pgexporter_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   sasl_continue = pgexporter_copy_message(msg);

   security_lengths[auth_index] = sasl_continue->length;
   memcpy(&security_messages[auth_index], sasl_continue->data, sasl_continue->length);
   auth_index++;

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      pgexporter_log_error("SCRAM-SHA-256: %s", err);
      goto error;
   }

   pgexporter_base64_decode(base64_salt, strlen(base64_salt), (void**)&salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = security_messages[1] + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = security_messages[2] + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, security_lengths[1] - 26,
                    server_first_message, security_lengths[2] - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgexporter_base64_encode((char*)proof, proof_length, &proof_base, &proof_base_length);

   status = pgexporter_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = sasl_continue_response->length;
   memcpy(&security_messages[auth_index], sasl_continue_response->data, sasl_continue_response->length);
   auth_index++;

   status = pgexporter_write_message(ssl, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_read_block_message(ssl, server_fd, &msg);
   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgexporter_log_message(msg);
      pgexporter_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   security_lengths[auth_index] = msg->length;
   memcpy(&security_messages[auth_index], msg->data, msg->length);
   auth_index++;

   if (pgexporter_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgexporter_base64_decode(base64_server_signature, sasl_final->length - 11,
                            (void**)&server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, security_lengths[1] - 26,
                        server_first_message, security_lengths[2] - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   has_security = SECURITY_SCRAM256;

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgexporter_free_message(sasl_response);
   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_continue_response);
   pgexporter_free_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:

   pgexporter_log_warn("Wrong password for user: %s", username);

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgexporter_free_message(sasl_response);
   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_continue_response);
   pgexporter_free_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgexporter_free_message(sasl_response);
   pgexporter_free_message(sasl_continue);
   pgexporter_free_message(sasl_continue_response);
   pgexporter_free_message(sasl_final);

   return AUTH_ERROR;
}

static char*
get_admin_password(char* username)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_admins; i++)
   {
      if (!strcmp(&config->admins[i].username[0], username))
      {
         return &config->admins[i].password[0];
      }
   }

   return NULL;
}

int
pgexporter_get_master_key(char** masterkey)
{
   FILE* master_key_file = NULL;
   char buf[MISC_LENGTH];
   char line[MISC_LENGTH];
   char* mk = NULL;
   size_t mk_length = 0;
   struct stat st = {0};

   if (pgexporter_get_home_directory() == NULL)
   {
      goto error;
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgexporter", pgexporter_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      goto error;
   }
   else
   {
      if (S_ISDIR(st.st_mode) && st.st_mode & S_IRWXU && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         goto error;
      }
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgexporter/master.key", pgexporter_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      goto error;
   }
   else
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         goto error;
      }
   }

   master_key_file = fopen(&buf[0], "r");
   if (master_key_file == NULL)
   {
      goto error;
   }

   memset(&line, 0, sizeof(line));
   if (fgets(line, sizeof(line), master_key_file) == NULL)
   {
      goto error;
   }

   pgexporter_base64_decode(&line[0], strlen(&line[0]), (void**)&mk, &mk_length);

   *masterkey = mk;

   fclose(master_key_file);

   return 0;

error:

   free(mk);

   if (master_key_file)
   {
      fclose(master_key_file);
   }

   return 1;
}

int
pgexporter_tls_valid(void)
{
   struct configuration* config;
   struct stat st = {0};

   config = (struct configuration*)shmem;

   if (config->tls)
   {
      if (strlen(config->tls_cert_file) == 0)
      {
         pgexporter_log_error("No TLS certificate defined");
         goto error;
      }

      if (strlen(config->tls_key_file) == 0)
      {
         pgexporter_log_error("No TLS private key defined");
         goto error;
      }

      if (stat(config->tls_cert_file, &st) == -1)
      {
         pgexporter_log_error("Can't locate TLS certificate file: %s", config->tls_cert_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgexporter_log_error("TLS certificate file is not a regular file: %s", config->tls_cert_file);
         goto error;
      }

      if (st.st_uid && st.st_uid != geteuid())
      {
         pgexporter_log_error("TLS certificate file not owned by user or root: %s", config->tls_cert_file);
         goto error;
      }

      memset(&st, 0, sizeof(struct stat));

      if (stat(config->tls_key_file, &st) == -1)
      {
         pgexporter_log_error("Can't locate TLS private key file: %s", config->tls_key_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgexporter_log_error("TLS private key file is not a regular file: %s", config->tls_key_file);
         goto error;
      }

      if (st.st_uid == geteuid())
      {
         if (st.st_mode & (S_IRWXG | S_IRWXO))
         {
            pgexporter_log_error("TLS private key file must have 0600 permissions when owned by a non-root user: %s", config->tls_key_file);
            goto error;
         }
      }
      else if (st.st_uid == 0)
      {
         if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO))
         {
            pgexporter_log_error("TLS private key file must have at least 0640 permissions when owned by root: %s", config->tls_key_file);
            goto error;
         }

      }
      else
      {
         pgexporter_log_error("TLS private key file not owned by user or root: %s", config->tls_key_file);
         goto error;
      }

      if (strlen(config->tls_ca_file) > 0)
      {
         memset(&st, 0, sizeof(struct stat));

         if (stat(config->tls_ca_file, &st) == -1)
         {
            pgexporter_log_error("Can't locate TLS CA file: %s", config->tls_ca_file);
            goto error;
         }

         if (!S_ISREG(st.st_mode))
         {
            pgexporter_log_error("TLS CA file is not a regular file: %s", config->tls_ca_file);
            goto error;
         }

         if (st.st_uid && st.st_uid != geteuid())
         {
            pgexporter_log_error("TLS CA file not owned by user or root: %s", config->tls_ca_file);
            goto error;
         }
      }
      else
      {
         pgexporter_log_debug("No TLS CA file");
      }
   }

   return 0;

error:

   return 1;
}

static int
sasl_prep(char* password, char** password_prep)
{
   char* p = NULL;

   /* Only support ASCII for now */
   for (size_t i = 0; i < strlen(password); i++)
   {
      if ((unsigned char)(*(password + i)) & 0x80)
      {
         goto error;
      }
   }

   p = strdup(password);

   *password_prep = p;

   return 0;

error:

   *password_prep = NULL;

   return 1;
}

static int
generate_nounce(char** nounce)
{
   size_t s = 18;
   unsigned char r[s + 1];
   char* base = NULL;
   size_t base_length;
   int result;

   memset(&r[0], 0, sizeof(r));

   result = RAND_bytes(r, sizeof(r));
   if (result != 1)
   {
      goto error;
   }

   r[s] = '\0';

   pgexporter_base64_encode((char*)&r[0], s, &base, &base_length);

   *nounce = base;

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 0;

error:

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 1;
}

static int
get_scram_attribute(char attribute, char* input, size_t size, char** value)
{
   char* dup = NULL;
   char* result = NULL;
   char* ptr = NULL;
   size_t token_size;
   char match[2];

   match[0] = attribute;
   match[1] = '=';

   dup = (char*)malloc(size + 1);
   memset(dup, 0, size + 1);
   memcpy(dup, input, size);

   ptr = strtok(dup, ",");
   while (ptr != NULL)
   {
      if (!strncmp(ptr, &match[0], 2))
      {
         token_size = strlen(ptr) - 1;
         result = malloc(token_size);
         memset(result, 0, token_size);
         memcpy(result, ptr + 2, token_size);
         goto done;
      }

      ptr = strtok(NULL, ",");
   }

   if (result == NULL)
   {
      goto error;
   }

done:

   *value = result;

   free(dup);

   return 0;

error:

   *value = NULL;

   free(dup);

   return 1;
}

static int
client_proof(char* password, char* salt, int salt_length, int iterations,
             char* client_first_message_bare, size_t client_first_message_bare_length,
             char* server_first_message, size_t server_first_message_length,
             char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
             unsigned char** result, size_t* result_length)
{
   size_t size = 32;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* c_k = NULL;
   int c_k_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned char* c_s = NULL;
   unsigned int length;
   unsigned char* r = NULL;
   HMAC_CTX* ctx = HMAC_CTX_new();

   if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
   {
      goto error;
   }

   if (salted_password_key(s_p, s_p_length, "Client Key", &c_k, &c_k_length))
   {
      goto error;
   }

   if (stored_key(c_k, c_k_length, &s_k, &s_k_length))
   {
      goto error;
   }

   c_s = malloc(size);
   memset(c_s, 0, size);

   r = malloc(size);
   memset(r, 0, size);

   /* Client signature: HMAC(StoredKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, c_s, &length) != 1)
   {
      goto error;
   }

   /* ClientProof: ClientKey XOR ClientSignature */
   for (size_t i = 0; i < size; i++)
   {
      *(r + i) = *(c_k + i) ^ *(c_s + i);
   }

   *result = r;
   *result_length = size;

   HMAC_CTX_free(ctx);

   free(s_p);
   free(c_k);
   free(s_k);
   free(c_s);

   return 0;

error:

   *result = NULL;
   *result_length = 0;

   if (ctx != NULL)
   {
      HMAC_CTX_free(ctx);
   }

   free(s_p);
   free(c_k);
   free(s_k);
   free(c_s);

   return 1;
}

static int
salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length)
{
   size_t size = 32;
   int password_length;
   unsigned int one;
   unsigned char Ui[size];
   unsigned char Ui_prev[size];
   unsigned int Ui_length;
   unsigned char* r = NULL;
   HMAC_CTX* ctx = HMAC_CTX_new();

   if (ctx == NULL)
   {
      goto error;
   }

   password_length = strlen(password);

   if (!pgexporter_bigendian())
   {
      one = pgexporter_swap(1);
   }
   else
   {
      one = 1;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* SaltedPassword: Hi(Normalize(password), salt, iterations) */
   if (HMAC_Init_ex(ctx, password, password_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)salt, salt_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)&one, sizeof(one)) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, &Ui_prev[0], &Ui_length) != 1)
   {
      goto error;
   }
   memcpy(r, &Ui_prev[0], size);

   for (int i = 2; i <= iterations; i++)
   {
      if (HMAC_CTX_reset(ctx) != 1)
      {
         goto error;
      }

      if (HMAC_Init_ex(ctx, password, password_length, EVP_sha256(), NULL) != 1)
      {
         goto error;
      }

      if (HMAC_Update(ctx, &Ui_prev[0], size) != 1)
      {
         goto error;
      }

      if (HMAC_Final(ctx, &Ui[0], &Ui_length) != 1)
      {
         goto error;
      }

      for (size_t j = 0; j < size; j++)
      {
         *(r + j) ^= *(Ui + j);
      }
      memcpy(&Ui_prev[0], &Ui[0], size);
   }

   *result = r;
   *result_length = size;

   HMAC_CTX_free(ctx);

   return 0;

error:

   if (ctx != NULL)
   {
      HMAC_CTX_free(ctx);
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
salted_password_key(unsigned char* salted_password, int salted_password_length, char* key, unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned int length;
   HMAC_CTX* ctx = HMAC_CTX_new();

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* HMAC(SaltedPassword, Key) */
   if (HMAC_Init_ex(ctx, salted_password, salted_password_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)key, strlen(key)) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = size;

   HMAC_CTX_free(ctx);

   return 0;

error:

   if (ctx != NULL)
   {
      HMAC_CTX_free(ctx);
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned int length;
   EVP_MD_CTX* ctx = EVP_MD_CTX_new();

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* StoredKey: H(ClientKey) */
   if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (EVP_DigestUpdate(ctx, client_key, client_key_length) != 1)
   {
      goto error;
   }

   if (EVP_DigestFinal_ex(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = size;

   EVP_MD_CTX_free(ctx);

   return 0;

error:

   if (ctx != NULL)
   {
      EVP_MD_CTX_free(ctx);
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
generate_salt(char** salt, int* size)
{
   size_t s = 16;
   unsigned char* r = NULL;
   int result;

   r = malloc(s);
   memset(r, 0, s);

   result = RAND_bytes(r, s);
   if (result != 1)
   {
      goto error;
   }

   *salt = (char*)r;
   *size = s;

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 0;

error:

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   free(r);

   *salt = NULL;
   *size = 0;

   return 1;
}

static int
server_signature(char* password, char* salt, int salt_length, int iterations,
                 char* s_key, int s_key_length,
                 char* client_first_message_bare, size_t client_first_message_bare_length,
                 char* server_first_message, size_t server_first_message_length,
                 char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                 unsigned char** result, size_t* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned int length;
   bool do_free = true;
   HMAC_CTX* ctx = HMAC_CTX_new();

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   if (password != NULL)
   {
      if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
      {
         goto error;
      }

      if (salted_password_key(s_p, s_p_length, "Server Key", &s_k, &s_k_length))
      {
         goto error;
      }
   }
   else
   {
      do_free = false;
      s_k = (unsigned char*)s_key;
      s_k_length = s_key_length;
   }

   /* Server signature: HMAC(ServerKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = length;

   HMAC_CTX_free(ctx);

   free(s_p);
   if (do_free)
   {
      free(s_k);
   }

   return 0;

error:

   *result = NULL;
   *result_length = 0;

   if (ctx != NULL)
   {
      HMAC_CTX_free(ctx);
   }

   free(s_p);
   if (do_free)
   {
      free(s_k);
   }

   return 1;
}

int
pgexporter_create_ssl_ctx(bool client, SSL_CTX** ctx)
{
   SSL_CTX* c = NULL;

   if (client)
   {
      c = SSL_CTX_new(TLS_client_method());
   }
   else
   {
      c = SSL_CTX_new(TLS_server_method());
   }

   if (c == NULL)
   {
      goto error;
   }

   if (SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION) == 0)
   {
      goto error;
   }

   SSL_CTX_set_mode(c, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
   SSL_CTX_set_options(c, SSL_OP_NO_TICKET);
   SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_OFF);

   *ctx = c;

   return 0;

error:

   if (c != NULL)
   {
      SSL_CTX_free(c);
   }

   return 1;
}

static int
create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl)
{
   SSL* s = NULL;
   bool have_cert = false;
   bool have_rootcert = false;

   if (root != NULL && strlen(root) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, root, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgexporter_log_error("Couldn't load TLS CA: %s", root);
         pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_rootcert = true;
   }

   if (cert != NULL && strlen(cert) > 0)
   {
      if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgexporter_log_error("Couldn't load TLS certificate: %s", cert);
         pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_cert = true;
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   if (have_cert && key != NULL && strlen(key) > 0)
   {
      if (SSL_use_PrivateKey_file(s, key, SSL_FILETYPE_PEM) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgexporter_log_error("Couldn't load TLS private key: %s", key);
         pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      if (SSL_check_private_key(s) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgexporter_log_error("TLS private key check failed: %s", key);
         pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }
   }

   if (have_rootcert)
   {
      SSL_set_verify(s, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
   }

   *ssl = s;

   return 0;

error:

   pgexporter_close_ssl(s);

   return 1;
}

int
pgexporter_create_ssl_server(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl)
{
   SSL* s = NULL;
   STACK_OF(X509_NAME) * root_cert_list = NULL;

   if (strlen(cert) == 0)
   {
      pgexporter_log_error("No TLS certificate defined");
      goto error;
   }

   if (strlen(key) == 0)
   {
      pgexporter_log_error("No TLS private key defined");
      goto error;
   }

   if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgexporter_log_error("Couldn't load TLS certificate: %s", cert);
      pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgexporter_log_error("Couldn't load TLS private key: %s", key);
      pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_check_private_key(ctx) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgexporter_log_error("TLS private key check failed: %s", key);
      pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (strlen(root) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, root, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgexporter_log_error("Couldn't load TLS CA: %s", root);
         pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      root_cert_list = SSL_load_client_CA_file(root);
      if (root_cert_list == NULL)
      {
         unsigned long err;

         err = ERR_get_error();
         pgexporter_log_error("Couldn't load TLS CA: %s", root);
         pgexporter_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      SSL_CTX_set_verify(ctx, (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE), NULL);
      SSL_CTX_set_client_CA_list(ctx, root_cert_list);
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   *ssl = s;

   return 0;

error:

   pgexporter_close_ssl(s);

   return 1;
}

int
pgexporter_extract_server_parameters(struct deque** server_parameters)
{
   int i;
   char* data = NULL;
   ssize_t data_length;
   size_t offset;
   char* name = NULL;
   char* value = NULL;
   struct message* msg = NULL;
   struct deque* sp = NULL;
   *server_parameters = NULL;

   if (pgexporter_deque_create(false, &sp))
   {
      return 1;
   }

   for (i = 0; i < NUMBER_OF_SECURITY_MESSAGES; ++i)
   {
      if ((data_length = security_lengths[i]) > 0)
      {
         data = &security_messages[i][0];
         offset = 0;

         while (offset < (size_t) data_length)
         {
            offset = pgexporter_extract_message_offset(offset, data, &msg);
            if (msg->kind == 'S')
            {
               name = pgexporter_read_string(msg->data + 5); // 1 byte for kind + 4 bytes for length
               value = pgexporter_read_string(msg->data + strlen(name) + 6);
               pgexporter_deque_add(sp, name, (uintptr_t)value, ValueString);
            }
            pgexporter_free_message(msg);
         }
      }
   }

   *server_parameters = sp;
   return 0;
}
