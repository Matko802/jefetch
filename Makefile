VERSION ?= 0.1.0
PREFIX ?= /usr/local

# Build through build.sh — uses rustup musl if available for a fully
# static binary, otherwise falls back to plain `cargo build --release`
# (works with vanilla rust). On NixOS, run inside `nix develop` or use `nix build`.
all:
	./build.sh

install: all
	@if [ -f target/x86_64-unknown-linux-musl/release/sharkfetch ]; then \
		install -Dm755 target/x86_64-unknown-linux-musl/release/sharkfetch $(DESTDIR)$(PREFIX)/bin/sharkfetch; \
	elif [ -f target/release/sharkfetch ]; then \
		install -Dm755 target/release/sharkfetch $(DESTDIR)$(PREFIX)/bin/sharkfetch; \
	else \
		echo "No binary found. Run 'make' first." >&2; exit 1; \
	fi

clean:
	cargo clean
	rm -rf result result-*

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
		if ! rustup target list --installed 2>/dev/null | grep -q "x86_64-unknown-linux-musl"; then \
			echo "Adding musl target..."; \
			rustup target add x86_64-unknown-linux-musl || true; \
		fi; \
	elif command -v cargo >/dev/null 2>&1; then \
		echo "Using system cargo (no rustup) — will build dynamic binary if musl target missing"; \
	fi

.PHONY: all install clean deps
