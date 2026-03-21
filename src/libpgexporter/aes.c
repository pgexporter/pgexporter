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

#include <pgexporter.h>
#include <aes.h>
#include <logging.h>
#include <security.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <limits.h>
#include <string.h>
#include <utils.h>

static int is_gcm(int mode);
static int get_tag_length(int mode);
static int derive_key_iv(char* password, unsigned char* salt, unsigned char* key, unsigned char* iv, int mode, unsigned char* master_salt);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode);
static const EVP_CIPHER* (*get_cipher(int mode))(void);
static int get_key_length(int mode);

#define AES_CHUNK_SIZE 4096

static int encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, char* password, unsigned char** res_buffer, size_t* res_size, int enc, int mode);

static bool master_salt_set = false;
static unsigned char master_salt[PBKDF2_SALT_LENGTH];

static _Thread_local unsigned char master_key_cache[EVP_MAX_KEY_LENGTH];
static _Thread_local unsigned char cached_master_salt[PBKDF2_SALT_LENGTH];
static _Thread_local unsigned char cached_password_hash[EVP_MAX_MD_SIZE];
static _Thread_local unsigned int cached_password_hash_len = 0;
static _Thread_local bool master_key_cached = false;

static void __attribute__((destructor)) pgexporter_aes_cache_destructor(void);

int
pgexporter_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length, int mode)
{
   if (plaintext == NULL || password == NULL || ciphertext == NULL || ciphertext_length == NULL)
   {
      return 1;
   }

   *ciphertext = NULL;
   *ciphertext_length = 0;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   char* encrypted = NULL;
   int encrypted_length = 0;
   char* output = NULL;
   int res = 1;

   int iv_len = 0;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (cipher_fp == NULL)
   {
      return 1;
   }
   iv_len = EVP_CIPHER_iv_length(cipher_fp());

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   /* Generate a cryptographically random salt and IV */
   if (RAND_bytes(salt, PBKDF2_SALT_LENGTH) != 1)
   {
      goto cleanup;
   }
   if (RAND_bytes(iv, iv_len) != 1)
   {
      goto cleanup;
   }

   if (derive_key_iv(password, salt, key, NULL, mode, NULL) != 0)
   {
      goto cleanup;
   }

   if (aes_encrypt(plaintext, key, iv, &encrypted, &encrypted_length, mode) != 0)
   {
      goto cleanup;
   }

   /* Format for GCM is: [salt][iv][ciphertext][tag] where encrypted = ciphertext||tag */
   output = malloc(PBKDF2_SALT_LENGTH + iv_len + encrypted_length);
   if (output == NULL)
   {
      goto cleanup;
   }

   memcpy(output, salt, PBKDF2_SALT_LENGTH);
   memcpy(output + PBKDF2_SALT_LENGTH, iv, iv_len);
   memcpy(output + PBKDF2_SALT_LENGTH + iv_len, encrypted, encrypted_length);

   *ciphertext = output;
   *ciphertext_length = PBKDF2_SALT_LENGTH + iv_len + encrypted_length;
   res = 0;

cleanup:
   if (encrypted != NULL)
   {
      free(encrypted);
   }

   /* Wipe key material from stack */
   pgexporter_cleanse(key, sizeof(key));
   pgexporter_cleanse(iv, sizeof(iv));

   return res;
}

int
pgexporter_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode)
{
   if (ciphertext == NULL || password == NULL || plaintext == NULL)
   {
      return 1;
   }

   *plaintext = NULL;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];

   int iv_len = 0;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (cipher_fp == NULL)
   {
      return 1;
   }
   iv_len = EVP_CIPHER_iv_length(cipher_fp());

   int tag_len = get_tag_length(mode);
   /* The ciphertext must be at least salt_length + iv_length + tag_length bytes */
   if (ciphertext_length < (int)(PBKDF2_SALT_LENGTH + iv_len + tag_len))
   {
      return 1;
   }

   int res = 1;

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   /* Extract salt and IV from the header */
   memcpy(salt, ciphertext, PBKDF2_SALT_LENGTH);
   memcpy(iv, ciphertext + PBKDF2_SALT_LENGTH, iv_len);

   if (derive_key_iv(password, salt, key, NULL, mode, NULL) != 0)
   {
      goto cleanup;
   }

   res = aes_decrypt(ciphertext + PBKDF2_SALT_LENGTH + iv_len,
                     ciphertext_length - (PBKDF2_SALT_LENGTH + iv_len),
                     key, iv, plaintext, mode);

