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

#ifndef PGEXPORTER_SECURITY_H
#define PGEXPORTER_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <deque.h>

#include <stdlib.h>

#include <openssl/ssl.h>

/**
 * Authenticate a user
 * @param server The server
 * @param database The database
 * @param username The username
 * @param password The password
 * @param ssl The resulting SSL structure
 * @param fd The resulting socket
 * @return AUTH_SUCCESS, AUTH_BAD_PASSWORD or AUTH_ERROR
 */
int
pgexporter_server_authenticate(int server, char* database, char* username, char* password, SSL** ssl, int* fd);

/**
 * Authenticate a remote management user
 * @param client_fd The descriptor
 * @param address The client address
 * @param client_ssl The client SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_remote_management_auth(int client_fd, char* address, SSL** client_ssl);

/**
 * Connect using SCRAM-SHA256
 * @param username The user name
 * @param password The password
 * @param server_fd The descriptor
 * @param s_ssl The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl);

/**
 * Get the master key
 * @param masterkey The master key
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_get_master_key(char** masterkey);

/**
 * Is the TLS configuration valid
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_tls_valid(void);

/**
 * Close a SSL structure
 * @param ssl The SSL structure
 */
void
pgexporter_close_ssl(SSL* ssl);

/**
 * Extract server parameters recevied during the latest authentication
 * @param server_parameters The resulting non-thread-safe deque
 * @return 0 on success, otherwise 1
 */
int
pgexporter_extract_server_parameters(struct deque** server_parameters);

/**
 * Create a SSL context
 * @param client True if client, false if server
 * @param ctx The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_ssl_ctx(bool client, SSL_CTX** ctx);

/**
 * Create a SSL server
 * @param ctx The SSL context
 * @param key The key file path
 * @param cert The certificate file path
 * @param root The root file path
 * @param socket The socket
 * @param ssl The SSL structure
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_create_ssl_server(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl);

#ifdef __cplusplus
}
#endif

#endif
