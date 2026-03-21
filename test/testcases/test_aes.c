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
#include <aes.h>
#include <tscommon.h>
#include <mctf.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <security.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utils.h>

static char test_home[MAX_PATH];

static void
setup_mock_master_key(void)
{
   char dir[MAX_PATH];
   char file[MAX_PATH];
   FILE* f;

   if (test_home[0] == '\0')
   {
      char tmp[] = "/tmp/pgexporter_test_XXXXXX";
      if (mkdtemp(tmp) == NULL)
      {
         err(1, "mkdtemp failed");
      }
      pgexporter_snprintf(test_home, sizeof(test_home), "%s", tmp);
      setenv("PGEXPORTER_HOME", test_home, 1);
   }

   memset(&dir[0], 0, sizeof(dir));
   pgexporter_snprintf(&dir[0], sizeof(dir), "%s/.pgexporter", pgexporter_get_home_directory());
   if (mkdir(&dir[0], S_IRWXU) != 0 && errno != EEXIST)
   {
      err(1, "mkdir failed for %s", &dir[0]);
   }
   if (chmod(&dir[0], S_IRWXU) != 0)
   {
      err(1, "chmod failed for %s", &dir[0]);
   }

   memset(&file[0], 0, sizeof(file));
   pgexporter_snprintf(&file[0], sizeof(file), "%s/master.key", &dir[0]);

   f = fopen(&file[0], "w");
   if (f == NULL)
   {
      err(1, "fopen failed for %s", &file[0]);
   }

   if (fputs("bWFzdGVyLWtleS1mb3ItdGVzdGluZw==\n", f) == EOF)
   {
      fclose(f);
      err(1, "fputs failed for %s", &file[0]);
   }
   if (fputs("AAAAAAAAAAAAAAAAAAAAAA==\n", f) == EOF)
   {
      fclose(f);
      err(1, "fputs failed for %s", &file[0]);
   }

   fclose(f);
   if (chmod(&file[0], S_IRUSR | S_IWUSR) != 0)
   {
      err(1, "chmod failed for %s", &file[0]);
   }

   char* master_key = NULL;
   unsigned char* master_salt = NULL;
   if (pgexporter_get_master_key_and_salt(&master_key, &master_salt, NULL) == 0)
   {
      pgexporter_set_master_salt(master_salt);
      if (master_key != NULL)
      {
         pgexporter_cleanse(master_key, strlen(master_key));
      }
      free(master_key);
      free(master_salt);
   }
}

static void
teardown_mock_master_key(void)
{
   if (test_home[0] != '\0')
   {
      unsigned char default_salt[PBKDF2_SALT_LENGTH];

      pgexporter_delete_directory(test_home);
      memset(test_home, 0, sizeof(test_home));
      unsetenv("PGEXPORTER_HOME");

      /* Reset global test state to runner's default */
      memset(default_salt, 0x42, PBKDF2_SALT_LENGTH);
      pgexporter_set_master_salt(default_salt);
      pgexporter_clear_aes_cache();
   }
}

/**
 * Test: AES-256-GCM encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_encrypt_decrypt_roundtrip)
{
   char* plaintext = "pgexporter-test-password-round-trip";
   char* password = "master-key-for-testing";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   int iv_len = EVP_CIPHER_iv_length(EVP_aes_256_gcm());

   setup_mock_master_key();
   ret_enc = pgexporter_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM);

   MCTF_ASSERT(ret_enc == 0, cleanup, "pgexporter_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");
   MCTF_ASSERT(ciphertext_length > PBKDF2_SALT_LENGTH + iv_len + GCM_TAG_LENGTH, cleanup, "ciphertext_length should be greater than salt + iv + tag length");

   ret_dec = pgexporter_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_256_GCM);

   MCTF_ASSERT(ret_dec == 0, cleanup, "pgexporter_decrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(decrypted, cleanup, "decrypted should not be NULL");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original plaintext");

cleanup:
   free(ciphertext);
   if (decrypted != NULL)
   {
      pgexporter_cleanse(decrypted, strlen(decrypted));
   }
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: Salt verification — same password produces different ciphertext.
 *
 * Encrypts the exact same plaintext with the same password twice and
 * verifies that the two ciphertext outputs are different. This proves
 * the 16-byte random salt is working correctly.
 */
