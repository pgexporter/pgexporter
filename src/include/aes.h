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

#ifndef PGEXPORTER_AES_H
#define PGEXPORTER_AES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <json.h>

#include <openssl/ssl.h>

#define PBKDF2_ITERATIONS  600000
#define PBKDF2_SALT_LENGTH 16
#define GCM_TAG_LENGTH     16

/**
 * Encrypt a string
 * @param plaintext The string
 * @param password The master password
 * @param ciphertext The ciphertext output
 * @param ciphertext_length The length of the ciphertext
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 * @note A master salt must be set before calling this function (via pgexporter_set_master_salt() or implicitly).
 */
int
pgexporter_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length, int mode);

/**
 * Decrypt a string
 * @param ciphertext The string
 * @param ciphertext_length The length of the ciphertext
 * @param password The master password
 * @param plaintext The plaintext output
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 * @note A master salt must be set before calling this function (via pgexporter_set_master_salt() or implicitly).
 */
int
pgexporter_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode);

/**
 *
 * Encrypt a buffer
 * @param origin_buffer The original buffer
 * @param origin_size The size of the buffer
 * @param enc_buffer The result buffer
 * @param enc_size The result buffer size
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_encrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** enc_buffer, size_t* enc_size, int mode);

/**
 *
 * Encrypt a buffer with a password
 * @param origin_buffer The original buffer
 * @param origin_size The size of the buffer
 * @param password The password
 * @param enc_buffer The result buffer
 * @param enc_size The result buffer size
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 * @note A master salt must be set before calling this function (via pgexporter_set_master_salt() or implicitly).
 */
int
pgexporter_encrypt_buffer_with_password(unsigned char* origin_buffer, size_t origin_size, char* password, unsigned char** enc_buffer, size_t* enc_size, int mode);

/**
 *
 * Decrypt a buffer
 * @param origin_buffer The original buffer
 * @param origin_size The size of the buffer
 * @param dec_buffer The result buffer
 * @param dec_size The result buffer size
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 */
int
pgexporter_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** dec_buffer, size_t* dec_size, int mode);

/**
 *
 * Decrypt a buffer with a password
 * @param origin_buffer The original buffer
 * @param origin_size The size of the buffer
 * @param password The password
 * @param dec_buffer The result buffer
 * @param dec_size The result buffer size
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 * @note A master salt must be set before calling this function (via pgexporter_set_master_salt() or implicitly).
 */
int
pgexporter_decrypt_buffer_with_password(unsigned char* origin_buffer, size_t origin_size, char* password, unsigned char** dec_buffer, size_t* dec_size, int mode);

/**
 * Set the master salt for the high-iteration KDF
 * @param salt The 16-byte salt
 */
void
pgexporter_set_master_salt(unsigned char* salt);

/**
 * Is GCM
 * @param mode The mode
 * @return True if GCM, otherwise false
 */
bool pgexporter_is_gcm(int mode);

/**
 * Clear the AES master key cache for the current thread from memory.
 * This function only affects the calling thread's thread-local cache and
 * does not clear AES master key caches in other threads.
 */
void pgexporter_clear_aes_cache(void);

/**
 * Encrypt a file
 * @param input The input file path
 * @param output The output file path
 * @param password The password
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 * @note A master salt must be set before calling this function (via pgexporter_set_master_salt() or implicitly).
 */
int pgexporter_encrypt_file(char* input, char* output, char* password, int mode);

/**
 * Decrypt a file
 * @param input The input file path
 * @param output The output file path
 * @param password The password
 * @param mode The aes mode
 * @return 0 upon success, otherwise 1
 * @note A master salt must be set before calling this function (via pgexporter_set_master_salt() or implicitly).
 */
int pgexporter_decrypt_file(char* input, char* output, char* password, int mode);

#ifdef __cplusplus
}
#endif

#endif
