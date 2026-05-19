#ifndef FILEFORMAT_H
#define FILEFORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crypto.h"

#define CEDS_MAGIC    "CEDS"
#define CEDS_VERSION  2

#define COMPRESSION_NONE  0
#define COMPRESSION_RLE   1

#define FLAG_RICH_TEXT  (1u << 0)
#define FLAG_ENCRYPTED  (1u << 1)

#define CRYPTO_NONE          0
#define CRYPTO_AES_256_CBC   1

typedef struct __attribute__((packed)) {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  compression_type;
    uint16_t flags;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t checksum;
    uint32_t style_count;
    char     title[32];
    uint8_t  reserved[8];
} FileHeader;

_Static_assert(sizeof(FileHeader) == 64,
               "FileHeader debe ser exactamente 64 bytes (revisar padding)");

typedef struct __attribute__((packed)) {
    uint32_t offset;
    uint32_t length;
    uint8_t  bold;
    uint8_t  italic;
    uint8_t  underline;
    uint8_t  reserved;
    uint32_t color;
} StyleEntry;

_Static_assert(sizeof(StyleEntry) == 16,
               "StyleEntry debe ser exactamente 16 bytes (revisar padding)");
_Static_assert(sizeof(CryptoMetadata) == 32,
               "CryptoMetadata debe ser exactamente 32 bytes");

typedef struct {
    char       *text;
    size_t      text_size;
    FileHeader  header;
    StyleEntry *styles;
} LoadedDocument;

typedef struct {
    double compression_ms;
    double encryption_ms;
    double write_ms;
    size_t compressed_size;
    size_t encrypted_size;
} SaveMetrics;

static inline void header_set_encrypted_size(FileHeader *hdr, uint32_t size) {
    memcpy(hdr->reserved, &size, sizeof(size));
}

static inline uint32_t header_get_encrypted_size(const FileHeader *hdr) {
    uint32_t size = 0;
    memcpy(&size, hdr->reserved, sizeof(size));
    return size;
}

static inline void header_set_crypto_type(FileHeader *hdr, uint8_t type) {
    hdr->reserved[4] = type;
}

static inline uint8_t header_get_crypto_type(const FileHeader *hdr) {
    return hdr->reserved[4];
}

int doc_save_mode(const char *path, const char *text, size_t text_size,
                  const char *title, const StyleEntry *styles, uint32_t style_count,
                  int use_mmap, int encrypt_payload,
                  const char *passphrase, size_t passphrase_len,
                  SaveMetrics *metrics);
int doc_load(const char *path, const char *passphrase, size_t passphrase_len,
             LoadedDocument *out);
int doc_peek_header(const char *path, FileHeader *out);
void doc_free(LoadedDocument *doc);
uint32_t calc_checksum(const uint8_t *data, size_t size);

#endif /* FILEFORMAT_H */