MCTF_TEST(test_aes_salt_produces_unique_ciphertext)
{
   char* plaintext = "identical-password-for-salt-test";
   char* password = "master-key-for-testing";
   char* ciphertext_a = NULL;
   int ciphertext_a_length = 0;
   char* ciphertext_b = NULL;
   int ciphertext_b_length = 0;
   int ret_a = 0;
   int ret_b = 0;
   int blobs_are_identical = 0;

   setup_mock_master_key();
   ret_a = pgexporter_encrypt(plaintext, password, &ciphertext_a, &ciphertext_a_length, ENCRYPTION_AES_256_GCM);

   MCTF_ASSERT(ret_a == 0, cleanup, "first pgexporter_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext_a, cleanup, "ciphertext_a should not be NULL");
   MCTF_ASSERT(ciphertext_a_length > 0, cleanup, "ciphertext_a_length should be greater than 0");

   ret_b = pgexporter_encrypt(plaintext, password, &ciphertext_b, &ciphertext_b_length, ENCRYPTION_AES_256_GCM);

   MCTF_ASSERT(ret_b == 0, cleanup, "second pgexporter_encrypt should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext_b, cleanup, "ciphertext_b should not be NULL");
   MCTF_ASSERT(ciphertext_b_length > 0, cleanup, "ciphertext_b_length should be greater than 0");

   /* The entire ciphertext will be different because the random salt at the beginning changes the AES-GCM output */
   if (ciphertext_a_length == ciphertext_b_length)
   {
      blobs_are_identical = (memcmp(ciphertext_a, ciphertext_b, ciphertext_a_length) == 0);
   }

   MCTF_ASSERT(!blobs_are_identical, cleanup,
               "encrypting the same plaintext twice must produce entirely different ciphertext blobs");

cleanup:
   free(ciphertext_a);
   free(ciphertext_b);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: AES-192-GCM encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_192_gcm_roundtrip)
{
   char* plaintext = "test-192-gcm-mode-round-trip";
   char* password = "master-key-192-test";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   setup_mock_master_key();
   ret_enc = pgexporter_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_192_GCM);
   MCTF_ASSERT(ret_enc == 0, cleanup, "pgexporter_encrypt with AES-192-GCM should succeed");

   ret_dec = pgexporter_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_192_GCM);
   MCTF_ASSERT(ret_dec == 0, cleanup, "pgexporter_decrypt with AES-192-GCM should succeed");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-192-GCM");

cleanup:
   free(ciphertext);
   if (decrypted != NULL)
   {
      pgexporter_cleanse(decrypted, strlen(decrypted));
   }
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: AES-128-GCM encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_128_gcm_roundtrip)
{
   char* plaintext = "test-128-gcm-mode-round-trip";
   char* password = "master-key-128-test";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   setup_mock_master_key();
   ret_enc = pgexporter_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_128_GCM);
   MCTF_ASSERT(ret_enc == 0, cleanup, "pgexporter_encrypt with AES-128-GCM should succeed");

   ret_dec = pgexporter_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_128_GCM);
   MCTF_ASSERT(ret_dec == 0, cleanup, "pgexporter_decrypt with AES-128-GCM should succeed");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-128-GCM");

