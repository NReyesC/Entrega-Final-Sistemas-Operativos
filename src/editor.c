#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * editor.c - Implementación del Gap Buffer.
 *
 * Gestión de memoria:
 *   - Un solo malloc() en gb_new() para el buffer principal.
 *   - gb_grow() llama a malloc() + memcpy + free() (no realloc, para
 *     mantener control exacto sobre dónde queda el gap en el nuevo buffer).
 *   - gb_free() libera exactamente lo que se reservó: buf y la estructura.
 *   - Sin fugas: cada malloc tiene su free correspondiente.
 */

GapBuffer *gb_new(void) {
    GapBuffer *gb = malloc(sizeof(GapBuffer));
    if (!gb) return NULL;

    gb->buf = malloc(GAP_INITIAL_SIZE);
    if (!gb->buf) {
        free(gb);
        return NULL;
    }

    /* Al inicio todo el buffer es gap: no hay texto, el cursor está al inicio */
    gb->gap_start = 0;
    gb->gap_end   = GAP_INITIAL_SIZE;
    gb->total     = GAP_INITIAL_SIZE;
    return gb;
}

/* ─── Función interna: expande el buffer cuando el gap se agota ─────────── */
static int gb_grow(GapBuffer *gb) {
    size_t new_total = gb->total + GAP_GROW_SIZE;
    char  *new_buf   = malloc(new_total);
    if (!new_buf) return -1;

    /* Copiar texto antes del gap al nuevo buffer */
    memcpy(new_buf, gb->buf, gb->gap_start);

    /* Copiar texto después del gap, dejando el nuevo espacio de gap en el medio */
    size_t after_size = gb->total - gb->gap_end;
    size_t new_gap_end = new_total - after_size;
    memcpy(new_buf + new_gap_end, gb->buf + gb->gap_end, after_size);

    free(gb->buf);         /* liberar el buffer anterior */
    gb->buf     = new_buf;
    gb->gap_end = new_gap_end;
    gb->total   = new_total;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */

int gb_insert(GapBuffer *gb, char c) {
    /* Si el gap está lleno (gap_start == gap_end), necesitamos más espacio */
    if (gb->gap_start == gb->gap_end) {
        if (gb_grow(gb) < 0) return -1;
    }

    /* Insertar en la posición del cursor y avanzar el inicio del gap */
    gb->buf[gb->gap_start] = c;
    gb->gap_start++;
    return 0;
}

int gb_insert_str(GapBuffer *gb, const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (gb_insert(gb, str[i]) < 0) return -1;
    }
    return 0;
}

int gb_delete(GapBuffer *gb) {
    if (gb->gap_start == 0) return -1;  /* Nada antes del cursor */
    gb->gap_start--;  /* Expandir el gap hacia la izquierda = borrar char */
    return 0;
}

int gb_move_to(GapBuffer *gb, size_t pos) {
    size_t text_size = gb_text_size(gb);
    if (pos > text_size) pos = text_size;  /* Clamping al límite del texto */

    if (pos < gb->gap_start) {
        /*
         * Mover cursor a la izquierda:
         * Desplazar el texto que estaba justo antes del gap hacia la derecha
         * (pasarlo al lado derecho del gap).
         *
         * Antes: [aaa|___gap___|bbb]   cursor en gap_start
         * Mover a pos=1: [a|___gap___|aabbb]
         */
        size_t delta = gb->gap_start - pos;
        gb->gap_end   -= delta;
        gb->gap_start -= delta;
        memmove(gb->buf + gb->gap_end, gb->buf + gb->gap_start, delta);

    } else if (pos > gb->gap_start) {
        /*
         * Mover cursor a la derecha:
         * El texto después del gap se mueve al lado izquierdo.
         * 'pos' está en coordenadas lógicas → convertir a coordenadas de buffer.
         */
        size_t buf_pos = pos + (gb->gap_end - gb->gap_start);
        size_t delta = buf_pos - gb->gap_end;
        memmove(gb->buf + gb->gap_start, gb->buf + gb->gap_end, delta);
        gb->gap_start += delta;
        gb->gap_end   += delta;
    }
    return 0;
}

size_t gb_text_size(const GapBuffer *gb) {
    /* El texto útil es el buffer total menos el tamaño del gap */
    return gb->total - (gb->gap_end - gb->gap_start);
}

char *gb_get_text(const GapBuffer *gb) {
    size_t text_size = gb_text_size(gb);
    char  *text = malloc(text_size + 1);  /* +1 para el null terminator */
    if (!text) return NULL;

    /* El texto tiene dos partes separadas por el gap: copiarlas en orden */
    memcpy(text, gb->buf, gb->gap_start);                              /* izquierda */
    memcpy(text + gb->gap_start, gb->buf + gb->gap_end,               /* derecha   */
           gb->total - gb->gap_end);
    text[text_size] = '\0';
    return text;
}

void gb_free(GapBuffer *gb) {
    if (!gb) return;
    free(gb->buf);   /* liberar el buffer de texto */
    free(gb);        /* liberar la estructura misma */
}
