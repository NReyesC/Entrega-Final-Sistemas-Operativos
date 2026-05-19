#ifndef IO_H
#define IO_H

/*
 * io.h - Capa de I/O de bajo nivel usando la API POSIX de Linux.
 *
 * Justificación del diseño (Criterio 1 - Arquitectura I/O):
 *
 *   POSIX fd (open/read/write) vs stdio (fopen/fread/fwrite):
 *   - stdio agrega una capa de buffering en User Space sobre los fd.
 *   - Para I/O de alta precisión en SO, los fd directos permiten controlar
 *     exactamente cuándo y cuánto se escribe, evitando doble-buffering.
 *
 *   Buffers alineados a PAGE_SIZE (4096 bytes):
 *   - El kernel gestiona memoria en páginas de 4096 bytes.
 *   - Un buffer alineado permite que el DMA copie datos sin copias extra
 *     (zero-copy paths). Un buffer no alineado puede cruzar límites de
 *     página y requerir que el kernel haga copias intermedias.
 *
 *   write() en bloques vs mmap():
 *   - write(): el proceso llama al kernel, que copia de User Space al
 *     Page Cache. Cada llamada es un context switch (User→Kernel→User).
 *   - mmap(): el kernel mapea el archivo directamente en el espacio virtual
 *     del proceso. Se escribe con memcpy; el kernel sincroniza con msync().
 *     Ideal para archivos grandes donde el overhead de syscalls domina.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Tamaño de página en x86-64 Linux = 4096 bytes */
#define PAGE_SIZE 4096

/*
 * io_write_fd - Escribe datos usando descriptores de archivo POSIX.
 * Usa posix_memalign() para alinear el buffer interno a PAGE_SIZE,
 * y escribe en bloques de PAGE_SIZE para maximizar eficiencia del bus DMA.
 *
 * Pipeline:
 *   open(O_WRONLY|O_CREAT) → posix_memalign(4096) → write(4KB) × N → close()
 *
 * Retorna: 0 en éxito, -1 en error.
 */
int io_write_fd(const char *path, const uint8_t *data, size_t size);

/*
 * io_write_mmap - Escribe datos usando mapeo en memoria (mmap).
 * Evita el overhead de múltiples syscalls write(): el kernel sincroniza
 * el page cache con el archivo en msync().
 *
 * Pipeline:
 *   open(O_RDWR|O_CREAT) → ftruncate(size) → mmap(MAP_SHARED) → memcpy → msync → munmap → close()
 *
 * Retorna: 0 en éxito, -1 en error.
 */
int io_write_mmap(const char *path, const uint8_t *data, size_t size);

/*
 * io_read_fd - Lee un archivo completo en un buffer heap.
 * El caller es responsable de liberar el buffer con free().
 *
 * Retorna: puntero al buffer o NULL en error. Tamaño en *out_size.
 */
uint8_t *io_read_fd(const char *path, size_t *out_size);

/*
 * io_write_plain_naive - Escribe texto plano en bloques pequeños (64 bytes).
 * Simula el comportamiento de fputc()/fprintf(): muchas llamadas pequeñas
 * sin compresión ni alineación. Usado solo para comparación en benchmark.
 *
 * Retorna: 0 en éxito, -1 en error.
 */
int io_write_plain_naive(const char *path, const char *text, size_t size);

#endif /* IO_H */