cleanup:
   free(ciphertext);
   if (decrypted != NULL)
   {
      pgexporter_cleanse(decrypted, strlen(decrypted));
   }
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: AES Buffer encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_buffer_roundtrip)
{
   setup_mock_master_key();
   unsigned char origin[32];
   unsigned char* enc = NULL;
   size_t enc_size = 0;
   unsigned char* dec = NULL;
   size_t dec_size = 0;

   memset(origin, 0x42, sizeof(origin));

   MCTF_ASSERT(pgexporter_encrypt_buffer_with_password(origin, sizeof(origin), "test-password", &enc, &enc_size, ENCRYPTION_AES_256_GCM) == 0, cleanup, "encrypt buffer should succeed");
   MCTF_ASSERT_PTR_NONNULL(enc, cleanup, "encrypted buffer should not be NULL");
   MCTF_ASSERT(enc_size > sizeof(origin), cleanup, "encrypted size should be larger than original");

   MCTF_ASSERT(pgexporter_decrypt_buffer_with_password(enc, enc_size, "test-password", &dec, &dec_size, ENCRYPTION_AES_256_GCM) == 0, cleanup, "decrypt buffer should succeed");
   MCTF_ASSERT(dec_size == sizeof(origin), cleanup, "decrypted size should match original");
   MCTF_ASSERT(memcmp(origin, dec, dec_size) == 0, cleanup, "decrypted content should match original");

cleanup:
   free(enc);
   if (dec != NULL)
   {
      pgexporter_cleanse(dec, dec_size);
   }
   free(dec);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: AES Buffer tamper fails.
 */
MCTF_TEST(test_aes_buffer_tamper_fails)
{
   setup_mock_master_key();
   unsigned char origin[32];
   unsigned char* enc = NULL;
   size_t enc_size = 0;
   unsigned char* dec = NULL;
   size_t dec_size = 0;

   int iv_len = EVP_CIPHER_iv_length(EVP_aes_256_gcm());

   memset(origin, 0x42, sizeof(origin));

   MCTF_ASSERT(pgexporter_encrypt_buffer_with_password(origin, sizeof(origin), "test-password", &enc, &enc_size, ENCRYPTION_AES_256_GCM) == 0, cleanup, "encrypt buffer should succeed");

   /* Tamper with encrypted payload (at salt + iv + some data offset) */
   size_t tamper_index = (size_t)PBKDF2_SALT_LENGTH + (size_t)iv_len + 2;
   MCTF_ASSERT(enc_size > tamper_index, cleanup, "encrypted buffer must be large enough to tamper with payload");
   enc[tamper_index] ^= 0x01;

   MCTF_ASSERT(pgexporter_decrypt_buffer_with_password(enc, enc_size, "test-password", &dec, &dec_size, ENCRYPTION_AES_256_GCM) != 0, cleanup, "decrypting tampered buffer should fail");

cleanup:
   free(enc);
   free(dec);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: AES Buffer empty payload.
 */
MCTF_TEST(test_aes_buffer_empty_payload)
{
   setup_mock_master_key();
   unsigned char* enc = NULL;
   size_t enc_size = 0;
   unsigned char* dec = NULL;
   size_t dec_size = 0;

   MCTF_ASSERT(pgexporter_encrypt_buffer_with_password(NULL, 0, "test-password", &enc, &enc_size, ENCRYPTION_AES_256_GCM) == 0, cleanup, "encrypting empty buffer should succeed");
   MCTF_ASSERT(pgexporter_decrypt_buffer_with_password(enc, enc_size, "test-password", &dec, &dec_size, ENCRYPTION_AES_256_GCM) == 0, cleanup, "decrypting empty buffer should succeed");
   MCTF_ASSERT(dec_size == 0, cleanup, "decrypted size for empty payload should be 0");

cleanup:
   free(enc);
   free(dec);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: Decryption with wrong password fails (no leak verification).
 */
MCTF_TEST(test_aes_decrypt_wrong_password_no_leak)
{
   char* plaintext = "secret-data-wrong-password-test";
   char* correct_password = "correct-master-key";
   char* wrong_password = "wrong-master-key";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   setup_mock_master_key();

   ret_enc = pgexporter_encrypt(plaintext, correct_password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM);
   MCTF_ASSERT(ret_enc == 0, cleanup, "pgexporter_encrypt should succeed");

   ret_dec = pgexporter_decrypt(ciphertext, ciphertext_length, wrong_password, &decrypted, ENCRYPTION_AES_256_GCM);
   MCTF_ASSERT(ret_dec != 0, cleanup, "pgexporter_decrypt with wrong password should fail");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted should be NULL on wrong password");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: Truncated ciphertext is rejected gracefully.
 */
MCTF_TEST(test_aes_decrypt_truncated_ciphertext_fails)
{
   char truncated_buf[10];
   char* password = "master-key-for-testing";
   char* decrypted = NULL;
   int ret = 0;

   memset(truncated_buf, 0xAB, sizeof(truncated_buf));

   ret = pgexporter_decrypt(truncated_buf, sizeof(truncated_buf), password, &decrypted, ENCRYPTION_AES_256_GCM);

   MCTF_ASSERT(ret != 0, cleanup, "pgexporter_decrypt should reject ciphertext shorter than salt + IV + tag length");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted should be NULL on failure");

cleanup:
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: GCM tag tampering fails.
 */
MCTF_TEST(test_aes_gcm_tag_tampering_fails)
{
   char* plaintext = "sensitive-tag-test-data";
   char* password = "master-key-for-testing";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   setup_mock_master_key();

   MCTF_ASSERT(pgexporter_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgexporter_encrypt should succeed");

   /* Tamper with the GCM tag (at the end of the ciphertext) */
   ciphertext[ciphertext_length - 1] ^= 0x01;

   MCTF_ASSERT(pgexporter_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_256_GCM) != 0, cleanup, "decrypting with tampered tag should fail");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: GCM authentication failure (tampered ciphertext).
 */
MCTF_TEST(test_aes_gcm_authentication_failure)
{
   char* plaintext = "authentic-data-to-be-tampered";
   char* password = "master-key-for-testing";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   setup_mock_master_key();

   int iv_len = EVP_CIPHER_iv_length(EVP_aes_256_gcm());

   ret_enc = pgexporter_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM);
   MCTF_ASSERT(ret_enc == 0, cleanup, "pgexporter_encrypt should succeed");

   /* Tamper with the encrypted data (flip a bit in the middle of the payload) */
   size_t tamper_index = (size_t)PBKDF2_SALT_LENGTH + (size_t)iv_len + 5;
   MCTF_ASSERT((size_t)ciphertext_length > tamper_index, cleanup, "ciphertext must be large enough to tamper with payload");
   ciphertext[tamper_index] ^= 0xFF;

   ret_dec = pgexporter_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_256_GCM);
   MCTF_ASSERT(ret_dec != 0, cleanup, "pgexporter_decrypt should fail if ciphertext is tampered with (GCM)");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted should be NULL if tampering detected");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: Salt mutation fails decryption.
 */
MCTF_TEST(test_aes_salt_mutation_fails)
{
   char* plaintext = "mutation-test-payload";
   char* password = "master-key-for-testing";
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;
   int ret_enc = 0;
   int ret_dec = 0;

   setup_mock_master_key();
   ret_enc = pgexporter_encrypt(plaintext, password, &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM);
   MCTF_ASSERT(ret_enc == 0, cleanup, "encrypt should succeed");

   /* Tamper with the salt (first 16 bytes) */
   MCTF_ASSERT(ciphertext_length > PBKDF2_SALT_LENGTH, cleanup, "ciphertext too short");
   ciphertext[0] ^= 0x01; /* Mutate first byte of salt */

   ret_dec = pgexporter_decrypt(ciphertext, ciphertext_length, password, &decrypted, ENCRYPTION_AES_256_GCM);
   MCTF_ASSERT(ret_dec != 0, cleanup, "decrypting with mutated salt should fail");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted string must be NULL on failure");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: File encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_file_roundtrip)
{
   char path_in[1024];
   char path_enc[1024];
   char path_dec[1024];
   char* password = "master-key-for-testing";
   char* plaintext = "this-is-a-file-content-test";
   FILE* f;
   char read_back[128];

   setup_mock_master_key();

   pgexporter_snprintf(path_in, sizeof(path_in), "%s/test_plain.txt", test_home);
   pgexporter_snprintf(path_enc, sizeof(path_enc), "%s/test_plain.txt.enc", test_home);
   pgexporter_snprintf(path_dec, sizeof(path_dec), "%s/test_plain.txt.dec", test_home);

   /* 1. Create plaintext file */
   f = fopen(path_in, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "failed to create test file");
   fputs(plaintext, f);
   fclose(f);

   /* 2. Encrypt */
   MCTF_ASSERT(pgexporter_encrypt_file(path_in, path_enc, password, ENCRYPTION_AES_256_GCM) == 0, cleanup, "file encryption failed");

   /* 3. Decrypt */
   MCTF_ASSERT(pgexporter_decrypt_file(path_enc, path_dec, password, ENCRYPTION_AES_256_GCM) == 0, cleanup, "file decryption failed");

   /* 4. Verify */
   f = fopen(path_dec, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "failed to open decrypted file");
   memset(read_back, 0, sizeof(read_back));
   fgets(read_back, sizeof(read_back), f);
   fclose(f);

   MCTF_ASSERT_STR_EQ(read_back, plaintext, cleanup, "decrypted file content mismatch");

cleanup:
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: Empty file encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_file_empty_roundtrip)
{
   char path_in[1024];
   char path_enc[1024];
   char path_dec[1024];
   char* password = "master-key-for-testing";
   FILE* f;
   struct stat st;

   setup_mock_master_key();

   pgexporter_snprintf(path_in, sizeof(path_in), "%s/test_empty.txt", test_home);
   pgexporter_snprintf(path_enc, sizeof(path_enc), "%s/test_empty.txt.enc", test_home);
   pgexporter_snprintf(path_dec, sizeof(path_dec), "%s/test_empty.txt.dec", test_home);

   /* 1. Create empty file */
   f = fopen(path_in, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "failed to create empty test file");
   fclose(f);

   /* 2. Encrypt */
   MCTF_ASSERT(pgexporter_encrypt_file(path_in, path_enc, password, ENCRYPTION_AES_256_GCM) == 0, cleanup, "empty file encryption failed");

   /* 3. Decrypt */
   MCTF_ASSERT(pgexporter_decrypt_file(path_enc, path_dec, password, ENCRYPTION_AES_256_GCM) == 0, cleanup, "empty file decryption failed");

   /* 4. Verify size is 0 */
   MCTF_ASSERT(stat(path_dec, &st) == 0, cleanup, "failed to stat decrypted file");
   MCTF_ASSERT(st.st_size == 0, cleanup, "decrypted empty file is not 0 bytes");

cleanup:
   teardown_mock_master_key();
   MCTF_FINISH();
}

/**
 * Test: File tampering fails (GCM).
 */
MCTF_TEST(test_aes_file_tamper_fails)
{
   char path_in[1024];
   char path_enc[1024];
   char path_dec[1024];
   char* password = "master-key-for-testing";
   char* plaintext = "data-to-be-tampered-in-file";
   FILE* f;
   long pos;

   setup_mock_master_key();

   pgexporter_snprintf(path_in, sizeof(path_in), "%s/test_tamper.txt", test_home);
   pgexporter_snprintf(path_enc, sizeof(path_enc), "%s/test_tamper.txt.enc", test_home);
   pgexporter_snprintf(path_dec, sizeof(path_dec), "%s/test_tamper.txt.dec", test_home);

   /* 1. Create and Encrypt */
   f = fopen(path_in, "w");
   fputs(plaintext, f);
   fclose(f);
   MCTF_ASSERT(pgexporter_encrypt_file(path_in, path_enc, password, ENCRYPTION_AES_256_GCM) == 0, cleanup, "encryption failed");

   /* 2. Tamper with the IV or payload in the file */
   f = fopen(path_enc, "r+b");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "failed to open encrypted file for tampering");
   fseek(f, PBKDF2_SALT_LENGTH + 2, SEEK_SET); /* Tamper with IV area */
   int c = fgetc(f);
   fseek(f, -1, SEEK_CUR);
   fputc(c ^ 0xFF, f);
   fclose(f);

   /* 3. Decrypt should fail */
   MCTF_ASSERT(pgexporter_decrypt_file(path_enc, path_dec, password, ENCRYPTION_AES_256_GCM) != 0, cleanup, "decryption should have failed due to tampering");

cleanup:
   teardown_mock_master_key();
   MCTF_FINISH();
}
