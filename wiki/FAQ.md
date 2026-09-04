# FAQ

## General

**Q: `sharkfetch` vs `fastfetch`?**
A: Drop-in 1:1. Same `modules` order, same logo `$N` carryColor, same `display`/`general`. Run `diff <(fastfetch --json)` if you doubt.

**Q: Why static musl?**
A: `ldd` → `not a dynamic executable`. Ships via `fish-flake`, no glibc mismatch, instant start (`~4ms`).

**Q: Config file not found?**
A: Search order (`src/app.rs:43`):
1. `-c /path` (`.toml` → TOML, else JSONC)
2. `~/.config/sharkfetch/config.jsonc` (preferred if exists)
3. `~/.config/sharkfetch/config.toml` (auto-created)
4. Compiled defaults

If you `touch` an empty `config.jsonc`, it will be used (empty → defaults).

## Configuration

**Q: TOML or JSONC?**
A: Both. JSONC if you want fastfetch compat (`"logo": {"source": "nixos"}`), TOML if you want `name = "nixos"`. JSONC wins if both exist.

**Q: How to switch?**
=== "TOML → JSONC"

    ```sh
    rm ~/.config/sharkfetch/config.toml
    cat > ~/.config/sharkfetch/config.jsonc <<'JSONC'
    {
        "logo": { "source": "nixos", "animation": "spin y speed=2.0" },
        "modules": ["title","separator","os","kernel","uptime","break","colors"]
    }
    JSONC
    ```

=== "JSONC → TOML"

    ```sh
    rm ~/.config/sharkfetch/config.jsonc
    sharkfetch  # recreates config.toml
    ```

**Q: `animation = "spin"` does nothing?**
A: Check it's uncommented, not `off`/`static`/`false`/`0`. And not running with `--static` or `--no-config`. Quit key is `q`/`Esc`/`Ctrl-C`.

## Animation

**Q: How to change spin?**
A: `animation` string (`src/anim.rs:50`):
```toml
animation = "spin y speed=2.0"          # Y only (default)
animation = "spin x"                    # X
animation = "spin z speed=1.5"          # Z (in-plane)
animation = "spin xyz speed=1.0 speed_y=-1"  # all, Y reversed
```
`speed` scales all, `speed_x`/`speed_y`/`speed_z` per-axis, negative = reverse.

**Q: Animation is weird / different stats when spinning?**
A: Fixed in `a8e65d2`/`cfc7e4b`: spinning and static share the same `base_lines` — only logo moves. If you still see different `Packages`/`Display`, you are running the old `fetch` delegate. Update: `git pull` + `./build.sh`.

**Q: No `[press q …]` hint?**
A: Intentionally removed in `4da55de` — animation is clean; `q`/`Ctrl-C` still quits.

**Q: Terminal garbage `^[P1+r` after run?**
A: Fixed: `terminalfont` excluded from `--structure`, `run_capture_timeout` saves/restores termios, `main.rs` drains DCS kitty query.

## Logos

**Q: Logo not found?**
A: `sharkfetch --list-logos | grep -i nixos` — names are case-insensitive, aliases included. `name = ""` auto-detects.

**Q: Custom logo?**
A: Use `type = "file"` + `source = "~/logo.txt"` (TOML) or `"type": "file"` (JSONC). Supports `$N` slot colors and `color: {"1": "red"}`.

## Building

**Q: `cargo build` fails with musl?**
A: Use `./build.sh`, not `cargo build --release`. Nix's `cargo` lacks musl.

**Q: Warnings?**
A: Should be zero. `PADDING_RIGHT` is `#[allow(dead_code)]`, `fastfetch_*` helpers are `#[allow(dead_code)]`.

## Nix

**Q: Update flake input?**
A: In your flake: `nix flake lock --update-input sharkfetch` then `nh os switch`.

**Q: `fish-flake` already uses `github:Matko802/sharkfetch`?**
A: Yes (`fish-flake/flake.nix:22`). The local `path:` was replaced with the GitHub input.

## Contributing

PRs welcome. Keep deps to `libc` only, preserve fastfetch parity, run `./build.sh test` before pushing.
