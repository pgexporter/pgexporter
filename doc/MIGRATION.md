# Migration

## From 0.8.x to 0.9.x

### MD5 Authentication Removal

Legacy MD5 authentication has been completely removed from pgexporter for improved security. This is a **breaking change** for any deployment still relying on MD5.

**Action required:**

1. **PostgreSQL Server**:
   - Ensure `password_encryption = scram-sha-256` is set in `postgresql.conf`.
   - Update `pg_hba.conf` to use `scram-sha-256` instead of `md5`.
   - For existing users with MD5 passwords, you **must** reset their passwords while SCRAM encryption is active so that PostgreSQL generates SCRAM‑compatible verifiers.
2. **pgexporter**:
   - Ensure the user configured in `pgexporter.conf` uses `scram-sha-256` for database connectivity. MD5 is no longer a valid method.
3. **Clients**:
   - Ensure clients are compatible with `scram-sha-256` (standard for modern PostgreSQL drivers).

## From 0.7.x to 0.8.x

### Vault Encryption

The key derivation for vault file encryption has been upgraded to
`PKCS5_PBKDF2_HMAC` (SHA-256, random 16-byte salt, 600,000 iterations),
and the encryption mode has been upgraded to **AES-GCM** (authenticated encryption).

This is a **breaking change**. Existing vault files encrypted with the
old method (CBC/CTR) cannot be decrypted by version 0.8.x.

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
