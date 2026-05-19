#define _POSIX_C_SOURCE 200809L

#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>

#define PBKDF2_ITERATIONS 1000000
#define AES_KEY_SIZE      32

static int secure_lock(void *ptr, size_t len) {
    if (!ptr || len == 0) return 0;
    if (mlock(ptr, len) == 0) return 0;
    fprintf(stderr, "Aviso: mlock() no pudo proteger %zu bytes de memoria sensible.\n", len);
    return -1;
}

void secure_memzero(void *ptr, size_t len) {
    if (!ptr || len == 0) return;
#if defined(__GLIBC__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    explicit_bzero(ptr, len);
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
#endif
}

int secure_buffer_copy(SecureBuffer *out, const char *src, size_t len) {
    if (!out || !src) return -1;

    out->data = calloc(len + 1, 1);
    if (!out->data) return -1;

    out->locked = (secure_lock(out->data, len + 1) == 0);
    memcpy(out->data, src, len);
    out->len = len;
    return 0;
}

void secure_buffer_destroy(SecureBuffer *buf) {
    if (!buf || !buf->data) return;
    secure_memzero(buf->data, buf->len + 1);
    if (buf->locked) {
        munlock(buf->data, buf->len + 1);
    }
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->locked = 0;
}

static int prompt_from_stream(FILE *in, FILE *out, const char *prompt, SecureBuffer *buf) {
    char *line = NULL;
    size_t cap = 0;
    struct termios oldt;
    int has_tty = isatty(fileno(in));
    int restored = 0;

    fprintf(out, "%s", prompt);
    fflush(out);

    if (has_tty && tcgetattr(fileno(in), &oldt) == 0) {
        struct termios newt = oldt;
        newt.c_lflag &= ~(ECHO);
        if (tcsetattr(fileno(in), TCSAFLUSH, &newt) == 0) {
            restored = 1;
        }
    }

    ssize_t nread = getline(&line, &cap, in);

    if (restored) {
        tcsetattr(fileno(in), TCSAFLUSH, &oldt);
        fputc('\n', out);
        fflush(out);
    }

    if (nread < 0) {
        free(line);
        return -1;
    }

    while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
        line[--nread] = '\0';
    }

    if (nread == 0) {
        secure_memzero(line, cap);
        free(line);
        return -1;
    }

    int rc = secure_buffer_copy(buf, line, (size_t)nread);
    secure_memzero(line, cap);
    free(line);
    return rc;
}

int secure_prompt_passphrase(const char *prompt, SecureBuffer *out) {
    const char *env = getenv("CEDS_KEY");
    memset(out, 0, sizeof(*out));

    if (env && env[0]) {
        return secure_buffer_copy(out, env, strlen(env));
    }

    FILE *tty = fopen("/dev/tty", "r+");
    if (tty) {
        int rc = prompt_from_stream(tty, tty, prompt, out);
        fclose(tty);
        return rc;
    }

    return prompt_from_stream(stdin, stderr, prompt, out);
}

static int derive_key(const char *passphrase, size_t passphrase_len,
                      const uint8_t salt[CEDS_SALT_SIZE], SecureBuffer *key) {
    memset(key, 0, sizeof(*key));
    key->data = calloc(AES_KEY_SIZE, 1);
    if (!key->data) return -1;

    key->locked = (secure_lock(key->data, AES_KEY_SIZE) == 0);
    key->len = AES_KEY_SIZE;

    if (PKCS5_PBKDF2_HMAC(passphrase, (int)passphrase_len,
                          salt, CEDS_SALT_SIZE,
                          PBKDF2_ITERATIONS,
                          EVP_sha256(), AES_KEY_SIZE,
                          (unsigned char *)key->data) != 1) {
        secure_buffer_destroy(key);
        return -1;
    }

    return 0;
}

int crypto_random_bytes(uint8_t *buf, size_t len) {
    if (!buf || len == 0) return -1;
    return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}

static int crypt_buffer(int encrypt,
                        const uint8_t *input, size_t input_len,
                        const char *passphrase, size_t passphrase_len,
                        const uint8_t salt[CEDS_SALT_SIZE],
                        const uint8_t iv[CEDS_IV_SIZE],
                        uint8_t **out, size_t *out_len) {
    SecureBuffer key;
    EVP_CIPHER_CTX *ctx = NULL;
    uint8_t *buffer = NULL;
    int outl1 = 0, outl2 = 0;
    int rc = -1;

    if (!input || !passphrase || passphrase_len == 0 || !out || !out_len) return -1;

    if (derive_key(passphrase, passphrase_len, salt, &key) < 0) {
        return -1;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto cleanup;

    buffer = malloc(input_len + EVP_MAX_BLOCK_LENGTH + 1);
    if (!buffer) goto cleanup;

    if (encrypt) {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                               (unsigned char *)key.data, iv) != 1) goto cleanup;
        if (EVP_EncryptUpdate(ctx, buffer, &outl1, input, (int)input_len) != 1) goto cleanup;
        if (EVP_EncryptFinal_ex(ctx, buffer + outl1, &outl2) != 1) goto cleanup;
    } else {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
                               (unsigned char *)key.data, iv) != 1) goto cleanup;
        if (EVP_DecryptUpdate(ctx, buffer, &outl1, input, (int)input_len) != 1) goto cleanup;
        if (EVP_DecryptFinal_ex(ctx, buffer + outl1, &outl2) != 1) goto cleanup;
    }

    *out_len = (size_t)(outl1 + outl2);
    *out = buffer;
    buffer = NULL;
    rc = 0;

cleanup:
    if (buffer) {
        secure_memzero(buffer, input_len + EVP_MAX_BLOCK_LENGTH + 1);
        free(buffer);
    }
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    secure_buffer_destroy(&key);
    return rc;
}

int crypto_encrypt_buffer(const uint8_t *plain, size_t plain_len,
                          const char *passphrase, size_t passphrase_len,
                          const uint8_t salt[CEDS_SALT_SIZE],
                          const uint8_t iv[CEDS_IV_SIZE],
                          uint8_t **out, size_t *out_len) {
    return crypt_buffer(1, plain, plain_len, passphrase, passphrase_len,
                        salt, iv, out, out_len);
}

int crypto_decrypt_buffer(const uint8_t *cipher, size_t cipher_len,
                          const char *passphrase, size_t passphrase_len,
                          const uint8_t salt[CEDS_SALT_SIZE],
                          const uint8_t iv[CEDS_IV_SIZE],
                          uint8_t **out, size_t *out_len) {
    return crypt_buffer(0, cipher, cipher_len, passphrase, passphrase_len,
                        salt, iv, out, out_len);
}
