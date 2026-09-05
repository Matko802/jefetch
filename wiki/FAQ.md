# FAQ

**jefetch vs fastfetch?**
Drop-in 1:1 — same modules, logo colors, display.

**Why static musl?**
Instant start, no glibc mismatch. `ldd` → `not a dynamic executable`.

**Config file not found?**
`-c <path>` → `~/.config/jefetch/config.jsonc` → compiled defaults. Delete it to regenerate.

**`"animation": "spin"` does nothing?**
Motion needs an explicit `speed`: `"spin y speed=2.0"`. Also check it's not `off`, and you're not passing `--static`.

**How to change the spin?**
```jsonc
"animation": "spin y speed=2.0"
"animation": "spin xyz speed=1.0 speed_y=-1"  // all axes, Y reversed
```

**Different stats when spinning?**
No — only the logo moves.

**Logo not found?**
`jefetch --list-logos | grep -i nixos`. `"source": ""` auto-detects.

**Custom logo?**
`"type": "file"` + `"source": "~/logo.txt"`, with `$N` colors and `color: {"1": "red"}`.

**`cargo build` fails with musl?**
Use `./build.sh`, not `cargo build --release` directly.

**Warnings?**
Should be zero.
