# FAQ

## General

**sharkfetch vs fastfetch?**
Drop-in 1:1. Same modules order, same logo colors, same display/general.

**Why static musl?**
Instant start, no glibc mismatch. `ldd` → `not a dynamic executable`.

**Config file not found?**
Search order: `-c <path>` → `~/.config/sharkfetch/config.jsonc` → `config.toml` (auto-created) → compiled defaults. An empty `config.jsonc` is auto-populated.

## Configuration

**TOML or JSONC?**
Both. JSONC for fastfetch compat, TOML for `name = "nixos"`. JSONC wins if both exist.

**How to switch to JSONC?**
```sh
rm ~/.config/sharkfetch/config.toml
touch ~/.config/sharkfetch/config.jsonc
sharkfetch   # fills config.jsonc automatically
```

**`animation = "spin"` does nothing?**
Check it's uncommented, not `off`/`static`/`false`, and you're not running `--static`/`--no-config`. Quit with `q`/`Esc`/`Ctrl-C`.

## Animation

**How to change the spin?**
```toml
animation = "spin y speed=2.0"               # default (Y only)
animation = "spin x"
animation = "spin z speed=1.5"
animation = "spin xyz speed=1.0 speed_y=-1"  # all axes, Y reversed
```

**Different stats when spinning?**
Nope — spinning and static share the same text output; only the logo moves.

## Logos

**Logo not found?**
`sharkfetch --list-logos | grep -i nixos`. Use `name = ""` to auto-detect.

**Custom logo?**
`type = "file"` + `source = "~/logo.txt"`. Supports `$N` colors and `color: {"1": "red"}`.

## Building

**`cargo build` fails with musl?**
Use `./build.sh`, not `cargo build --release` directly.

**Warnings?**
Should be zero.
