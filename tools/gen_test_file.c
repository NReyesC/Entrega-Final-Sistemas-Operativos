/*
 * gen_test_file.c - Generador de archivos de texto para benchmarks.
 *
 * Genera texto con vocabulario de Sistemas Operativos para simular
 * un documento real. El texto tiene patrones repetidos que el RLE
 * puede comprimir eficientemente.
 *
 * Uso: ./bin/gen_test <MB> <archivo_salida>
 * Ej:  ./bin/gen_test 5 test_5mb.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Frases de relleno (texto humano + bloques de espacios para favorecer RLE) */
static const char *PHRASES[] = {
    "el sistema operativo gestiona los recursos del hardware",
    "la memoria virtual permite aislar los procesos entre sí",
    "el bus de datos conecta la CPU con los dispositivos",
    "las llamadas al sistema son la interfaz entre User y Kernel Space",
    "el scheduler decide qué proceso se ejecuta en la CPU",
    "la compresión reduce el volumen de datos en el bus de I/O"
};
#define PHRASE_COUNT (int)(sizeof(PHRASES) / sizeof(PHRASES[0]))

#define SPACE_RUN 192

static void write_spaces(FILE *f, size_t count, size_t *written) {
    static const char blanks[] =
        "                                                                "
        "                                                                ";
    while (count > 0) {
        size_t chunk = count < (sizeof(blanks) - 1) ? count : (sizeof(blanks) - 1);
        fwrite(blanks, 1, chunk, f);
        *written += chunk;
        count -= chunk;
    }
}

int main(int argc, char *argv[]) {
    size_t target_mb = 5;
    const char *outfile = "test_document.txt";

    if (argc >= 2) target_mb = (size_t)atol(argv[1]);
    if (argc >= 3) outfile   = argv[2];

    size_t target_bytes = target_mb * 1024 * 1024;

    FILE *f = fopen(outfile, "w");
    if (!f) { perror("fopen"); return 1; }

    size_t written   = 0;
    size_t line_count = 0;

    printf("Generando %zu MB en '%s'...\n", target_mb, outfile);

    while (written < target_bytes) {
        /* Bloque de espacios inicial para que RLE comprima una parte grande del archivo */
        write_spaces(f, SPACE_RUN, &written);

        /* Línea de texto variable con vocabulario de SO */
        const char *phrase = PHRASES[line_count % PHRASE_COUNT];
        size_t plen = strlen(phrase);
        fwrite(phrase, 1, plen, f);
        written += plen;

        /* Bloque de espacios final: el mayor beneficio de RLE viene de aquí */
        write_spaces(f, SPACE_RUN, &written);
        fputc('\n', f);
        written++;

        /* Separador adicional, altamente compresible */
        if ((line_count % 4) == 3) {
            write_spaces(f, SPACE_RUN * 2, &written);
            fputc('\n', f);
            written++;
        }

        line_count++;
    }

    fclose(f);

    /* Reportar estadísticas */
    printf("Generado: %zu bytes (%.2f MB)\n", written, written / 1024.0 / 1024.0);
    printf("Usa con: ./bin/editor %s\n", outfile);
    return 0;
}