cleanup:
   /* Wipe key material from stack */
   pgexporter_cleanse(key, sizeof(key));
   pgexporter_cleanse(iv, sizeof(iv));

   return res;
}

// [private]
static int
derive_key_iv(char* password, unsigned char* salt, unsigned char* key, unsigned char* iv, int mode, unsigned char* p_master_salt)
{
   if (password == NULL)
   {
      return 1;
   }

   size_t password_length = strlen(password);

   if (password_length > INT_MAX)
   {
      return 1;
   }
   int key_length;
   int iv_length;
   unsigned char derived[EVP_MAX_KEY_LENGTH + EVP_MAX_IV_LENGTH];
   unsigned char* ms = p_master_salt;

   key_length = get_key_length(mode);
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      return 1;
   }
   iv_length = EVP_CIPHER_iv_length(cipher_fp());

   /* Use global master salt if none provided */
   if (ms == NULL)
   {
      if (master_salt_set)
      {
         ms = master_salt;
      }
      else
      {
         pgexporter_log_error("Master salt required. Please generate a master key with a salt using 'pgexporter-admin master-key'");
         return 1;
      }
   }

   /* Step 1: Check if Master Key is already cached */
   unsigned char current_hash[EVP_MAX_MD_SIZE];
   unsigned int current_hash_len = 0;
   EVP_MD_CTX* hash_ctx = EVP_MD_CTX_new();
   if (hash_ctx == NULL ||
       EVP_DigestInit_ex(hash_ctx, EVP_sha256(), NULL) != 1 ||
       EVP_DigestUpdate(hash_ctx, password, password_length) != 1 ||
       EVP_DigestFinal_ex(hash_ctx, current_hash, &current_hash_len) != 1)
   {
      EVP_MD_CTX_free(hash_ctx);
      return 1;
   }
   EVP_MD_CTX_free(hash_ctx);

   if (!master_key_cached || current_hash_len != cached_password_hash_len || memcmp(cached_password_hash, current_hash, current_hash_len) != 0 ||
       memcmp(cached_master_salt, ms, PBKDF2_SALT_LENGTH) != 0)
   {
      if (!PKCS5_PBKDF2_HMAC(password, (int)password_length,
                             ms, PBKDF2_SALT_LENGTH,
                             PBKDF2_ITERATIONS,
                             EVP_sha256(),
                             EVP_MAX_KEY_LENGTH,
                             master_key_cache))
      {
         return 1;
      }
      memcpy(cached_password_hash, current_hash, current_hash_len);
      cached_password_hash_len = current_hash_len;
      memcpy(cached_master_salt, ms, PBKDF2_SALT_LENGTH);
      master_key_cached = true;
   }

   /* Step 2: Use Master Key to derive File Key fast (1 iteration) */
   if (!PKCS5_PBKDF2_HMAC((char*)master_key_cache, EVP_MAX_KEY_LENGTH,
                          salt, PBKDF2_SALT_LENGTH,
                          1,
                          EVP_sha256(),
                          key_length + iv_length,
                          derived))
   {
      return 1;
   }

   memcpy(key, derived, key_length);
   if (iv != NULL)
   {
      memcpy(iv, derived + key_length, iv_length);
   }

   /* Wipe sensitive derived material */
   pgexporter_cleanse(derived, sizeof(derived));

   return 0;
}

