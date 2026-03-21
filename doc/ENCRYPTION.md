## Overview
AES-GCM (Galois/Counter Mode) is the recommended encryption mode in pgexporter when encryption is enabled. It provides both confidentiality (encryption) and integrity/authenticity (verification), ensuring that encrypted data has not been tampered with. The default setup is no encryption.

## Encryption Configuration
`none`: No encryption (default value)

`aes | aes256`: AES-256 GCM mode with 256-bit key length

`aes192`: AES-192 GCM mode with 192-bit key length

`aes128`: AES-128 GCM mode with 128-bit key length


## Encryption / Decryption CLI Commands
### decrypt
The `pgexporter-cli` provides a `decrypt` command to decrypt a file.

### encrypt
The `pgexporter-cli` provides an `encrypt` command to encrypt a file.

## Key Derivation and Caching

To encrypt many files efficiently without paying the computational cost of thousands of iterations for every file, `pgexporter` uses a two-step key derivation process:

1. **Master Key Derivation (Slow):** The master key is derived from the user-provided password and a randomly generated master salt using `PKCS5_PBKDF2_HMAC` (SHA-256) with a high number of iterations (600,000). The master salt is stored persistently (for example, in `master.key`) and reused for subsequent master key derivations until it is rotated. This provides strong resistance against brute-force attacks.
2. **Key Caching:** This master key is cached in volatile memory for the duration of the management process, eliminating the overhead of repeating the expensive PBKDF2 operation on every use.
3. **File Key Derivation (Fast):** For every individual file or operation, a unique random salt and a unique random initialization vector (IV) are generated. A specific key is then derived from the cached master key and the random salt using `PKCS5_PBKDF2_HMAC` with 1 iteration. This ensures every operation is cryptographically isolated.

The encrypted format is: `[Salt(16)][IV(EVP_CIPHER_iv_length)][Ciphertext][Tag(16)]` (for AES-GCM, the IV is typically 12 bytes).

The cache is automatically and securely wiped when the process lifecycle ends using a destructor pattern.

## Benchmark
Check if your CPU has [AES-NI](https://en.wikipedia.org/wiki/AES_instruction_set)
```sh
cat /proc/cpuinfo | grep aes
```
or 
```sh
lscpu | grep '^CPU(s):'
```

By default, OpenSSL uses AES-NI if the CPU supports it.
```sh
openssl speed -elapsed -evp aes-256-gcm
```
 
Speed test with explicitly disabled AES-NI feature
```sh
OPENSSL_ia32cap="~0x200000200000000" openssl speed -elapsed -evp aes-256-gcm
```
 
Test decrypt
```sh
openssl speed -elapsed -decrypt -evp aes-256-gcm
```
 
Speed test with 8 cores
```
openssl speed -multi 8 -elapsed -evp aes-256-gcm
```
