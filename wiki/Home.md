# sharkfetch Wiki

> Fastfetch clone in pure Rust — static `musl`, Linux-only — 1:1 output, zero bloat.

Welcome to the **sharkfetch** wiki! This is the detailed guide for installing, configuring, and hacking sharkfetch — structured like [ironbar's wiki](https://github.com/JakeStanger/ironbar/wiki) with tabbed examples for every setup.

## Quick Links

| Page | What you'll find |
|------|------------------|
| [Installation](Installation) | Nix flake, Cargo, Make, static binary |
| [Configuration](Configuration) | TOML vs JSONC, `config.toml` vs `config.jsonc` |
| [Animation](Animation) | `spin x/y/z`, speed, direction, areofyl 1:1 math |
| [Logos](Logos) | 530 logos, builtin vs file, `$N` colors, padding |
| [Modules](Modules) | `title`, `os`, `cpu`, `gpu` … all fields + `display`/`general` |
| [Development](Development) | `build.sh`, musl, profiling, adding a module |
| [FAQ](FAQ) | Common gotchas, static check, `q` to quit animation |

---

## 1-Minute Start

=== "Nix (flake)"

    ```nix
    # flake.nix
    inputs.sharkfetch.url = "github:Matko802/sharkfetch";
    # in nixosConfiguration:
    # environment.systemPackages = [ inputs.sharkfetch.packages.x86_64-linux.default ];
    ```
    ```sh
    nix run github:Matko802/sharkfetch
    nix build github:Matko802/sharkfetch && ./result/bin/sharkfetch
    ```

=== "Cargo / Make"

    ```sh
    git clone https://github.com/Matko802/sharkfetch && cd sharkfetch
    make deps && make            # or: ./build.sh
    sudo make install            # /usr/local/bin/sharkfetch
    sharkfetch --static          # force one frame
    ```

=== "Static binary"

    ```sh
    ./build.sh                   # target/x86_64-unknown-linux-musl/release/sharkfetch
    ldd target/x86_64-unknown-linux-musl/release/sharkfetch
    # -> not a dynamic executable
    ```

**First run** auto-creates `~/.config/sharkfetch/config.toml` (TOML). Prefer JSONC? Just `touch ~/.config/sharkfetch/config.jsonc` — JSONC takes precedence over TOML. See [Configuration](Configuration).

## Tabs Convention

Every page uses **tabs** for alternative setups:

=== "TOML"

    ```toml
    [logo]
    name = "nixos"
    animation = "spin y speed=2.0"
    ```

=== "JSONC"

    ```jsonc
    {
        "logo": {
            "source": "nixos",
            "animation": "spin y speed=2.0"
        }
    }
    ```

Pick whichever they exist — sharkfetch detects both. Copy whichever tab matches your file.

---

## Feature Map

- **Drop-in fastfetch** — `sharkfetch` replaces `fastfetch` 1:1 (same `modules` order, same logo `$N` colors).
- **530 logos** — all `fastfetch` `ascii/` logos imported into `src/logo/data.rs`.
- **Zero crates** except `libc` — hand-rolled JSONC + TOML parsers, `OnceLock` caches, termios restore.
- **Static musl** — `x86_64-unknown-linux-musl`, `panic=abort`, `strip=true`.
- **Areofyl 3D spin** — faithful `fetch.c` port (`build_points` → `K1/K2` → `zbuf` → `Blinn-Phong`) with `x`/`y`/`z` axes.

Continue to [Installation](Installation) →
