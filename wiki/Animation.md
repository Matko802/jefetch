# Animation

jefetch embeds a faithful Rust port of areofyl/fetch's 3D engine — the logo truly spins in 3D. Static and animated share the same text output; only the logo moves.

## Enable

Add `animation` to `~/.config/jefetch/config.jsonc`:

```jsonc
{ "logo": { "source": "nixos", "animation": "spin y speed=2.0" } }
```

| Value | Effect |
|-------|--------|
| `spin` / `spin y` | Animate (default == `spin y`, speed 2) |
| `off` / `static` / `none` | Static |
| unset | Static |

`--static` prints one static frame and exits (also used for piped output).
In a terminal you always get the live view instead: `t` pauses/resumes
the spin in place (pose is kept), `q` / `Esc` / `Ctrl-C` quits, and the
system info refreshes every second (uptime, memory, swap, disk, ...).

Edit and save `config.jsonc` while it runs — logo, modules, style and
animation hot-reload within a second (a broken file is ignored until
it parses again).

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
| `speed=N` | Overall multiplier — refresh rate follows automatically so fast spins stay smooth |
| `light=X` | Light direction: `top-left`, `top-right` (default), `top`, `left`, `right`, `front`, `bottom-left`, `bottom-right`, or `x,y,z` — `front` makes faces bright |

```jsonc
"animation": "spin y speed=2.0"          // default, 30 fps
"animation": "spin y speed=6.0"          // 3x faster and auto-raises to ~90 fps
```

## Style: flat or 3d

`3d` (default) extrudes the logo with thickness; `flat` spins a
single-sided plane with no depth. Use a bare word or a `style` key:

```jsonc
"animation": "spin z speed=1.5 flat"    // flat plane
"animation": "spin z style=3d"          // extruded 3d (default)
```

Or as separate logo keys (they win over the animation string):

```jsonc
{ "logo": { "source": "nixos", "animation": "spin z speed=1.5", "style": "flat" } }
```

| Value | Effect |
|-------|--------|
| `flat` / `2d` | Flat plane, no thickness |
| `3d` | Extruded 3d (default) |

## Characters: ascii or custom symbols

By default the 3d logo is shaded with `░▒▓█`. Use the logo's own
characters instead, or any custom ramp (dark → bright):

```jsonc
"animation": "spin z chars=ascii"           // keep original logo chars
"animation": "spin z chars=.,-~:;=!*#$@"    // areofyl ascii ramp
"animation": "spin z chars=blocks"          // back to ░▒▓█
```

Or as a separate logo key:

```jsonc
{ "logo": { "source": "nixos", "animation": "spin z speed=1.5", "chars": "ascii" } }
```

| Value | Effect |
|-------|--------|
| `ascii` / `original` | Keep the logo's own characters |
| `blocks` | `░▒▓█` shading (default) |
| any other text | Custom ramp, first char = darkest |

## Presets

```jsonc
"animation": "spin y speed=2.0"                    // gentle, areofyl-like
"animation": "spin xyz speed=2.5"                  // fast tumble
"animation": "spin yz speed_y=0.6 speed_z=-1"
"animation": "spin x speed=1.0"
"animation": "spin z speed=1.5 flat chars=ascii"    // flat, original chars
"animation": "spin xyz speed=2.0 flat chars=ascii"  // everything in one line (add color=red to force one color)
```

## sharkvis: same color, slow down on the beat

While [`sharkvis`](https://github.com/Matko802/sharkvis) is running,
jefetch can borrow its look and groove: the spinning logo is tinted with
the sharkvis gradient colors and the spin slows down on the beat
(`speed × (1 − depth × beat)`).

```jsonc
"animation": "spin y speed=2.0 sharkvis"          // auto: active while sharkvis runs
"animation": "spin y speed=2.0 sharkvis beat=0.8" // deeper slowdown (default 0.6, max 0.9)
"animation": "spin y sharkvis=off"                // opt out (also: "no-sharkvis")
```

| Value | Effect |
|-------|--------|
| `sharkvis` / `sharkvis=auto` | Integrate when a `sharkvis` process is detected (default) |
| `sharkvis=on` | Always try (state file + monitor, even without a process match) |
| `sharkvis=off` / `no-sharkvis` | Never integrate |
| `beat=N` | Slowdown depth on the beat, `0`–`0.9` (default `0.6`) |

Or as a separate logo key (the animation string wins when both are set):

```jsonc
{ "logo": { "source": "nixos", "animation": "spin y", "sharkvis": "off" } }
```

How it works:

- **Color**: `gradient_low` → `gradient_high` from the sharkvis config
  (`$SHARKVIS_CONFIG`, `~/.config/sharkvis/config`), lerped by the live
  audio energy. A fresh `$XDG_RUNTIME_DIR/sharkvis/state` file
  (`color=#rrggbb energy=0..1 beat=0..1`) wins when present.
- **Beat**: from the state file when present, otherwise from a tiny
  built-in PulseAudio monitor (8 kHz mono RMS + onset envelope) that only
  runs while the integration is active — no new dependencies.

Next: [Logos](Logos.md) →