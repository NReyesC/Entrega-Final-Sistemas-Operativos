#ifndef COMPRESS_H
#define COMPRESS_H

/*
 * compress.h - Compresión Run-Length Encoding (RLE) en User Space.
 *
 * El objetivo es comprimir el texto ANTES de invocar cualquier syscall
 * (write/mmap), reduciendo el volumen de datos que viaja al bus I/O.
 *
 * Formato de encoding:
 *   Byte literal:           [byte]              (si byte != ESCAPE)
 *   ESCAPE literal:         [ESCAPE][0x00]
 *   Run de N bytes iguales: [ESCAPE][N][byte]   (N >= 3, N <= 255)
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Byte de escape: marca el inicio de una secuencia RLE */
#define RLE_ESCAPE 0xFE

/*
 * Cota superior del tamaño comprimido (peor caso: ningún byte se comprime
 * y todos son ESCAPE → cada byte se expande a 2 bytes).
 */
#define RLE_WORST_CASE(n) ((n) * 2 + 16)

/*
 * rle_compress - Comprime 'src_size' bytes de 'src' en 'dst'.
 * 'dst' debe tener al menos RLE_WORST_CASE(src_size) bytes reservados.
 *
 * Retorna: bytes escritos en 'dst' (>= 0), o -1 en error.
 */
ssize_t rle_compress(const uint8_t *src, size_t src_size,
                     uint8_t *dst,       size_t dst_size);

/*
 * rle_decompress - Descomprime 'src_size' bytes de 'src' en 'dst'.
 * 'dst' debe tener al menos 'dst_size' bytes (= original_size del header).
 *
 * Retorna: bytes escritos en 'dst' (>= 0), o -1 en error.
 */
ssize_t rle_decompress(const uint8_t *src, size_t src_size,
                       uint8_t *dst,       size_t dst_size);

#endif /* COMPRESS_H */
