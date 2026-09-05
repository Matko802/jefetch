# jefetch Wiki

A fastfetch clone in pure Rust — static `musl`, Linux-only — with 1:1 output and an areofyl-style 3D spin animation.

| Page | What you'll find |
|------|------------------|
| [Installation](Installation.md) | Install & update |
| [Configuration](Configuration.md) | `config.jsonc`, all sections |
| [Animation](Animation.md) | Spin axes, speed, direction |
| [Logos](Logos.md) | Builtin / custom, colors, padding |
| [Modules](Modules.md) | The `modules` list & options |
| [Development](Development.md) | Build, layout, contributing |
| [FAQ](FAQ.md) | Quick answers |

## Quick Start

=== "Nix flake"

    ```sh
    nix run github:Matko802/jefetch
    ```

=== "Cargo / Make"

    ```sh
    git clone https://github.com/Matko802/jefetch && cd jefetch
    make deps && make
    sudo make install
    ```

=== "Static binary"

    ```sh
    ./build.sh
    ./target/x86_64-unknown-linux-musl/release/jefetch
    ```

First run auto-creates `~/.config/jefetch/config.jsonc`.

Continue to [Installation](Installation.md) →
