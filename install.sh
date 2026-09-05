#!/usr/bin/env bash
# Install jefetch from GitHub Releases (static binary, any Linux distro).
# Usage: curl -fsSL https://raw.githubusercontent.com/Matko802/jefetch/main/install.sh | sh
set -euo pipefail

REPO="Matko802/jefetch"

if [ "$(uname -s)" != "Linux" ]; then
    echo "error: Linux only" >&2
    exit 1
fi
case "$(uname -m)" in
    x86_64) ARCH="x86_64" ;;
    aarch64 | arm64) ARCH="aarch64" ;;
    *) echo "error: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading jefetch (linux-$ARCH)..."
curl -fsSL "https://github.com/$REPO/releases/latest/download/jefetch-linux-$ARCH.tar.gz" \
    | tar -xz -C "$TMP"

if [ -w /usr/local/bin ]; then
    install -Dm755 "$TMP/jefetch" /usr/local/bin/jefetch
else
    sudo install -Dm755 "$TMP/jefetch" /usr/local/bin/jefetch
fi

echo "Installed: $(command -v jefetch)"
