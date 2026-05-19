#include "fileformat.h"
#include "compress.h"
#include "io.h"
#include "profiling.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint32_t calc_checksum(const uint8_t *data, size_t size) {
    uint32_t csum = 0;
    for (size_t i = 0; i < size; i++) {
        csum ^= ((uint32_t)data[i]) << ((i % 4) * 8);
    }
    return csum;
}

int doc_save_mode(const char *path, const char *text, size_t text_size,
                  const char *title, const StyleEntry *styles, uint32_t style_count,
                  int use_mmap, int encrypt_payload,
                  const char *passphrase, size_t passphrase_len,
                  SaveMetrics *metrics) {
    size_t worst = RLE_WORST_CASE(text_size);
    uint8_t *comp_buf = NULL;
    uint8_t *stored_payload = NULL;
    uint8_t *payload = NULL;
    ssize_t comp_size;
    size_t stored_size;
    size_t style_sz = style_count * sizeof(StyleEntry);
    size_t crypto_sz = encrypt_payload ? sizeof(CryptoMetadata) : 0;
    size_t total_size;
    uint8_t *ptr;
    int result;
    FileHeader hdr;
    CryptoMetadata crypto_meta;

    if (metrics) memset(metrics, 0, sizeof(*metrics));
    if (encrypt_payload && (!passphrase || passphrase_len == 0)) {
        fprintf(stderr, "doc_save: se requiere una llave para cifrar\n");
        return -1;
    }

    comp_buf = malloc(worst);
    if (!comp_buf) {
        perror("doc_save: malloc comp_buf");
        return -1;
    }

    PROF_START(compress);
    comp_size = rle_compress((const uint8_t *)text, text_size, comp_buf, worst);
    PROF_END(compress);
    if (comp_size < 0) {
        fprintf(stderr, "doc_save: error en compresión RLE\n");
        free(comp_buf);
        return -1;
    }

    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, CEDS_MAGIC, 4);
    hdr.version = CEDS_VERSION;
    hdr.compression_type = COMPRESSION_RLE;
    hdr.flags = (style_count > 0) ? FLAG_RICH_TEXT : 0;
    hdr.original_size = (uint32_t)text_size;
    hdr.compressed_size = (uint32_t)comp_size;
    hdr.checksum = calc_checksum(comp_buf, (size_t)comp_size);
    hdr.style_count = style_count;
    if (title) strncpy(hdr.title, title, sizeof(hdr.title) - 1);

    stored_payload = comp_buf;
    stored_size = (size_t)comp_size;
    memset(&crypto_meta, 0, sizeof(crypto_meta));

    if (metrics) metrics->compression_ms = PROF_MS(compress);

    if (encrypt_payload) {
        if (crypto_random_bytes(crypto_meta.salt, sizeof(crypto_meta.salt)) < 0 ||
            crypto_random_bytes(crypto_meta.iv, sizeof(crypto_meta.iv)) < 0) {
            fprintf(stderr, "doc_save: no se pudieron generar salt/iv\n");
            free(comp_buf);
            return -1;
        }

        PROF_START(encrypt);
        if (crypto_encrypt_buffer(comp_buf, (size_t)comp_size,
                                  passphrase, passphrase_len,
                                  crypto_meta.salt, crypto_meta.iv,
                                  &stored_payload, &stored_size) < 0) {
            fprintf(stderr, "doc_save: error al cifrar el payload\n");
            free(comp_buf);
            return -1;
        }
        PROF_END(encrypt);

        hdr.flags |= FLAG_ENCRYPTED;
        header_set_encrypted_size(&hdr, (uint32_t)stored_size);
        header_set_crypto_type(&hdr, CRYPTO_AES_256_CBC);
        if (metrics) metrics->encryption_ms = PROF_MS(encrypt);
    } else {
        header_set_encrypted_size(&hdr, (uint32_t)stored_size);
        header_set_crypto_type(&hdr, CRYPTO_NONE);
    }

    total_size = sizeof(FileHeader) + style_sz + crypto_sz + stored_size;
    payload = malloc(total_size);
    if (!payload) {
        perror("doc_save: malloc payload");
        if (stored_payload != comp_buf) {
            secure_memzero(stored_payload, stored_size);
            free(stored_payload);
        }
        free(comp_buf);
        return -1;
    }

    ptr = payload;
    memcpy(ptr, &hdr, sizeof(FileHeader));
    ptr += sizeof(FileHeader);
    if (style_count > 0 && styles) {
        memcpy(ptr, styles, style_sz);
        ptr += style_sz;
    }
    if (encrypt_payload) {
        memcpy(ptr, &crypto_meta, sizeof(crypto_meta));
        ptr += sizeof(crypto_meta);
    }
    memcpy(ptr, stored_payload, stored_size);

    PROF_START(write);
    result = use_mmap ? io_write_mmap(path, payload, total_size)
                      : io_write_fd(path, payload, total_size);
    PROF_END(write);

    if (metrics) {
        metrics->write_ms = PROF_MS(write);
        metrics->compressed_size = (size_t)comp_size;
        metrics->encrypted_size = stored_size;
    }

    if (stored_payload != comp_buf) {
        secure_memzero(stored_payload, stored_size);
        free(stored_payload);
    }
    secure_memzero(comp_buf, (size_t)comp_size);
    free(comp_buf);
    free(payload);
    return result;
}

int doc_peek_header(const char *path, FileHeader *out) {
    int fd;
    ssize_t nread;

    if (!out) return -1;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    nread = read(fd, out, sizeof(*out));
    close(fd);
    if (nread != (ssize_t)sizeof(*out)) return -1;
    if (memcmp(out->magic, CEDS_MAGIC, 4) != 0) return -1;
    return 0;
}

