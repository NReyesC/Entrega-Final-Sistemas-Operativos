#ifndef EDITOR_H
#define EDITOR_H

/*
 * editor.h - Gap Buffer: estructura de datos para editores de texto.
 *
 * ¿Por qué Gap Buffer y no un array simple?
 *   - Un array simple requiere O(n) para insertar/borrar en el medio.
 *   - El Gap Buffer mantiene un "hueco" (gap) en la posición del cursor.
 *   - Insertar = copiar al gap → O(1). Mover cursor = desplazar el gap → O(distancia).
 *   - Uso de memoria: un solo bloque contiguo en el heap (mejor para cache).
 *
 * Layout en memoria:
 *
 *   [ texto_izq | ← gap → | texto_der ]
 *     0       gap_start  gap_end    total
 *
 * El "cursor" del editor está en gap_start.
 * El tamaño del texto útil = total - (gap_end - gap_start).
 */

#include <stddef.h>

#define GAP_INITIAL_SIZE 4096   /* Tamaño inicial del buffer (1 página) */
#define GAP_GROW_SIZE    4096   /* Cuánto crece el buffer cuando el gap se agota */

typedef struct {
    char  *buf;        /* Buffer en el heap que contiene texto + gap */
    size_t gap_start;  /* Inicio del gap = posición lógica del cursor */
    size_t gap_end;    /* Fin del gap (exclusive) */
    size_t total;      /* Tamaño total del buffer (texto + gap) */
} GapBuffer;

/* Crea un GapBuffer vacío. Retorna NULL si falla malloc. */
GapBuffer *gb_new(void);

/* Inserta un carácter en la posición del cursor. Retorna -1 si falla. */
int gb_insert(GapBuffer *gb, char c);

/* Inserta una cadena de longitud 'len' en la posición del cursor. */
int gb_insert_str(GapBuffer *gb, const char *str, size_t len);

/* Elimina el carácter antes del cursor (backspace). Retorna -1 si no hay nada. */
int gb_delete(GapBuffer *gb);

/* Mueve el cursor a la posición absoluta 'pos' en el texto lógico. */
int gb_move_to(GapBuffer *gb, size_t pos);

/* Retorna el tamaño del texto (sin contar el gap). */
size_t gb_text_size(const GapBuffer *gb);

/*
 * Retorna el texto completo como string null-terminated en el heap.
 * El caller debe liberar con free().
 */
char *gb_get_text(const GapBuffer *gb);

/* Libera toda la memoria del GapBuffer. */
void gb_free(GapBuffer *gb);

#endif /* EDITOR_H */