// [private]
static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int length;
   size_t size = 0;
   unsigned char* ct = NULL;
   int ct_length = 0;
   int ret = 0;
   int tag_len = get_tag_length(mode);
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (plaintext == NULL || key == NULL || iv == NULL || ciphertext == NULL || ciphertext_length == NULL)
   {
      return 1;
   }

   if (strlen(plaintext) > INT_MAX)
   {
      return 1;
   }

   *ciphertext = NULL;
   *ciphertext_length = 0;

   if (cipher_fp == NULL)
   {
      ret = 1;
      goto cleanup;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      ret = 1;
      goto cleanup;
   }

   if (EVP_EncryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   size = strlen(plaintext) + tag_len + EVP_CIPHER_block_size(cipher_fp());
   ct = malloc(size);

   if (ct == NULL)
   {
      ret = 1;
      goto cleanup;
   }

   memset(ct, 0, size);

   if (EVP_EncryptUpdate(ctx,
                         ct, &length,
                         (unsigned char*)plaintext, (int)strlen((char*)plaintext)) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   ct_length = length;

   if (EVP_EncryptFinal_ex(ctx, ct + length, &length) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   ct_length += length;

   if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, ct + ct_length) != 1)
   {
      ret = 1;
      goto cleanup;
   }
   ct_length += tag_len;

   *ciphertext = (char*)ct;
   *ciphertext_length = ct_length;

cleanup:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   if (ret != 0 && ct != NULL)
   {
      pgexporter_cleanse(ct, size);
      free(ct);
   }

   return ret;
}

// [private]
static int
aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int plaintext_length;
   int length;
   size_t size = 0;
   char* pt = NULL;
   int ret = 0;
   int tag_len = get_tag_length(mode);
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (ciphertext == NULL || key == NULL || iv == NULL || plaintext == NULL)
   {
      return 1;
   }

   *plaintext = NULL;

   if (cipher_fp == NULL)
   {
      return 1;
   }

   if (ciphertext_length < tag_len)
   {
      return 1;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      ret = 1;
      goto cleanup;
   }

   if (EVP_DecryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   size = ciphertext_length + EVP_CIPHER_block_size(cipher_fp());
   pt = malloc(size);

   if (pt == NULL)
   {
      ret = 1;
      goto cleanup;
   }

   memset(pt, 0, size);
   if (EVP_DecryptUpdate(ctx,
                         (unsigned char*)pt, &length,
                         (unsigned char*)ciphertext, ciphertext_length - tag_len) != 1)
   {
      ret = 1;
      goto cleanup;
   }
   plaintext_length = length;

   if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, ciphertext + ciphertext_length - tag_len) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   if (EVP_DecryptFinal_ex(ctx, (unsigned char*)pt + length, &length) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   plaintext_length += length;

   pt[plaintext_length] = 0;
   *plaintext = pt;

cleanup:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   if (ret != 0 && pt != NULL)
   {
      pgexporter_cleanse(pt, size);
      free(pt);
   }

   return ret;
}

static const EVP_CIPHER* (*get_cipher(int mode))(void)
{
   if (mode == ENCRYPTION_AES_256_GCM)
   {
      return &EVP_aes_256_gcm;
   }
   if (mode == ENCRYPTION_AES_192_GCM)
   {
      return &EVP_aes_192_gcm;
   }
   if (mode == ENCRYPTION_AES_128_GCM)
   {
      return &EVP_aes_128_gcm;
   }
   return NULL;
}

// [private]
static int
get_key_length(int mode)
{
   switch (mode)
   {
      case ENCRYPTION_AES_256_GCM:
         return 32;
      case ENCRYPTION_AES_192_GCM:
         return 24;
      case ENCRYPTION_AES_128_GCM:
         return 16;
      default:
         return 32;
   }
}

static int
is_gcm(int mode)
{
   switch (mode)
   {
      case ENCRYPTION_AES_256_GCM:
      case ENCRYPTION_AES_192_GCM:
      case ENCRYPTION_AES_128_GCM:
         return 1;
      default:
         return 0;
   }
}

bool
pgexporter_is_gcm(int mode)
{
   return is_gcm(mode) == 1;
}

static int
get_tag_length(int mode)
{
   if (is_gcm(mode))
   {
      return GCM_TAG_LENGTH;
   }

   return 0;
}

int
pgexporter_encrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** enc_buffer, size_t* enc_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, NULL, enc_buffer, enc_size, 1, mode);
}

int
pgexporter_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** dec_buffer, size_t* dec_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, NULL, dec_buffer, dec_size, 0, mode);
}

int
pgexporter_encrypt_buffer_with_password(unsigned char* origin_buffer, size_t origin_size, char* password, unsigned char** enc_buffer, size_t* enc_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, password, enc_buffer, enc_size, 1, mode);
}

