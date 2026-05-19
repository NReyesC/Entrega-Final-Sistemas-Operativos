#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define CEDS_SALT_SIZE 16
#define CEDS_IV_SIZE   16

typedef struct {
    char   *data;
    size_t  len;
    int     locked;
} SecureBuffer;

typedef struct __attribute__((packed)) {
    uint8_t salt[CEDS_SALT_SIZE];
    uint8_t iv[CEDS_IV_SIZE];
} CryptoMetadata;

int secure_prompt_passphrase(const char *prompt, SecureBuffer *out);
int secure_buffer_copy(SecureBuffer *out, const char *src, size_t len);
void secure_buffer_destroy(SecureBuffer *buf);
void secure_memzero(void *ptr, size_t len);

int crypto_random_bytes(uint8_t *buf, size_t len);
int crypto_encrypt_buffer(const uint8_t *plain, size_t plain_len,
                          const char *passphrase, size_t passphrase_len,
                          const uint8_t salt[CEDS_SALT_SIZE],
                          const uint8_t iv[CEDS_IV_SIZE],
                          uint8_t **out, size_t *out_len);
int crypto_decrypt_buffer(const uint8_t *cipher, size_t cipher_len,
                          const char *passphrase, size_t passphrase_len,
                          const uint8_t salt[CEDS_SALT_SIZE],
                          const uint8_t iv[CEDS_IV_SIZE],
                          uint8_t **out, size_t *out_len);

#endif /* CRYPTO_H */