int doc_load(const char *path, const char *passphrase, size_t passphrase_len,
             LoadedDocument *out) {
    size_t file_size;
    uint8_t *file_data = NULL;
    uint8_t *comp_buf = NULL;
    uint8_t *owned_comp_buf = NULL;
    size_t comp_size;
    size_t style_sz;
    size_t crypto_sz;
    size_t total_sz;
    const uint8_t *cursor;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    file_data = io_read_fd(path, &file_size);
    if (!file_data) return -1;
    if (file_size < sizeof(FileHeader)) {
        fprintf(stderr, "doc_load: archivo demasiado pequeño\n");
        free(file_data);
        return -1;
    }

    FileHeader *hdr = (FileHeader *)file_data;
    if (memcmp(hdr->magic, CEDS_MAGIC, 4) != 0) {
        fprintf(stderr, "doc_load: magic number inválido\n");
        free(file_data);
        return -1;
    }
    if (hdr->version != CEDS_VERSION) {
        fprintf(stderr, "doc_load: versión de formato no soportada\n");
        free(file_data);
        return -1;
    }
    if (hdr->compression_type != COMPRESSION_RLE) {
        fprintf(stderr, "doc_load: tipo de compresión no soportado\n");
        free(file_data);
        return -1;
    }
    if (hdr->style_count > 64) {
        fprintf(stderr, "doc_load: demasiados estilos\n");
        free(file_data);
        return -1;
    }

    style_sz = hdr->style_count * sizeof(StyleEntry);
    crypto_sz = (hdr->flags & FLAG_ENCRYPTED) ? sizeof(CryptoMetadata) : 0;
    total_sz = sizeof(FileHeader) + style_sz + crypto_sz + header_get_encrypted_size(hdr);
    if (total_sz != file_size || total_sz < sizeof(FileHeader)) {
        fprintf(stderr, "doc_load: tamaños inconsistentes en el archivo\n");
        free(file_data);
        return -1;
    }

    if ((hdr->flags & FLAG_RICH_TEXT) == 0 && hdr->style_count > 0) {
        fprintf(stderr, "doc_load: tabla de estilos inconsistente\n");
        free(file_data);
        return -1;
    }

    cursor = file_data + sizeof(FileHeader);
    if (hdr->style_count > 0) {
        out->styles = malloc(style_sz);
        if (!out->styles) {
            free(file_data);
            return -1;
        }
        memcpy(out->styles, cursor, style_sz);
        cursor += style_sz;
    }

    if (hdr->flags & FLAG_ENCRYPTED) {
        const CryptoMetadata *meta = (const CryptoMetadata *)cursor;
        cursor += sizeof(*meta);

        if (header_get_crypto_type(hdr) != CRYPTO_AES_256_CBC) {
            fprintf(stderr, "doc_load: cifrado no soportado\n");
            doc_free(out);
            free(file_data);
            return -1;
        }
        if (!passphrase || passphrase_len == 0) {
            fprintf(stderr, "doc_load: se requiere la llave de cifrado\n");
            doc_free(out);
            free(file_data);
            return -1;
        }
        if (crypto_decrypt_buffer(cursor, header_get_encrypted_size(hdr),
                                  passphrase, passphrase_len,
                                  meta->salt, meta->iv,
                                  &owned_comp_buf, &comp_size) < 0) {
            fprintf(stderr, "doc_load: fallo al descifrar (llave incorrecta o archivo corrupto)\n");
            doc_free(out);
            free(file_data);
            return -1;
        }
        if (comp_size != hdr->compressed_size) {
            fprintf(stderr, "doc_load: tamaño descifrado inconsistente\n");
            secure_memzero(owned_comp_buf, comp_size);
            free(owned_comp_buf);
            doc_free(out);
            free(file_data);
            return -1;
        }
        comp_buf = owned_comp_buf;
    } else {
        comp_buf = (uint8_t *)cursor;
        comp_size = hdr->compressed_size;
    }

    if (calc_checksum(comp_buf, comp_size) != hdr->checksum) {
        fprintf(stderr, "doc_load: checksum inválido — archivo corrupto\n");
        if (owned_comp_buf) {
            secure_memzero(owned_comp_buf, comp_size);
            free(owned_comp_buf);
        }
        doc_free(out);
        free(file_data);
        return -1;
    }

    out->text = malloc(hdr->original_size + 1);
    if (!out->text) {
        if (owned_comp_buf) {
            secure_memzero(owned_comp_buf, comp_size);
            free(owned_comp_buf);
        }
        doc_free(out);
        free(file_data);
        return -1;
    }

    ssize_t dec_size = rle_decompress(comp_buf, comp_size,
                                      (uint8_t *)out->text, hdr->original_size);
    if (dec_size < 0 || (uint32_t)dec_size != hdr->original_size) {
        fprintf(stderr, "doc_load: fallo en descompresión\n");
        if (owned_comp_buf) {
            secure_memzero(owned_comp_buf, comp_size);
            free(owned_comp_buf);
        }
        doc_free(out);
        free(file_data);
        return -1;
    }

    out->text[dec_size] = '\0';
    out->text_size = (size_t)dec_size;
    out->header = *hdr;

    if (owned_comp_buf) {
        secure_memzero(owned_comp_buf, comp_size);
        free(owned_comp_buf);
    }
    free(file_data);
    return 0;
}

void doc_free(LoadedDocument *doc) {
    if (!doc) return;
    free(doc->text);
    free(doc->styles);
    doc->text = NULL;
    doc->styles = NULL;
    doc->text_size = 0;
}
