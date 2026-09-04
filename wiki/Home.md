# sharkfetch Wiki

A fastfetch clone in pure Rust — static `musl`, Linux-only — with 1:1 output and an areofyl-style 3D spin animation.

| Page | What you'll find |
|------|------------------|
| [Installation](Installation) | Install & update |
| [Configuration](Configuration) | `.toml` vs `.jsonc`, all sections |
| [Animation](Animation) | Spin axes, speed, direction |
| [Logos](Logos) | Builtin / custom, colors, padding |
| [Modules](Modules) | The `modules` list & options |
| [Development](Development) | Build, layout, contributing |
| [FAQ](FAQ) | Quick answers |

## Quick Start

=== "Nix flake"

    ```sh
    nix run github:Matko802/sharkfetch
    ```

=== "Cargo / Make"

    ```sh
    git clone https://github.com/Matko802/sharkfetch && cd sharkfetch
    make deps && make
    sudo make install
    ```

=== "Static binary"

    ```sh
    ./build.sh
    ./target/x86_64-unknown-linux-musl/release/sharkfetch
    ```

First run auto-creates your config. Tabs throughout this wiki show both `TOML` and `JSONC` — pick the one matching your file; JSONC takes precedence if both exist.

Continue to [Installation](Installation) →
