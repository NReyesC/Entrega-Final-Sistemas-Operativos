/*
 * main.c - Editor de texto CEDS con compresión + cifrado simétrico.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "crypto.h"
#include "editor.h"
#include "fileformat.h"
#include "io.h"
#include "profiling.h"

#define VERSION   "2.0"
#define MAX_LINE  4096

static GapBuffer *g_buf       = NULL;
static char       g_title[32] = "Sin título";
static char       g_path[256] = "";
static int        g_modified  = 0;

static StyleEntry g_styles[64];
static uint32_t   g_style_count = 0;

static size_t file_size_of(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) ? (size_t)st.st_size : 0;
}

static const char *basename_of(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static void cmd_new(const char *title) {
    if (g_buf) gb_free(g_buf);
    g_buf = gb_new();
    if (!g_buf) {
        fprintf(stderr, "ERROR: no se pudo inicializar el buffer\n");
        return;
    }

    strncpy(g_title, title && title[0] ? title : "Sin título", sizeof(g_title) - 1);
    g_title[sizeof(g_title) - 1] = '\0';
    g_path[0] = '\0';
    g_modified = 0;
    g_style_count = 0;
    memset(g_styles, 0, sizeof(g_styles));
    printf("Nuevo documento creado: '%s'\n", g_title);
}

static void print_save_summary(const char *dest, int use_mmap, int encrypt_payload,
                               size_t text_size, const SaveMetrics *metrics) {
    size_t on_disk = file_size_of(dest);
    printf("Guardado: '%s' [%s]\n", dest, use_mmap ? "mmap" : "write(4KB)");
    printf("  Texto original   : %zu bytes\n", text_size);
    printf("  Tamaño RLE       : %zu bytes\n", metrics->compressed_size);
    if (encrypt_payload) {
        printf("  Tamaño cifrado   : %zu bytes\n", metrics->encrypted_size);
    }
    printf("  Archivo en disco : %zu bytes (%.1f%% del original)\n",
           on_disk, text_size ? on_disk * 100.0 / text_size : 0.0);
    printf("  CPU compresión   : %.3f ms\n", metrics->compression_ms);
    if (encrypt_payload) {
        printf("  CPU cifrado      : %.3f ms\n", metrics->encryption_ms);
        printf("  CPU total algos  : %.3f ms\n", metrics->compression_ms + metrics->encryption_ms);
    }
    printf("  Espera I/O       : %.3f ms\n", metrics->write_ms);
}

static int request_passphrase(SecureBuffer *key, const char *purpose) {
    if (secure_prompt_passphrase(purpose, key) < 0) {
        printf("No se pudo leer la llave criptográfica.\n");
        return -1;
    }
    return 0;
}

static void cmd_open(const char *path) {
    if (!path || !path[0]) {
        printf("Uso: :open <archivo>\n");
        return;
    }

    size_t plen = strlen(path);
    if (plen > 5 && strcmp(path + plen - 5, ".ceds") == 0) {
        LoadedDocument doc;
        FileHeader header;
        SecureBuffer key;
        const char *passphrase = NULL;
        size_t passphrase_len = 0;

        memset(&key, 0, sizeof(key));
        if (doc_peek_header(path, &header) < 0) {
            printf("Error al leer el encabezado de '%s'\n", path);
            return;
        }

        if (header.flags & FLAG_ENCRYPTED) {
            if (request_passphrase(&key, "Llave para abrir el documento: ") < 0) {
                return;
            }
            passphrase = key.data;
            passphrase_len = key.len;
        }

        PROF_START(load);
        if (doc_load(path, passphrase, passphrase_len, &doc) < 0) {
            secure_buffer_destroy(&key);
            printf("Error al abrir '%s'\n", path);
            return;
        }
        PROF_END(load);
        secure_buffer_destroy(&key);

        if (g_buf) gb_free(g_buf);
        g_buf = gb_new();
        gb_insert_str(g_buf, doc.text, doc.text_size);

        strncpy(g_title, doc.header.title, sizeof(g_title) - 1);
        g_title[sizeof(g_title) - 1] = '\0';
        strncpy(g_path, path, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        g_modified = 0;
        g_style_count = 0;
        memset(g_styles, 0, sizeof(g_styles));
        if (doc.styles && doc.header.style_count > 0) {
            g_style_count = doc.header.style_count > 64 ? 64 : doc.header.style_count;
            memcpy(g_styles, doc.styles, g_style_count * sizeof(StyleEntry));
        }

        printf("Abierto: '%s'\n", path);
        printf("  Título             : %s\n", g_title);
        printf("  Tamaño original    : %u bytes\n", doc.header.original_size);
        printf("  Tamaño comprimido  : %u bytes\n", doc.header.compressed_size);
        if (doc.header.flags & FLAG_ENCRYPTED) {
            printf("  Tamaño cifrado     : %u bytes\n", header_get_encrypted_size(&doc.header));
        }
        printf("  Pipeline           : %s\n",
               (doc.header.flags & FLAG_ENCRYPTED)
                 ? "Comprimir -> Encriptar -> write()"
                 : "Solo compresión" );
        PROF_PRINT(load, "  Tiempo de carga");
        doc_free(&doc);
    } else {
        size_t fsize;
        uint8_t *data = io_read_fd(path, &fsize);
        if (!data) {
            printf("Error al leer '%s'\n", path);
            return;
        }

        if (g_buf) gb_free(g_buf);
        g_buf = gb_new();
        gb_insert_str(g_buf, (char *)data, fsize);
        free(data);

        strncpy(g_title, basename_of(path), sizeof(g_title) - 1);
        g_title[sizeof(g_title) - 1] = '\0';
        strncpy(g_path, path, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        g_modified = 0;
        printf("Importado: '%s' (%zu bytes de texto plano)\n", path, fsize);
    }
}

static void cmd_show(void) {
    if (!g_buf || gb_text_size(g_buf) == 0) {
        printf("[Documento vacío — usa :append para escribir]\n");
        return;
    }
    char *text = gb_get_text(g_buf);
    if (!text) return;
    printf("─── %s ───\n%s\n────\n", g_title, text);
    free(text);
}

static void cmd_lines(void) {
    if (!g_buf) return;
    char *text = gb_get_text(g_buf);
    if (!text) return;

    printf("─── %s (con números de línea) ───\n", g_title);
    int line = 1;
    printf("%4d │ ", line);
    for (size_t i = 0; text[i]; i++) {
        putchar(text[i]);
        if (text[i] == '\n' && text[i + 1]) printf("%4d │ ", ++line);
    }
    printf("────\nTotal: %d línea(s), %zu bytes\n", line, gb_text_size(g_buf));
    free(text);
}

static void cmd_append(void) {
    if (!g_buf) {
        printf("Primero crea un documento con :new\n");
        return;
    }

    printf("Modo APPEND — Escribe tu texto. Línea ':end' para terminar.\n");
    gb_move_to(g_buf, gb_text_size(g_buf));

    char line[MAX_LINE];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strncmp(line, ":end", 4) == 0) break;
        gb_insert_str(g_buf, line, strlen(line));
        g_modified = 1;
    }
    printf("Texto agregado. Tamaño total: %zu bytes\n", gb_text_size(g_buf));
}

static void cmd_delete_line(int line_num) {
    if (!g_buf || line_num < 1) return;
    char *text = gb_get_text(g_buf);
    if (!text) return;

    int cur_line = 1;
    size_t start = 0;

    for (size_t i = 0; text[i]; i++) {
        if (cur_line == line_num) {
            size_t end = i;
            while (text[end] && text[end] != '\n') end++;
            if (text[end] == '\n') end++;

            size_t old_size = gb_text_size(g_buf);
            size_t new_size = old_size - (end - start);
            char *new_text = malloc(new_size + 1);
            if (!new_text) {
                free(text);
                return;
            }

            memcpy(new_text, text, start);
            memcpy(new_text + start, text + end, old_size - end);
            new_text[new_size] = '\0';

            gb_free(g_buf);
            g_buf = gb_new();
            gb_insert_str(g_buf, new_text, new_size);
            free(new_text);
            g_modified = 1;
            printf("Línea %d eliminada.\n", line_num);
            break;
        }
        if (text[i] == '\n') {
            cur_line++;
            start = i + 1;
        }
    }
    free(text);
}

static void cmd_save(const char *path, int use_mmap, int encrypt_payload) {
    const char *dest;
    char *text;
    size_t text_size;
    SaveMetrics metrics;
    SecureBuffer key;
    const char *passphrase = NULL;
    size_t passphrase_len = 0;
    int ret;

    if (!g_buf) {
        printf("No hay documento abierto\n");
        return;
    }

    dest = (path && path[0]) ? path : g_path;
    if (!dest || !dest[0]) {
        printf("Especifica un nombre: %s <archivo.ceds>\n", encrypt_payload ? ":save" : ":savec");
        return;
    }

    text = gb_get_text(g_buf);
    if (!text) return;
    text_size = strlen(text);
    memset(&metrics, 0, sizeof(metrics));
    memset(&key, 0, sizeof(key));

    if (encrypt_payload) {
        if (request_passphrase(&key, "Llave para cifrar el documento: ") < 0) {
            free(text);
            return;
        }
        passphrase = key.data;
        passphrase_len = key.len;
    }

    ret = doc_save_mode(dest, text, text_size, g_title,
                        g_style_count ? g_styles : NULL, g_style_count,
                        use_mmap, encrypt_payload,
                        passphrase, passphrase_len,
                        &metrics);

    secure_buffer_destroy(&key);
    free(text);

    if (ret == 0) {
        strncpy(g_path, dest, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        g_modified = 0;
        print_save_summary(dest, use_mmap, encrypt_payload, text_size, &metrics);
    } else {
        printf("Error al guardar '%s'\n", dest);
    }
}

static void cmd_add_style(uint32_t offset, uint32_t length,
                          int bold, int italic, int underline, uint32_t color) {
    if (g_style_count >= 64) {
        printf("Límite de estilos alcanzado (64)\n");
        return;
    }
    StyleEntry *s = &g_styles[g_style_count++];
    s->offset = offset;
    s->length = length;
    s->bold = (uint8_t)bold;
    s->italic = (uint8_t)italic;
    s->underline = (uint8_t)underline;
    s->reserved = 0;
    s->color = color;
    printf("Estilo agregado #%u: offset=%u len=%u B=%d I=%d U=%d\n",
           g_style_count, offset, length, bold, italic, underline);
}

static void cmd_benchmark(void) {
    char *text;
    size_t size;
    SaveMetrics comp_metrics;
    SaveMetrics enc_metrics;
    SecureBuffer key;
    size_t sz_naive;
    size_t sz_comp;
    size_t sz_enc;

    if (!g_buf || gb_text_size(g_buf) == 0) {
        printf("El documento está vacío. Usa :append para agregar contenido.\n");
        return;
    }

    text = gb_get_text(g_buf);
    if (!text) return;
    size = strlen(text);
    memset(&comp_metrics, 0, sizeof(comp_metrics));
    memset(&enc_metrics, 0, sizeof(enc_metrics));
    memset(&key, 0, sizeof(key));

    printf("\n══════════════════════════════════════════════════════════════════════\n");
    printf(" Reto Final — Benchmark Espacio, Tiempo y Seguridad\n");
    printf(" Archivo en RAM: %zu bytes (%.2f KB)\n", size, size / 1024.0);
    printf("══════════════════════════════════════════════════════════════════════\n\n");

    PROF_START(plain);
    io_write_plain_naive("/tmp/ceds_bench_plain.txt", text, size);
    PROF_END(plain);

    if (doc_save_mode("/tmp/ceds_bench_comp.ceds", text, size, "bench",
                      NULL, 0, 0, 0, NULL, 0, &comp_metrics) < 0) {
        printf("Fallo el benchmark de compresión.\n");
        free(text);
        return;
    }

    if (request_passphrase(&key, "Llave para benchmark cifrado: ") < 0) {
        free(text);
        return;
    }

    if (doc_save_mode("/tmp/ceds_bench_secure.ceds", text, size, "bench",
                      NULL, 0, 0, 1, key.data, key.len, &enc_metrics) < 0) {
        secure_buffer_destroy(&key);
        printf("Fallo el benchmark de cifrado.\n");
        free(text);
        return;
    }
    secure_buffer_destroy(&key);

    sz_naive = file_size_of("/tmp/ceds_bench_plain.txt");
    sz_comp = file_size_of("/tmp/ceds_bench_comp.ceds");
    sz_enc = file_size_of("/tmp/ceds_bench_secure.ceds");

    printf("┌──────────────────────────────┬──────────────┬──────────────┬──────────────┐\n");
    printf("│ Métrica                      │ A. Clásico   │ B. Compresión│ C. Comp+Enc  │\n");
    printf("├──────────────────────────────┼──────────────┼──────────────┼──────────────┤\n");
    printf("│ Tamaño transmitido (I/O)     │ %10zu B │ %10zu B │ %10zu B │\n", sz_naive, sz_comp, sz_enc);
    printf("│ CPU compresión               │ %10.3f │ %10.3f │ %10.3f │\n", 0.0, comp_metrics.compression_ms, enc_metrics.compression_ms);
    printf("│ CPU encriptación             │ %10.3f │ %10.3f │ %10.3f │\n", 0.0, 0.0, enc_metrics.encryption_ms);
    printf("│ CPU total algoritmos         │ %10.3f │ %10.3f │ %10.3f │\n", 0.0, comp_metrics.compression_ms, enc_metrics.compression_ms + enc_metrics.encryption_ms);
    printf("│ Espera I/O                   │ %10.3f │ %10.3f │ %10.3f │\n", PROF_MS(plain), comp_metrics.write_ms, enc_metrics.write_ms);
    printf("│ Tiempo total wall-clock      │ %10.3f │ %10.3f │ %10.3f │\n", PROF_MS(plain), comp_metrics.compression_ms + comp_metrics.write_ms, enc_metrics.compression_ms + enc_metrics.encryption_ms + enc_metrics.write_ms);
    printf("└──────────────────────────────┴──────────────┴──────────────┴──────────────┘\n\n");

    printf("Conclusión arquitectónica:\n");
    printf("  • Se comprime primero y se cifra después; invertir el orden aumenta la entropía y rompe la compresión.\n");
    printf("  • El archivo cifrado conserva casi todo el ahorro de I/O; el sobrecosto viene del CPU y del padding AES.\n");
    printf("  • La llave se toma en runtime, se bloquea en RAM con mlock() cuando el kernel lo permite y se destruye después de usarse.\n\n");

    free(text);
}

static void cmd_info(void) {
    printf("Documento : %s%s\n", g_title, g_modified ? " [modificado]" : "");
    printf("Archivo   : %s\n", g_path[0] ? g_path : "(sin guardar)");
    printf("Texto     : %zu bytes, ", g_buf ? gb_text_size(g_buf) : 0UL);
    if (g_buf) {
        char *t = gb_get_text(g_buf);
        int lines = 0;
        for (size_t i = 0; t && t[i]; i++) if (t[i] == '\n') lines++;
        printf("%d línea(s)\n", lines + 1);
        free(t);
    } else {
        printf("0 líneas\n");
    }
    printf("Estilos   : %u entradas\n", g_style_count);
    printf("FileHeader: %zu bytes | StyleEntry: %zu bytes | CryptoMetadata: %zu bytes\n",
           sizeof(FileHeader), sizeof(StyleEntry), sizeof(CryptoMetadata));
}

static void print_help(void) {
    printf("\nComandos disponibles:\n");
    printf("  :new [título]            Nuevo documento\n");
    printf("  :open <archivo>          Abrir .ceds o importar .txt\n");
    printf("  :save [archivo.ceds]     Guardar comprimido + cifrado (write 4KB)\n");
    printf("  :savem [archivo.ceds]    Guardar comprimido + cifrado (mmap)\n");
    printf("  :savec [archivo.ceds]    Guardar solo comprimido (sin cifrar)\n");
    printf("  :plain <archivo.txt>     Guardar texto plano (baseline inseguro)\n");
    printf("  :benchmark               Comparar A vs B vs C y separar CPU de compresión/cifrado\n");
    printf("  :show                    Mostrar contenido\n");
    printf("  :lines                   Mostrar con números de línea\n");
    printf("  :append                  Modo escritura (termina con :end)\n");
    printf("  :delete <N>              Eliminar línea N\n");
    printf("  :style <off> <len> <b> <i> <u> <color>  Agregar estilo\n");
    printf("  :info                    Información del documento\n");
    printf("  :help                    Esta ayuda\n");
    printf("  :quit / :q               Salir\n\n");
    printf("Nota: la llave no se pasa por argv. Se pide en runtime o se toma de CEDS_KEY para benchmarks automatizados.\n\n");
}

int main(int argc, char *argv[]) {
    printf("CEDS Editor v%s — Triángulo de Hierro: Espacio, Tiempo y Seguridad\n", VERSION);
    printf("Escribe ':help' para ver los comandos.\n\n");

    cmd_new("Sin título");
    if (argc >= 2) cmd_open(argv[1]);

    char line[MAX_LINE];
    char *arg;

    while (1) {
        printf("%s%s> ", g_title, g_modified ? "*" : "");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        if (line[0] != ':') {
            if (g_buf) {
                gb_move_to(g_buf, gb_text_size(g_buf));
                gb_insert_str(g_buf, line, strlen(line));
                gb_insert(g_buf, '\n');
                g_modified = 1;
            }
            continue;
        }

        arg = strchr(line, ' ');
        if (arg) { *arg = '\0'; arg++; }

        if      (!strcmp(line, ":new"))       cmd_new(arg);
        else if (!strcmp(line, ":open"))      cmd_open(arg);
        else if (!strcmp(line, ":save"))      cmd_save(arg, 0, 1);
        else if (!strcmp(line, ":savem"))     cmd_save(arg, 1, 1);
        else if (!strcmp(line, ":savec"))     cmd_save(arg, 0, 0);
        else if (!strcmp(line, ":plain")) {
            if (g_buf && arg) {
                char *t = gb_get_text(g_buf);
                if (t) {
                    io_write_plain_naive(arg, t, strlen(t));
                    printf("Guardado plano: '%s' (%zu bytes)\n", arg, strlen(t));
                    free(t);
                }
            }
        }
        else if (!strcmp(line, ":show"))      cmd_show();
        else if (!strcmp(line, ":lines"))     cmd_lines();
        else if (!strcmp(line, ":append"))    cmd_append();
        else if (!strcmp(line, ":delete"))    cmd_delete_line(arg ? atoi(arg) : 0);
        else if (!strcmp(line, ":style")) {
            uint32_t off = 0, len = 0, col = 0xFFFFFFFF;
            int b = 0, i = 0, u = 0;
            if (arg) sscanf(arg, "%u %u %d %d %d %x", &off, &len, &b, &i, &u, &col);
            cmd_add_style(off, len, b, i, u, col);
        }
        else if (!strcmp(line, ":benchmark")) cmd_benchmark();
        else if (!strcmp(line, ":info"))      cmd_info();
        else if (!strcmp(line, ":help"))      print_help();
        else if (!strcmp(line, ":quit") || !strcmp(line, ":q")) {
            if (g_modified) printf("Advertencia: hay cambios sin guardar.\n");
            break;
        }
        else printf("Comando desconocido: '%s'. Usa :help\n", line);
    }

    if (g_buf) gb_free(g_buf);
    printf("Saliendo. ¡Hasta luego!\n");
    return 0;
}