int
pgexporter_decrypt_buffer_with_password(unsigned char* origin_buffer, size_t origin_size, char* password, unsigned char** dec_buffer, size_t* dec_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, password, dec_buffer, dec_size, 0, mode);
}

static int
encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, char* password, unsigned char** res_buffer, size_t* res_size, int enc, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   char* master_key = NULL;
   EVP_CIPHER_CTX* ctx = NULL;
   const EVP_CIPHER* (*cipher_fp)(void) = NULL;
   size_t cipher_block_size = 0;
   size_t outbuf_size = 0;
   size_t outl = 0;
   size_t f_len = 0;
   int outl_i = 0;
   int f_len_i = 0;
   unsigned char* actual_input = NULL;
   size_t actual_input_size = 0;
   unsigned char* out_buf = NULL;
   int ret = 0;
   int tag_len = get_tag_length(mode);

   int iv_len = 0;

   if (res_buffer == NULL || res_size == NULL)
   {
      return 1;
   }

   if (origin_size > 0 && origin_buffer == NULL)
   {
      return 1;
   }

   if (origin_size > INT_MAX)
   {
      return 1;
   }

   *res_buffer = NULL;
   *res_size = 0;

   cipher_fp = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      pgexporter_log_error("Invalid encryption method specified");
      ret = 1;
      goto cleanup;
   }

   iv_len = EVP_CIPHER_iv_length(cipher_fp());
   cipher_block_size = EVP_CIPHER_block_size(cipher_fp());

   if (password != NULL)
   {
      master_key = strdup(password);
      if (master_key == NULL)
      {
         pgexporter_log_error("encrypt_decrypt_buffer: Allocation failure (password)");
         ret = 1;
         goto cleanup;
      }
   }
   else
   {
      if (pgexporter_get_master_key(&master_key))
      {
         pgexporter_log_error("pgexporter_get_master_key: Invalid master key");
         ret = 1;
         goto cleanup;
      }
   }

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (enc == 1)
   {
      /* Generate a cryptographically random salt and IV */
      if (RAND_bytes(salt, PBKDF2_SALT_LENGTH) != 1)
      {
         pgexporter_log_error("RAND_bytes: Failed to generate salt");
         ret = 1;
         goto cleanup;
      }
      if (RAND_bytes(iv, iv_len) != 1)
      {
         pgexporter_log_error("RAND_bytes: Failed to generate IV");
         ret = 1;
         goto cleanup;
      }

      if (derive_key_iv(master_key, salt, key, NULL, mode, NULL) != 0)
      {
         pgexporter_log_error("derive_key_iv: Failed to derive key");
         ret = 1;
         goto cleanup;
      }

      /* Output buffer: salt + iv + encrypted data + tag + padding */
      outbuf_size = PBKDF2_SALT_LENGTH + iv_len + origin_size + tag_len + cipher_block_size;
      out_buf = (unsigned char*)malloc(outbuf_size + 1);
      if (out_buf == NULL)
      {
         pgexporter_log_error("pgexporter_encrypt_decrypt_buffer: Allocation failure");
         ret = 1;
         goto cleanup;
      }

      /* Prepend salt and IV */
      memcpy(out_buf, salt, PBKDF2_SALT_LENGTH);
      memcpy(out_buf + PBKDF2_SALT_LENGTH, iv, iv_len);

      if (!(ctx = EVP_CIPHER_CTX_new()))
      {
         pgexporter_log_error("EVP_CIPHER_CTX_new: Failed to create context");
         ret = 1;
         goto cleanup;
      }

      if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
      {
         pgexporter_log_error("EVP_CipherInit_ex: Failed to initialize cipher context");
         ret = 1;
         goto cleanup;
      }

      if (EVP_CipherUpdate(ctx, out_buf + PBKDF2_SALT_LENGTH + iv_len, &outl_i, origin_buffer, (int)origin_size) == 0)
      {
         pgexporter_log_error("EVP_CipherUpdate: Failed to process data");
         ret = 1;
         goto cleanup;
      }
      outl = (size_t)outl_i;

      *res_size = PBKDF2_SALT_LENGTH + iv_len + outl;

      if (EVP_CipherFinal_ex(ctx, out_buf + PBKDF2_SALT_LENGTH + iv_len + outl, &f_len_i) == 0)
      {
         pgexporter_log_error("EVP_CipherFinal_ex: Failed to finalize operation");
         ret = 1;
         goto cleanup;
      }
      f_len = (size_t)f_len_i;

      *res_size += f_len;

      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, out_buf + *res_size) != 1)
      {
         pgexporter_log_error("EVP_CIPHER_CTX_ctrl: Failed to get GCM tag");
         ret = 1;
         goto cleanup;
      }
      *res_size += tag_len;
   }
   else
   {
      /* Decryption: extract salt and IV from the binary header */
      if (origin_size < PBKDF2_SALT_LENGTH + iv_len + tag_len)
      {
         pgexporter_log_error("encrypt_decrypt_buffer: Input too short for decryption");
         ret = 1;
         goto cleanup;
      }

      memcpy(salt, origin_buffer, PBKDF2_SALT_LENGTH);
      memcpy(iv, origin_buffer + PBKDF2_SALT_LENGTH, iv_len);

      actual_input = origin_buffer + PBKDF2_SALT_LENGTH + iv_len;
      actual_input_size = origin_size - PBKDF2_SALT_LENGTH - iv_len - tag_len;

      if (derive_key_iv(master_key, salt, key, NULL, mode, NULL) != 0)
      {
         pgexporter_log_error("derive_key_iv: Failed to derive key");
         ret = 1;
         goto cleanup;
      }

      outbuf_size = actual_input_size;
      out_buf = (unsigned char*)malloc(outbuf_size + 1);
      if (out_buf == NULL)
      {
         pgexporter_log_error("pgexporter_encrypt_decrypt_buffer: Allocation failure");
         ret = 1;
         goto cleanup;
      }

      if (!(ctx = EVP_CIPHER_CTX_new()))
      {
         pgexporter_log_error("EVP_Cipher_CTX_new: Failed to create context");
         ret = 1;
         goto cleanup;
      }

      if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
      {
         pgexporter_log_error("EVP_CipherInit_ex: Failed to initialize cipher context");
         ret = 1;
         goto cleanup;
      }

      if (EVP_CipherUpdate(ctx, out_buf, &outl_i, actual_input, (int)actual_input_size) == 0)
      {
         pgexporter_log_error("EVP_CipherUpdate: Failed to process data");
         ret = 1;
         goto cleanup;
      }
      outl = (size_t)outl_i;

      *res_size = outl;

      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, origin_buffer + origin_size - tag_len) != 1)
      {
         pgexporter_log_error("EVP_CIPHER_CTX_ctrl: Failed to set GCM tag");
         ret = 1;
         goto cleanup;
      }

      if (EVP_CipherFinal_ex(ctx, out_buf + outl, &f_len_i) == 0)
      {
         pgexporter_log_error("EVP_CipherFinal_ex: Failed to finalize operation");
         ret = 1;
         goto cleanup;
      }
      f_len = (size_t)f_len_i;

      *res_size += f_len;
      out_buf[*res_size] = '\0';
   }

   /* Assign output block on success */
   *res_buffer = out_buf;

