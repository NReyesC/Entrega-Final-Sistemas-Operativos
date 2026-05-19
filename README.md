# Entrega Final - Sistemas Operativos

## Reto Final: El Triángulo de Hierro (Espacio, Tiempo y Seguridad)

Este repositorio implementa un editor de texto en C para Linux que ahora trabaja con el pipeline correcto en memoria:

**Texto plano en RAM -> Compresión RLE -> Cifrado AES-256-CBC -> write()/mmap()**

El orden importa: si se cifra antes de comprimir, la entropía sube y la compresión deja de ser útil.

## Estructura

```text
.
├── Makefile
├── benchmark/
│   └── run_benchmark.sh
├── src/
│   ├── compress.c/.h
│   ├── crypto.c/.h
│   ├── editor.c/.h
│   ├── fileformat.c/.h
│   ├── io.c/.h
│   ├── main.c
│   └── profiling.h
└── tools/
    └── gen_test_file.c
```

## Compilación

```bash
make
```

> Requiere `gcc` y `libcrypto` de OpenSSL disponibles en el sistema.

## Uso rápido

```bash
./bin/gen_test 5 test_5mb.txt
./bin/editor test_5mb.txt
```

### Comandos principales

```text
:save  archivo.ceds   -> comprime y cifra con write() en bloques de 4KB
:savem archivo.ceds   -> comprime y cifra con mmap()
:savec archivo.ceds   -> comprime sin cifrar (solo para benchmark/comparación)
:plain archivo.txt    -> baseline clásico sin optimización
:benchmark            -> compara A vs B vs C y separa CPU de compresión/cifrado
:open  archivo.ceds   -> abre un archivo .ceds; pide llave si fue cifrado
```

## Gestión segura de la llave

- La llave **no** está quemada en el código.
- La llave **no** se pasa por `argv`.
- El editor la solicita en runtime, o usa `CEDS_KEY` solo para automatizar pruebas y benchmark.
- El buffer de la llave se intenta bloquear con `mlock()` para evitar swap cuando el kernel lo permite.
- Tras cifrar o descifrar, la memoria se destruye con borrado explícito.

## Formato `.ceds`

```text
[FileHeader 64B][StyleEntry x N][CryptoMetadata 32B opcional][payload]
```

- `payload` sin cifrar: RLE comprimido.
- `payload` cifrado: RLE comprimido y luego AES-256-CBC con padding PKCS#7.
- El checksum se calcula sobre el buffer comprimido original para detectar corrupción luego del descifrado.

## Benchmark analítico

```bash
CEDS_KEY=demo ./benchmark/run_benchmark.sh test_5mb.txt
```

El script produce:

- `strace -c` para A. clásico, B. solo compresión, C. compresión + cifrado.
- `time` para separar `real`, `user` y `sys`.
- un resumen interno del editor con tiempos aislados de compresión y cifrado.

## Respuestas de sustentación

### ¿Qué pasa si cifro antes de comprimir?
El archivo deja de comprimirse porque el cifrado genera bytes pseudoaleatorios de alta entropía. Sin patrones repetidos, la compresión no encuentra ahorro.

### ¿Por qué borrar la llave no basta si existe swap?
Porque el kernel podría haber paginado la llave a disco antes del borrado. Por eso el editor intenta usar `mlock()` sobre el buffer sensible.

### ¿Por qué 4096 bytes?
Porque 4096 bytes es el tamaño típico de página y bloque del sistema en Linux/x86-64. Alinear a ese tamaño reduce copias extra y favorece el bus I/O.

## Validación rápida

```bash
make check
make valgrind
```
