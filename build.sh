#!/usr/bin/env bash
# Build sharkfetch as a static musl binary using the rustup toolchain.
# On this NixOS box the system `cargo`/`rustc` is a Nix build that lacks the
# musl target, so we route through rustup and pin the toolchain rustc.
set -euo pipefail

export RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}"
export CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}"
TC="$RUSTUP_HOME/toolchains/stable-x86_64-unknown-linux-gnu"

if [ ! -x "$TC/bin/cargo" ]; then
    echo "rustup stable toolchain not found at $TC" >&2
    echo "Run: nix-shell -p rustup --run 'rustup default stable && rustup target add x86_64-unknown-linux-musl'" >&2
    exit 1
fi

MODE="${1:-release}"
TFLAG=""
if [ "$MODE" = "release" ]; then
    TFLAG="--release"
fi

# shellcheck disable=SC2086
if [ "$MODE" = "test" ]; then
    RUSTC="$TC/bin/rustc" "$TC/bin/cargo" test --target x86_64-unknown-linux-musl --lib --tests
    exit $?
fi

# shellcheck disable=SC2086
RUSTC="$TC/bin/rustc" "$TC/bin/cargo" build $TFLAG --target x86_64-unknown-linux-musl

BIN="target/x86_64-unknown-linux-musl/${MODE}/sharkfetch"
echo
echo "Built: $BIN"
echo "To verify it is static:  ldd $BIN  (should say 'not a dynamic executable')"