cleanup:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   /* Wipe key material from stack */
   pgexporter_cleanse(key, sizeof(key));
   pgexporter_cleanse(iv, sizeof(iv));

   if (master_key)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
      free(master_key);
   }

   if (ret != 0 && out_buf != NULL)
   {
      pgexporter_cleanse(out_buf, outbuf_size);
      free(out_buf);
   }

   return ret;
}

/**
 * Set the master salt for the high-iteration KDF
 * @param salt The 16-byte salt
 */
void
pgexporter_set_master_salt(unsigned char* salt)
{
   if (salt != NULL)
   {
      memcpy(master_salt, salt, PBKDF2_SALT_LENGTH);
      master_salt_set = true;
   }
}

/**
 * Clear the AES master key cache for the current thread from memory.
 * This function only affects the calling thread's thread-local cache and
 * does not clear AES master key caches in other threads.
 */
void
pgexporter_clear_aes_cache(void)
{
   pgexporter_cleanse(master_key_cache, sizeof(master_key_cache));
   pgexporter_cleanse(cached_master_salt, sizeof(cached_master_salt));
   pgexporter_cleanse(cached_password_hash, sizeof(cached_password_hash));
   cached_password_hash_len = 0;
   master_key_cached = false;
}

int
pgexporter_encrypt_file(char* input, char* output, char* password, int mode)
{
   FILE* f1 = NULL;
   FILE* f2 = NULL;
   int ret = 0;
   char* master_key = NULL;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   unsigned char tag[GCM_TAG_LENGTH];
   unsigned char in_buf[AES_CHUNK_SIZE];
   unsigned char out_buf[AES_CHUNK_SIZE + 16]; /* Add extra space for possible cipher block padding */
   int outl = 0;
   int iv_len = 0;
   EVP_CIPHER_CTX* ctx = NULL;
   const EVP_CIPHER* (*cipher_fp)(void);

   if (input == NULL || output == NULL || password == NULL)
   {
      return 1;
   }

   cipher_fp = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      return 1;
   }

   iv_len = EVP_CIPHER_iv_length(cipher_fp());

   /* Setup Master Key */
   master_key = strdup(password);
   if (master_key == NULL)
   {
      return 1;
   }

   /* Generate Salt and IV */
   if (RAND_bytes(salt, PBKDF2_SALT_LENGTH) != 1 || RAND_bytes(iv, iv_len) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   /* Derive File Key */
   if (derive_key_iv(master_key, salt, key, NULL, mode, NULL) != 0)
   {
      ret = 1;
      goto cleanup;
   }

   /* Open files */
   f1 = fopen(input, "rb");
   if (f1 == NULL)
   {
      ret = 1;
      goto cleanup;
   }

   f2 = fopen(output, "wb");
   if (f2 == NULL)
   {
      ret = 1;
      goto cleanup;
   }

   /* Write header: Salt + IV */
   if (fwrite(salt, 1, PBKDF2_SALT_LENGTH, f2) != PBKDF2_SALT_LENGTH ||
       fwrite(iv, 1, iv_len, f2) != iv_len)
   {
      ret = 1;
      goto cleanup;
   }

   /* Initialize encryption context */
   if (!(ctx = EVP_CIPHER_CTX_new()) ||
       EVP_EncryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   /* Process file in chunks */
   size_t n;
   while ((n = fread(in_buf, 1, sizeof(in_buf), f1)) > 0)
   {
      if (EVP_EncryptUpdate(ctx, out_buf, &outl, in_buf, (int)n) != 1)
      {
         ret = 1;
         goto cleanup;
      }
      if (fwrite(out_buf, 1, outl, f2) != (size_t)outl)
      {
         ret = 1;
         goto cleanup;
      }
   }

   if (EVP_EncryptFinal_ex(ctx, out_buf, &outl) != 1)
   {
      ret = 1;
      goto cleanup;
   }
   if (fwrite(out_buf, 1, outl, f2) != (size_t)outl)
   {
      ret = 1;
      goto cleanup;
   }

   /* Get and write GCM tag */
   if (pgexporter_is_gcm(mode))
   {
      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, tag) != 1)
      {
         ret = 1;
         goto cleanup;
      }
      if (fwrite(tag, 1, GCM_TAG_LENGTH, f2) != GCM_TAG_LENGTH)
      {
         ret = 1;
         goto cleanup;
      }
   }

