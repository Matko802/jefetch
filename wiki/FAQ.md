# FAQ

## General

**jefetch vs fastfetch?**
Drop-in 1:1. Same modules order, same logo colors, same display/general.

**Why static musl?**
Instant start, no glibc mismatch. `ldd` → `not a dynamic executable`.

**Config file not found?**
Search order: `-c <path>` → `~/.config/jefetch/config.jsonc` → compiled defaults. An empty `config.jsonc` is auto-populated.

## Configuration

**What format is the config?**
JSONC only — `~/.config/jefetch/config.jsonc` (supports `//` and `/* */` comments, trailing commas).

**How to start fresh?**
```sh
rm ~/.config/jefetch/config.jsonc
jefetch   # recreates config.jsonc
```

**`"animation": "spin"` does nothing?**
Check it's uncommented, not `off`/`static`/`false`, and you're not running `--static`/`--no-config`. Quit with `q`/`Esc`/`Ctrl-C`.

## Animation

**How to change the spin?**
```jsonc
"animation": "spin y speed=2.0"               // default (Y only)
"animation": "spin x"
"animation": "spin z speed=1.5"
"animation": "spin xyz speed=1.0 speed_y=-1"  // all axes, Y reversed
```

**Different stats when spinning?**
Nope — spinning and static share the same text output; only the logo moves.

## Logos

**Logo not found?**
`jefetch --list-logos | grep -i nixos`. Use `"source": ""` to auto-detect.

**Custom logo?**
`"type": "file"` + `"source": "~/logo.txt"`. Supports `$N` colors and `color: {"1": "red"}`.

## Building

**`cargo build` fails with musl?**
Use `./build.sh`, not `cargo build --release` directly.

**Warnings?**
Should be zero.