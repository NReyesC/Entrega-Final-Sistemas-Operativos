/*
 * io.c - Implementación de I/O de bajo nivel con API POSIX.
 * Demuestra empíricamente la diferencia entre:
 *   1. write() con bloques alineados a página (4KB) → óptimo para el bus DMA
 *   2. mmap() → elimina el overhead de syscalls write() para archivos grandes
 *   3. Naive (bloques pequeños) → baseline para comparación con strace
 */

#include "io.h"
#include <fcntl.h>      /* open(), O_WRONLY, O_CREAT, etc.  */
#include <unistd.h>     /* write(), read(), close(), ftruncate() */
#include <sys/mman.h>   /* mmap(), munmap(), msync(), MAP_SHARED */
#include <sys/stat.h>   /* fstat(), struct stat */
#include <stdio.h>
#include <stdlib.h>     /* posix_memalign(), free() */
#include <string.h>     /* memcpy() */
#include <errno.h>

/* ─────────────────────────────────────────────────────────────────────────── */

int io_write_fd(const char *path, const uint8_t *data, size_t size) {
    /* Abrir el archivo destino (sin stdio → sin buffering de alto nivel) */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("io_write_fd: open");
        return -1;
    }

    /*
     * Reservar un buffer alineado a PAGE_SIZE (4096 bytes).
     *
     * ¿Por qué alinear?
     * El controlador DMA transfiere datos en múltiplos de páginas.
     * Un buffer no alineado puede cruzar un límite de página, obligando
     * al kernel a hacer una copia extra (bounce buffer), lo que añade
     * latencia y carga al bus de memoria.
     *
     * posix_memalign garantiza que la dirección devuelta es múltiplo de
     * PAGE_SIZE. Es equivalente a valloc() pero más portable (POSIX).
     */
    void *aligned_buf = NULL;
    if (posix_memalign(&aligned_buf, PAGE_SIZE, PAGE_SIZE) != 0) {
        perror("io_write_fd: posix_memalign");
        close(fd);
        return -1;
    }

    /*
     * Escribir en bloques de PAGE_SIZE (4KB).
     * Cada llamada write() es una syscall (context switch User→Kernel).
     * Maximizamos la cantidad de datos por syscall para reducir el total.
     *
     * Total de write() calls ≈ ceil(size / PAGE_SIZE)
     * Para datos comprimidos de 15 MB: ~3750 calls.
     * Para datos planos de 50 MB:     ~12800 calls.
     */
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = (size - offset < PAGE_SIZE) ? (size - offset) : PAGE_SIZE;
        memcpy(aligned_buf, data + offset, chunk);

        ssize_t written = write(fd, aligned_buf, chunk);
        if (written < 0) {
            perror("io_write_fd: write");
            free(aligned_buf);
            close(fd);
            return -1;
        }
        offset += (size_t)written;
    }

    free(aligned_buf);
    close(fd);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */

int io_write_mmap(const char *path, const uint8_t *data, size_t size) {
    /*
     * mmap(): alternativa a write() para archivos grandes.
     *
     * Mecanismo:
     *   1. El SO mapea el archivo directamente en el espacio de direcciones
     *      virtual del proceso (sin copiar datos todavía).
     *   2. Al escribir en el puntero, se trabaja con el Page Cache del kernel.
     *   3. msync(MS_SYNC) forza el flush del Page Cache al dispositivo.
     *
     * Ventaja: memcpy() opera en espacio de usuario sin context switches
     * por cada bloque. Ideal cuando el archivo ya está en el Page Cache.
     */

    /* O_RDWR requerido: mmap MAP_SHARED necesita acceso de lectura y escritura */
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("io_write_mmap: open");
        return -1;
    }

    /* ftruncate: establece el tamaño del archivo ANTES de mapearlo.
     * Sin esto, mmap mapea un archivo vacío y cualquier escritura da SIGBUS. */
    if (ftruncate(fd, (off_t)size) < 0) {
        perror("io_write_mmap: ftruncate");
        close(fd);
        return -1;
    }

    /* Mapear el archivo en el espacio de direcciones del proceso.
     * MAP_SHARED: las escrituras se reflejan en el archivo (no solo en RAM).
     * PROT_READ|PROT_WRITE: necesario para memcpy de lectura/escritura.
     * offset=0: mapear desde el inicio del archivo. */
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("io_write_mmap: mmap");
        close(fd);
        return -1;
    }

    /* Escribir datos al espacio mapeado (sin syscalls adicionales por bloque) */
    memcpy(map, data, size);

    /* msync(MS_SYNC): garantiza que los datos llegaron al dispositivo físico
     * antes de retornar. Sin esto, los cambios podrían perderse en un crash. */
    if (msync(map, size, MS_SYNC) < 0) {
        perror("io_write_mmap: msync");
    }

    munmap(map, size);
    close(fd);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */

uint8_t *io_read_fd(const char *path, size_t *out_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("io_read_fd: open");
        return NULL;
    }

    /* fstat(): obtener metadatos del archivo sin leerlo.
     * st.st_size da el tamaño exacto para reservar el buffer correcto. */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("io_read_fd: fstat");
        close(fd);
        return NULL;
    }
    size_t file_size = (size_t)st.st_size;

    uint8_t *buf = malloc(file_size);
    if (!buf) {
        perror("io_read_fd: malloc");
        close(fd);
        return NULL;
    }

    /* Leer en bloques de PAGE_SIZE */
    size_t offset = 0;
    while (offset < file_size) {
        size_t to_read = (file_size - offset < PAGE_SIZE)
                         ? (file_size - offset) : PAGE_SIZE;
        ssize_t nread = read(fd, buf + offset, to_read);
        if (nread < 0) {
            perror("io_read_fd: read");
            free(buf);
            close(fd);
            return NULL;
        }
        if (nread == 0) break;  /* EOF inesperado */
        offset += (size_t)nread;
    }

    close(fd);
    *out_size = file_size;
    return buf;
}

/* ─────────────────────────────────────────────────────────────────────────── */

int io_write_plain_naive(const char *path, const char *text, size_t size) {
    /*
     * Enfoque "naïve": simula el comportamiento de fputc()/fprintf().
     * Escribe en bloques muy pequeños (64 bytes) sin:
     *   - Compresión previa
     *   - Alineación a página
     *   - Consideración del tamaño de página del SO
     *
     * Esto genera muchas más syscalls write():
     *   Para 50 MB con bloques de 64 B: ~781,250 write() calls
     *   vs enfoque optimizado con compresión + 4KB: ~3,750 write() calls
     *   → 200x más context switches hacia el kernel
     */
    #define NAIVE_BLOCK_SIZE 64  /* bytes por write() - simula buffering mínimo */

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("io_write_plain_naive: open");
        return -1;
    }

    size_t offset = 0;
    while (offset < size) {
        size_t chunk = (size - offset < NAIVE_BLOCK_SIZE)
                       ? (size - offset) : NAIVE_BLOCK_SIZE;
        if (write(fd, text + offset, chunk) < 0) {
            perror("io_write_plain_naive: write");
            close(fd);
            return -1;
        }
        offset += chunk;
    }

    close(fd);
    return 0;
}
