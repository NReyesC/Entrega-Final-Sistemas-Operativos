CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE
LDLIBS  = -lcrypto

SRCDIR  = src
TOOLDIR = tools
OBJDIR  = obj
BINDIR  = bin

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/editor.c \
       $(SRCDIR)/compress.c \
       $(SRCDIR)/crypto.c \
       $(SRCDIR)/fileformat.c \
       $(SRCDIR)/io.c

OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

TARGET     = $(BINDIR)/editor
GEN_TARGET = $(BINDIR)/gen_test

.PHONY: all clean valgrind check dirs

all: dirs $(TARGET) $(GEN_TARGET)
	@echo ""
	@echo "✓ Compilación exitosa"
	@echo "  Editor:     $(TARGET)"
	@echo "  Gen test:   $(GEN_TARGET)"
	@echo ""
	@echo "Uso rápido:"
	@echo "  ./$(GEN_TARGET) 5 test_5mb.txt"
	@echo "  CEDS_KEY=demo ./$(TARGET) test_5mb.txt"
	@echo "  CEDS_KEY=demo ./benchmark/run_benchmark.sh test_5mb.txt"

dirs:
	@mkdir -p $(OBJDIR) $(BINDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(GEN_TARGET): $(TOOLDIR)/gen_test_file.c
	$(CC) $(CFLAGS) -o $@ $<

valgrind: $(TARGET)
	@echo "Ejecutando valgrind (puede tardar ~30s)..."
	@printf ':append\nHola mundo cifrado\n:end\n:save /tmp/valgrind_test.ceds\n:open /tmp/valgrind_test.ceds\n:quit\n' \
	    | CEDS_KEY=valgrind-demo valgrind --leak-check=full --error-exitcode=1 $(TARGET)

check: $(TARGET)
	@echo "=== Smoke Test: comprimir -> cifrar -> guardar -> cargar ==="
	@printf 'Hola, este es un test del editor CEDS.\nLínea 2.\nLínea 3.\nFin.\n' > /tmp/ceds_smoke.txt
	@printf ':open /tmp/ceds_smoke.txt\n:save /tmp/ceds_smoke.ceds\n:quit\n' | CEDS_KEY=smoke-key $(TARGET) > /dev/null
	@printf ':open /tmp/ceds_smoke.ceds\n:show\n:quit\n' | CEDS_KEY=smoke-key $(TARGET) > /tmp/ceds_smoke.out
	@grep -q 'Hola, este es un test del editor CEDS.' /tmp/ceds_smoke.out
	@printf ':open /tmp/ceds_smoke.txt\n:savec /tmp/ceds_smoke_comp.ceds\n:quit\n' | $(TARGET) > /dev/null
	@printf ':open /tmp/ceds_smoke_comp.ceds\n:show\n:quit\n' | $(TARGET) > /tmp/ceds_smoke_comp.out
	@grep -q 'Línea 3.' /tmp/ceds_smoke_comp.out
	@echo "=== Test completado ==="

clean:
	rm -rf $(OBJDIR) $(BINDIR)
	@echo "Limpieza completada."
