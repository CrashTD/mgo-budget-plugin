CC      ?= x86_64-w64-mingw32-gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -Werror -std=c11
NAME    := MGOBudgetPlugin
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
WINE    ?= wine
CLANG_FORMAT ?= clang-format

ifeq ($(origin CC),default)
CC := x86_64-w64-mingw32-gcc
endif

.PHONY: all test format format-check tidy dist clean

all: $(NAME).dll

$(NAME).dll: src/mgo_budget_plugin.c
	$(CC) $(CFLAGS) -shared -static-libgcc -o $@ $< -lversion -Wl,--kill-at

tests/skse_harness.exe: tests/skse_harness.c
	$(CC) $(CFLAGS) -o $@ $<

test: $(NAME).dll tests/skse_harness.exe
	WINE="$(WINE)" tests/run.sh

format:
	$(CLANG_FORMAT) -i src/*.c tests/*.c

format-check:
	$(CLANG_FORMAT) --dry-run -Werror src/*.c tests/*.c

# casting-through-void: GetProcAddress results are cast via void * on purpose (gcc -Wcast-function-type).
TIDY_CHECKS := -*,clang-analyzer-*,bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-reserved-identifier,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-bugprone-casting-through-void

tidy:
	clang-tidy --warnings-as-errors='*' --checks='$(TIDY_CHECKS)' src/*.c -- \
		--target=x86_64-w64-mingw32 -isystem /usr/x86_64-w64-mingw32/include -std=c11

dist: $(NAME).dll
	rm -rf dist && mkdir -p dist/SKSE/Plugins
	cp $(NAME).dll $(NAME).ini dist/SKSE/Plugins/
	cd dist && zip -qr ../$(NAME)-$(VERSION).zip SKSE
	@echo "built $(NAME)-$(VERSION).zip"

clean:
	rm -rf $(NAME).dll tests/skse_harness.exe tests/work dist $(NAME)-*.zip
