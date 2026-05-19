#ifndef PROFILING_H
#define PROFILING_H

/*
 * profiling.h - Utilidades de medición de tiempo en nanosegundos.
 * Usa CLOCK_MONOTONIC: no se ve afectado por ajustes del reloj del sistema.
 *
 * Uso:
 *   PROF_START(mi_label);
 *   // ... código a medir ...
 *   PROF_END(mi_label);
 *   PROF_PRINT(mi_label, "descripción");
 */

#include <time.h>
#include <stdio.h>

/* Retorna el tiempo actual en nanosegundos */
static inline long long prof_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Inicia el temporizador con nombre 'name' */
#define PROF_START(name) \
    long long _prof_##name##_start = prof_now_ns()

/* Detiene el temporizador con nombre 'name' */
#define PROF_END(name) \
    long long _prof_##name##_end = prof_now_ns()

/* Calcula el tiempo transcurrido en milisegundos */
#define PROF_MS(name) \
    ((_prof_##name##_end - _prof_##name##_start) / 1e6)

/* Imprime el resultado del temporizador con una etiqueta descriptiva */
#define PROF_PRINT(name, label) \
    printf("[PROF] %-35s : %.3f ms\n", (label), PROF_MS(name))

#endif /* PROFILING_H */