cleanup:
   if (f1)
   {
      fclose(f1);
   }
   if (f2)
   {
      fclose(f2);
   }
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }
   if (master_key)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
      free(master_key);
   }
   pgexporter_cleanse(key, sizeof(key));
   pgexporter_cleanse(iv, sizeof(iv));

   return ret;
}

int
pgexporter_decrypt_file(char* input, char* output, char* password, int mode)
{
   FILE* f1 = NULL;
   FILE* f2 = NULL;
   int ret = 0;
   char* master_key = NULL;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   unsigned char tag[GCM_TAG_LENGTH];
   unsigned char in_buf[AES_CHUNK_SIZE + 16];
   unsigned char out_buf[AES_CHUNK_SIZE + 32];
   int outl = 0;
   int iv_len = 0;
   size_t file_size = 0;
   size_t ciphertext_size = 0;
   EVP_CIPHER_CTX* ctx = NULL;
   const EVP_CIPHER* (*cipher_fp)(void);

   if (input == NULL || output == NULL || password == NULL)
   {
      return 1;
   }

   cipher_fp = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      return 1;
   }

   iv_len = EVP_CIPHER_iv_length(cipher_fp());

   /* Open input file */
   f1 = fopen(input, "rb");
   if (f1 == NULL)
   {
      return 1;
   }

   /* Get file size */
   if (fseek(f1, 0, SEEK_END) != 0)
   {
      fclose(f1);
      return 1;
   }
   file_size = (size_t)ftell(f1);
   fseek(f1, 0, SEEK_SET);

   /* Minimum size: Salt + IV */
   if (file_size < (size_t)PBKDF2_SALT_LENGTH + iv_len)
   {
      fclose(f1);
      return 1;
   }

   /* Read Salt and IV */
   if (fread(salt, 1, PBKDF2_SALT_LENGTH, f1) != PBKDF2_SALT_LENGTH ||
       fread(iv, 1, iv_len, f1) != (size_t)iv_len)
   {
      fclose(f1);
      return 1;
   }

   /* Calculate ciphertext size */
   ciphertext_size = file_size - PBKDF2_SALT_LENGTH - iv_len;
   if (pgexporter_is_gcm(mode))
   {
      if (ciphertext_size < GCM_TAG_LENGTH)
      {
         fclose(f1);
         return 1;
      }
      ciphertext_size -= GCM_TAG_LENGTH;

      /* Read Tag from the end */
      if (fseek(f1, file_size - GCM_TAG_LENGTH, SEEK_SET) != 0)
      {
         fclose(f1);
         return 1;
      }
      if (fread(tag, 1, GCM_TAG_LENGTH, f1) != GCM_TAG_LENGTH)
      {
         fclose(f1);
         return 1;
      }
      /* Seek back to start of ciphertext */
      fseek(f1, PBKDF2_SALT_LENGTH + iv_len, SEEK_SET);
   }

   /* Setup Master Key */
   master_key = strdup(password);
   if (master_key == NULL)
   {
      fclose(f1);
      return 1;
   }

   /* Derive File Key */
   if (derive_key_iv(master_key, salt, key, NULL, mode, NULL) != 0)
   {
      ret = 1;
      goto cleanup;
   }

   f2 = fopen(output, "wb");
   if (f2 == NULL)
   {
      ret = 1;
      goto cleanup;
   }

   /* Initialize decryption context */
   if (!(ctx = EVP_CIPHER_CTX_new()) ||
       EVP_DecryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      ret = 1;
      goto cleanup;
   }

   /* Set expected GCM tag */
   if (pgexporter_is_gcm(mode))
   {
      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, tag) != 1)
      {
         ret = 1;
         goto cleanup;
      }
   }

   /* Process ciphertext in chunks */
   size_t total_processed = 0;
   while (total_processed < ciphertext_size)
   {
      size_t to_read = sizeof(in_buf);
      if (to_read > (ciphertext_size - total_processed))
      {
         to_read = ciphertext_size - total_processed;
      }

      size_t n = fread(in_buf, 1, to_read, f1);
      if (n == 0)
      {
         break;
      }

      if (EVP_DecryptUpdate(ctx, out_buf, &outl, in_buf, (int)n) != 1)
      {
         ret = 1;
         goto cleanup;
      }
      if (fwrite(out_buf, 1, outl, f2) != (size_t)outl)
      {
         ret = 1;
         goto cleanup;
      }
      total_processed += n;
   }

   if (EVP_DecryptFinal_ex(ctx, out_buf, &outl) <= 0)
   {
      /* Authentication failure or other error */
      ret = 1;
      goto cleanup;
   }
   if (fwrite(out_buf, 1, outl, f2) != (size_t)outl)
   {
      ret = 1;
      goto cleanup;
   }

cleanup:
   if (f1)
   {
      fclose(f1);
   }
   if (f2)
   {
      fclose(f2);
      if (ret != 0)
      {
         remove(output); /* Don't keep partial/unauthenticated files */
      }
   }
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }
   if (master_key)
   {
      pgexporter_cleanse(master_key, strlen(master_key));
      free(master_key);
   }
   pgexporter_cleanse(key, sizeof(key));
   pgexporter_cleanse(iv, sizeof(iv));

   return ret;
}

static void __attribute__((destructor))
pgexporter_aes_cache_destructor(void)
{
   pgexporter_clear_aes_cache();
}
