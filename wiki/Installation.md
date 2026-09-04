# Installation

All methods produce the **same static `musl` binary**. Requirements: Linux x86_64, `cargo` + `rustup` (or Nix).

## Install

=== "Nix flake (recommended)"

    ```sh
    # One-off run
    nix run github:Matko802/sharkfetch -- --static

    # Or add to your flake: packages.x86_64-linux.default / overlay
    ```

=== "Cargo / Make"

    ```sh
    git clone https://github.com/Matko802/sharkfetch && cd sharkfetch
    make deps && make        # == ./build.sh (release, static musl)
    sudo make install        # /usr/local/bin/sharkfetch
    ```

=== "Nix build"

    ```sh
    nix build github:Matko802/sharkfetch
    ./result/bin/sharkfetch
    ```

Makefile targets: `make deps` (musl target), `make` / `make release`, `make debug`, `make test`, `sudo make install`.

## Update

=== "Nix flake"

    ```sh
    nix flake lock --update-input sharkfetch
    ```

=== "Git"

    ```sh
    git pull --rebase origin main
    ./build.sh
    ```

## Verify it's static

```sh
ldd target/x86_64-unknown-linux-musl/release/sharkfetch
# → not a dynamic executable
```

> Use `./build.sh` / `make`, not `cargo build --release` directly — Nix's cargo lacks the musl target and produces a dynamic binary.

Next: [Configuration](Configuration) →
