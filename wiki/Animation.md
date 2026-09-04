# Animation

sharkfetch embeds a faithful Rust port of areofyl/fetch's 3D engine — the logo truly spins in 3D. Static and animated share the same text output; only the logo moves.

## Enable

Add `animation` to `~/.config/sharkfetch/config.jsonc`:

```jsonc
{ "logo": { "source": "nixos", "animation": "spin y speed=2.0" } }
```

| Value | Effect |
|-------|--------|
| `spin` / `spin y` | Animate (default == `spin y`, speed 2) |
| `off` / `static` / `none` | Static |
| unset | Static |

`--static` forces static even if config says `spin`. Quit: `q`, `Esc`, or `Ctrl-C`.

## Axis & Speed

`animation` accepts `x` / `y` / `z` (any combo) and per-axis speed (negative = reverse):

```jsonc
"animation": "spin y speed=2.0"               // default
"animation": "spin x"                         // pitch
"animation": "spin z speed=1.5"               // roll (in-plane)
"animation": "spin xyz speed=1.5 speed_z=-1"  // all axes, Z reversed
"animation": "off"                            // disable
```

| Key | Effect |
|-----|--------|
| `speed=N` | Overall multiplier |
| `speed_x` / `speed_y` / `speed_z` | Per-axis (negative reverses) |

## Presets

```jsonc
"animation": "spin y speed=2.0"          // gentle, areofyl-like
"animation": "spin xyz speed=2.5"        // fast tumble
"animation": "spin yz speed_y=0.6 speed_z=-1"
"animation": "spin x speed=1.0"
```

Next: [Logos](Logos) →