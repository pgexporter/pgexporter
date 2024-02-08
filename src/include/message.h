/*
 * Copyright (C) 2024 The pgexporter community
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

#ifndef PGEXPORTER_MESSAGE_H
#define PGEXPORTER_MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

#define MESSAGE_STATUS_ZERO  0
#define MESSAGE_STATUS_OK    1
#define MESSAGE_STATUS_ERROR 2

/** @struct
 * Defines a message
 */
typedef struct
{
   signed char kind;  /**< The kind of the message */
   ssize_t length;    /**< The length of the message */
   size_t max_length; /**< The maximum size of the message */
   void* data;        /**< The message data */
} __attribute__ ((aligned (64))) message_t;

/**
 * Read a message in blocking mode
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgexporter_read_block_message(SSL* ssl, int socket, message_t** msg);

/**
 * Read a message with a timeout
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param timeout The timeout in seconds
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgexporter_read_timeout_message(SSL* ssl, int socket, int timeout, message_t** msg);

/**
 * Write a message using a socket
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgexporter_write_message(SSL* ssl, int socket, message_t* msg);

/**
 * Free a message
 * @param msg The resulting message
 */
void
pgexporter_free_message(message_t* msg);

/**
 * Copy a message
 * @param msg The resulting message
 * @return The copy
 */
message_t*
pgexporter_copy_message(message_t* msg);

/**
 * Free a copy message
 * @param msg The resulting message
 */
void
pgexporter_free_copy_message(message_t* msg);

/**
 * Is the connection valid
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return true upon success, otherwise false
 */
bool
pgexporter_connection_isvalid(SSL* ssl, int socket);

/**
 * Log a message
 * @param msg The message
 */
void
pgexporter_log_message(message_t* msg);

/**
 * Write a notice message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_notice(SSL* ssl, int socket);

/**
 * Write a terminate message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_terminate(SSL* ssl, int socket);

/**
 * Write an empty message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_empty(SSL* ssl, int socket);

/**
 * Write a connection refused message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_connection_refused(SSL* ssl, int socket);

/**
 * Write a connection refused message (protocol 1 or 2)
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_connection_refused_old(SSL* ssl, int socket);

/**
 * Write TLS response
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_tls(SSL* ssl, int socket);

/**
 * Create an auth password response message
 * @param password The password
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_auth_password_response(char* password, message_t** msg);

/**
 * Create an auth MD5 response message
 * @param md5 The md5
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_auth_md5_response(char* md5, message_t** msg);

/**
 * Write an auth SCRAM-SHA-256 message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_auth_scram256(SSL* ssl, int socket);

/**
 * Create an auth SCRAM-SHA-256 response message
 * @param nounce The nounce
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_auth_scram256_response(char* nounce, message_t** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue message
 * @param cn The client nounce
 * @param sn The server nounce
 * @param salt The salt
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_auth_scram256_continue(char* cn, char* sn, char* salt, message_t** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue response message
 * @param wp The without proff
 * @param p The proff
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_auth_scram256_continue_response(char* wp, char* p, message_t** msg);

/**
 * Create an auth SCRAM-SHA-256/Final message
 * @param ss The server signature (BASE64)
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_auth_scram256_final(char* ss, message_t** msg);

/**
 * Write an auth success message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_write_auth_success(SSL* ssl, int socket);

/**
 * Create a SSL message
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_ssl_message(message_t** msg);

/**
 * Create a startup message
 * @param username The user name
 * @param database The database
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_startup_message(char* username, char* database, message_t** msg);

#ifdef __cplusplus
}
#endif

#endif
