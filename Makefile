VERSION ?= 0.1.0
PREFIX ?= /usr/local

# Build through build.sh which uses rustup musl toolchain for a fully
# static binary. On NixOS, run inside `nix develop` or use `nix build`.
all: target/x86_64-unknown-linux-musl/release/sharkfetch

target/x86_64-unknown-linux-musl/release/sharkfetch: Cargo.toml Cargo.lock $(wildcard src/*.rs) $(wildcard src/**/*.rs)
	./build.sh

install: target/x86_64-unknown-linux-musl/release/sharkfetch
	install -Dm755 target/x86_64-unknown-linux-musl/release/sharkfetch $(DESTDIR)$(PREFIX)/bin/sharkfetch

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
	@if ! rustup target list --installed 2>/dev/null | grep -q "x86_64-unknown-linux-musl"; then \
		echo "Adding musl target..."; \
		rustup target add x86_64-unknown-linux-musl; \
	fi

.PHONY: all install clean deps
