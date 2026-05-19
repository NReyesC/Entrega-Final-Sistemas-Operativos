#!/bin/bash
set -e

EDITOR="./bin/editor"
GENTEST="./bin/gen_test"
TESTFILE="${1:-}"
RESULTS_DIR="benchmark/results"
: "${CEDS_KEY:=benchmark-demo-key}"
export CEDS_KEY

mkdir -p "$RESULTS_DIR"

if [ ! -f "$EDITOR" ]; then
    echo "ERROR: '$EDITOR' no encontrado. Ejecuta 'make' primero."
    exit 1
fi

if [ -z "$TESTFILE" ] || [ ! -f "$TESTFILE" ]; then
    TESTFILE="$RESULTS_DIR/test_5mb.txt"
    echo "Generando archivo de prueba de 5 MB..."
    if [ -f "$GENTEST" ]; then
        "$GENTEST" 5 "$TESTFILE"
    else
        dd if=/dev/urandom bs=1M count=5 2>/dev/null | tr -dc 'a-zA-Z \n' | head -c 5242880 > "$TESTFILE"
    fi
fi

FILE_SIZE=$(stat -c%s "$TESTFILE" 2>/dev/null || stat -f%z "$TESTFILE")

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " CEDS Editor — Benchmark Espacio, Tiempo y Seguridad"
echo " Archivo: $TESTFILE"
echo " Tamaño:  $FILE_SIZE bytes"
echo " Llave :  tomada desde CEDS_KEY para no pasarla por argv"
echo "═══════════════════════════════════════════════════════════════"
echo ""

echo "┌─────────────────────────────────────────────────────────────┐"
echo "│ EXPERIMENTO A: Plano clásico                               │"
echo "└─────────────────────────────────────────────────────────────┘"
strace -c -f -e trace=write,open,close \
    bash -c "printf ':open %s\n:plain /tmp/bench_plain_out.txt\n:quit\n' \"$TESTFILE\" | $EDITOR" \
    2>"$RESULTS_DIR/strace_plain.txt" || true
cat "$RESULTS_DIR/strace_plain.txt"
echo ""

echo "┌─────────────────────────────────────────────────────────────┐"
echo "│ EXPERIMENTO B: Solo compresión                             │"
echo "└─────────────────────────────────────────────────────────────┘"
strace -c -f -e trace=write,open,close \
    bash -c "printf ':open %s\n:savec /tmp/bench_comp_out.ceds\n:quit\n' \"$TESTFILE\" | $EDITOR" \
    2>"$RESULTS_DIR/strace_comp.txt" || true
cat "$RESULTS_DIR/strace_comp.txt"
echo ""

echo "┌─────────────────────────────────────────────────────────────┐"
echo "│ EXPERIMENTO C: Compresión + cifrado                        │"
echo "└─────────────────────────────────────────────────────────────┘"
strace -c -f -e trace=write,open,close \
    bash -c "printf ':open %s\n:save /tmp/bench_secure_out.ceds\n:quit\n' \"$TESTFILE\" | $EDITOR" \
    2>"$RESULTS_DIR/strace_secure.txt" || true
cat "$RESULTS_DIR/strace_secure.txt"
echo ""

echo "┌─────────────────────────────────────────────────────────────┐"
echo "│ time: separar CPU (user/sys) e I/O (real)                  │"
echo "└─────────────────────────────────────────────────────────────┘"
echo -n "[A. Plano]      "; { time ( printf ':open %s\n:plain /tmp/t_plain.txt\n:quit\n' "$TESTFILE" | "$EDITOR" >/dev/null 2>&1 ); } 2>&1
echo -n "[B. Compresión] "; { time ( printf ':open %s\n:savec /tmp/t_comp.ceds\n:quit\n' "$TESTFILE" | "$EDITOR" >/dev/null 2>&1 ); } 2>&1
echo -n "[C. Comp+Enc]   "; { time ( printf ':open %s\n:save /tmp/t_secure.ceds\n:quit\n' "$TESTFILE" | "$EDITOR" >/dev/null 2>&1 ); } 2>&1

echo ""
echo "┌─────────────────────────────────────────────────────────────┐"
echo "│ Desglose interno de CPU en el pipeline                     │"
echo "└─────────────────────────────────────────────────────────────┘"
printf ':open %s\n:benchmark\n:quit\n' "$TESTFILE" | "$EDITOR"
