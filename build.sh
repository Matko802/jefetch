#!/usr/bin/env bash
# Build jefetch as a static musl binary using the rustup toolchain.
# On this NixOS box the system `cargo`/`rustc` is a Nix build that lacks the
# musl target, so we route through rustup and pin the toolchain rustc.
set -euo pipefail

MODE="${1:-release}"
TFLAG=""
if [ "$MODE" = "release" ]; then
    TFLAG="--release"
fi

# shellcheck disable=SC2086
if [ "$MODE" = "test" ]; then
    if [ -x "${HOME:-$HOME}/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/cargo" ] && "${HOME:-$HOME}/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/cargo" --version >/dev/null 2>&1; then
        RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}" CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}" \
        RUSTC="$HOME/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc" \
        "$HOME/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/cargo" test --target x86_64-unknown-linux-musl --lib --tests
    else
        cargo test --lib --tests
    fi
    exit $?
fi

# Prefer rustup musl for static binary, but fall back to plain cargo (works with normal rust)
if [ -x "${HOME:-$HOME}/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/cargo" ]; then
    TC="$HOME/.rustup/toolchains/stable-x86_64-unknown-linux-gnu"
    export RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}"
    export CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}"
    if "$TC/bin/cargo" --version >/dev/null 2>&1 && "$TC/bin/rustc" --print target-list 2>/dev/null | grep -q "x86_64-unknown-linux-musl"; then
        # shellcheck disable=SC2086
        RUSTC="$TC/bin/rustc" "$TC/bin/cargo" build $TFLAG --target x86_64-unknown-linux-musl
        BIN="target/x86_64-unknown-linux-musl/${MODE}/jefetch"
        echo
        echo "Built (static musl): $BIN"
        echo "To verify it is static:  ldd $BIN  (should say 'not a dynamic executable')"
        exit 0
    fi
fi

if cargo --version >/dev/null 2>&1; then
    # Try musl if the toolchain actually has it (rustup or nix develop)
    if (command -v rustup >/dev/null 2>&1 && rustup target list --installed 2>/dev/null | grep -q "x86_64-unknown-linux-musl") || \
       (cargo --print target-list 2>/dev/null | grep -q "x86_64-unknown-linux-musl" && rustc --print sysroot 2>/dev/null | xargs -I{} sh -c 'ls {}/lib/rustlib/x86_64-unknown-linux-musl/lib/libcore.rlib 2>/dev/null | grep -q .'); then
        echo "Building static musl with system cargo..."
        if cargo build $TFLAG --target x86_64-unknown-linux-musl 2>&1; then
            BIN="target/x86_64-unknown-linux-musl/${MODE}/jefetch"
            echo
            echo "Built (static musl): $BIN"
            echo "To verify it is static:  ldd $BIN  (should say 'not a dynamic executable')"
            exit 0
        fi
        echo "Musl build failed, falling back to dynamic..." >&2
    fi
fi

# Fallback: normal cargo (dynamic, works with vanilla rust)
echo "Building with system cargo (dynamic)..."
# shellcheck disable=SC2086
cargo build $TFLAG
BIN="target/${MODE}/jefetch"
echo
echo "Built: $BIN"
