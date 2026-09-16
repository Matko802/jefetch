VERSION ?= 0.1.0
PREFIX ?= /usr/local

MUSL_TARGET := $(shell m=$$(uname -m); case "$$m" in x86_64) echo x86_64-unknown-linux-musl;; aarch64|arm64) echo aarch64-unknown-linux-musl;; armv7*|armv6*) echo armv7-unknown-linux-musleabihf;; *) echo "";; esac)

# Static musl when the toolchain knows the host musl target,
# otherwise plain `cargo build --release` (Arch, Arch ARM, vanilla rust).
# On NixOS, run inside `nix develop` or use `nix build`.
all:
	@if [ -n "$(MUSL_TARGET)" ] && rustc --print target-list 2>/dev/null | grep -q "^$(MUSL_TARGET)$$"; then \
		cargo build --release --target $(MUSL_TARGET); \
	else \
		cargo build --release; \
	fi

install: all
	@BIN=""; \
	if [ -n "$(MUSL_TARGET)" ] && [ -f "target/$(MUSL_TARGET)/release/jefetch" ]; then \
		BIN="target/$(MUSL_TARGET)/release/jefetch"; \
	elif [ -f "target/release/jefetch" ]; then \
		BIN="target/release/jefetch"; \
	else \
		BIN=$$(ls -t target/*/release/jefetch 2>/dev/null | head -n1); \
	fi; \
	if [ -z "$$BIN" ]; then echo "No binary found. Run 'make' first." >&2; exit 1; fi; \
	install -Dm755 "$$BIN" "$(DESTDIR)$(PREFIX)/bin/jefetch"

clean:
	cargo clean
	rm -rf result result-*

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jefetch

# Install the build dependencies for the detected distro.
deps:
	@if command -v apt-get >/dev/null 2>&1; then \
		sudo apt-get install -y cargo rustc; \
	elif command -v pacman >/dev/null 2>&1; then \
		sudo pacman -S --needed rust cargo; \
	elif command -v dnf >/dev/null 2>&1; then \
		sudo dnf install -y cargo rust; \
	elif command -v zypper >/dev/null 2>&1; then \
		sudo zypper install -y cargo rust; \
	elif command -v xbps-install >/dev/null 2>&1; then \
		sudo xbps-install -S cargo rust; \
	elif command -v apk >/dev/null 2>&1; then \
		sudo apk add cargo rust; \
	elif command -v emerge >/dev/null 2>&1; then \
		sudo emerge --ask dev-lang/rust dev-lang/rust-bin; \
	elif command -v nix >/dev/null 2>&1; then \
		echo "Nix detected: run 'nix develop' for toolchain"; \
	else \
		echo "Unsupported package manager. Install cargo and rustc."; \
	fi
	@if command -v rustup >/dev/null 2>&1; then \
		if [ -n "$(MUSL_TARGET)" ] && ! rustup target list --installed 2>/dev/null | grep -q "^$(MUSL_TARGET)"; then \
			echo "Adding musl target $(MUSL_TARGET)..."; \
			rustup target add $(MUSL_TARGET) || true; \
		fi; \
	elif command -v cargo >/dev/null 2>&1; then \
		echo "Using system cargo (no rustup) — will build dynamic binary if musl target missing"; \
	fi

.PHONY: all install uninstall clean deps
