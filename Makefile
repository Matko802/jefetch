VERSION ?= 0.2.10
PREFIX ?= /usr/local
CC ?= cc
CFLAGS ?= -O2
BUILDDIR ?= build

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS))

TARGET_TRIPLE := $(shell $(CC) -dumpmachine 2>/dev/null || echo unknown)
ifeq ($(findstring musl,$(TARGET_TRIPLE)),musl)
  JEFETCH_LIB := musl
else ifneq ($(findstring GNU libc,$(shell ldd --version 2>/dev/null)),)
  JEFETCH_LIB := glibc
else
  JEFETCH_LIB := unknown
endif

DEFS := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	'-DJEFETCH_TARGET="$(TARGET_TRIPLE)"' '-DJEFETCH_LIB="$(JEFETCH_LIB)"'
STD := -std=c17
WARN := -Wall -Wextra -Wno-trigraphs -Werror=implicit-function-declaration -Werror=implicit-int
INCS := -Isrc
LIBS := -lm -pthread

all: $(BUILDDIR)/jefetch

$(BUILDDIR)/jefetch: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LIBS)

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(STD) $(DEFS) $(WARN) $(INCS) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

install: all
	install -Dm755 "$(BUILDDIR)/jefetch" "$(DESTDIR)$(PREFIX)/bin/jefetch"

clean:
	rm -rf $(BUILDDIR) result result-*

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jefetch

deps:
	@if command -v apt-get >/dev/null 2>&1; then \
		sudo apt-get install -y gcc make; \
	elif command -v pacman >/dev/null 2>&1; then \
		sudo pacman -S --needed gcc make; \
	elif command -v dnf >/dev/null 2>&1; then \
		sudo dnf install -y gcc make; \
	elif command -v zypper >/dev/null 2>&1; then \
		sudo zypper install -y gcc make; \
	elif command -v xbps-install >/dev/null 2>&1; then \
		sudo xbps-install -S gcc make; \
	elif command -v apk >/dev/null 2>&1; then \
		sudo apk add gcc make musl-dev; \
	elif command -v emerge >/dev/null 2>&1; then \
		sudo emerge --ask sys-devel/gcc sys-devel/make; \
	elif command -v nix >/dev/null 2>&1; then \
		echo "Nix detected: run 'nix develop' for toolchain"; \
	else \
		echo "Unsupported package manager. Install gcc and make."; \
	fi

.PHONY: all install uninstall clean deps
