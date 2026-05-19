#include "compress.h"
#include <errno.h>

/*
 * compress.c - Implementación de RLE (Run-Length Encoding).
 *
 * Algoritmo de compresión:
 *   - Detecta runs (secuencias de bytes idénticos consecutivos).
 *   - Si el run tiene 3 o más bytes: codifica como [ESCAPE][N][byte].
 *   - Si el byte es ESCAPE: codifica como [ESCAPE][0x00] (escape literal).
 *   - Cualquier otro byte: se copia literalmente.
 *
 * Eficiencia real en texto: el texto en español tiene muchos espacios
 * y palabras repetidas. Los espacios repetidos y tabulaciones se comprimen
 * bien. Para texto variado, la ganancia es modesta (~10-30%).
 * Para archivos con patrones repetitivos la ganancia puede superar 70%.
 */

ssize_t rle_compress(const uint8_t *src, size_t src_size,
                     uint8_t *dst,       size_t dst_size) {
    size_t i = 0;  /* índice de lectura en src */
    size_t j = 0;  /* índice de escritura en dst */

    while (i < src_size) {
        uint8_t byte = src[i];

        /* Caso especial: el byte de escape debe ser codificado explícitamente */
        if (byte == RLE_ESCAPE) {
            if (j + 2 > dst_size) return -1;  /* sin espacio en dst */
            dst[j++] = RLE_ESCAPE;
            dst[j++] = 0x00;   /* count=0 → ESCAPE literal */
            i++;
            continue;
        }

        /* Contar cuántos bytes consecutivos iguales hay (run) */
        size_t run = 1;
        while (i + run < src_size &&
               src[i + run] == byte &&
               run < 255) {
            run++;
        }

        if (run >= 3) {
            /* Run encoding: 3 bytes representan hasta 255 bytes de datos */
            if (j + 3 > dst_size) return -1;
            dst[j++] = RLE_ESCAPE;
            dst[j++] = (uint8_t)run;
            dst[j++] = byte;
            i += run;
        } else {
            /* Bytes literales (1 o 2 repeticiones no valen el overhead de 3 bytes) */
            for (size_t k = 0; k < run; k++) {
                if (j + 1 > dst_size) return -1;
                dst[j++] = byte;
            }
            i += run;
        }
    }

    return (ssize_t)j;
}

ssize_t rle_decompress(const uint8_t *src, size_t src_size,
                       uint8_t *dst,       size_t dst_size) {
    size_t i = 0;  /* índice de lectura */
    size_t j = 0;  /* índice de escritura */

    while (i < src_size) {
        uint8_t byte = src[i++];

        if (byte != RLE_ESCAPE) {
            /* Byte literal: copiar directamente */
            if (j + 1 > dst_size) return -1;
            dst[j++] = byte;
        } else {
            /* Secuencia de escape: leer el count */
            if (i >= src_size) return -1;  /* datos truncados */
            uint8_t count = src[i++];

            if (count == 0x00) {
                /* ESCAPE literal */
                if (j + 1 > dst_size) return -1;
                dst[j++] = RLE_ESCAPE;
            } else {
                /* Run: leer el byte a repetir y expandir */
                if (i >= src_size) return -1;
                uint8_t val = src[i++];
                if (j + count > dst_size) return -1;
                for (uint8_t k = 0; k < count; k++) {
                    dst[j++] = val;
                }
            }
        }
    }

    return (ssize_t)j;
}
