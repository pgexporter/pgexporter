# Migration

## From 0.7.x to 0.8.0

### Vault Encryption

The key derivation for vault file encryption has been upgraded to
`PKCS5_PBKDF2_HMAC` (SHA-256, random 16-byte salt, 600,000 iterations),
and the encryption mode has been upgraded to **AES-GCM** (authenticated encryption).

This is a **breaking change**. Existing vault files encrypted with the
old method (CBC/CTR) cannot be decrypted by version 0.8.0.

The `master.key` file now stores both the password and a random salt
used for key derivation. Existing `master.key` files must be regenerated.
**Action required:**

1. Stop pgexporter
2. Delete the existing master key:
   ```
   rm ~/.pgexporter/master.key
   ```
3. Regenerate the master key:
   ```
   pgexporter-admin master-key
   ```
4. Restart pgexporter
