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
animation hot-reload within ~250ms (a broken file is ignored until
it parses again). sharkvis theme edits (colors, charset) apply within
~500ms while it runs.

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

## sharkvis: same colors, charset, beats

Nothing happens unless the animation string asks for it — add `sharkvis`
and, while [`sharkvis`](https://github.com/Matko802/sharkvis) is running,
jefetch borrows its look and groove:

- **Colors**: the spinning logo gets the full vertical gradient —
  `gradient_low` at the bottom → `gradient_high` at the top — just like
  the sharkvis bars (a lone live color is used only when no gradients
  are configured).
- **Charset**: the logo is shaded with sharkvis's `[visualizer] glyphs`
  ramp, unless your animation picks its own (`chars=...` always wins).
- **Beat**: the spin dips on each kick (`speed × (1 − depth × beat)`)
  and the logo pulses bigger (`scale = 1 + grow × beat`). Brightness
  never dims — quiet still shows full color.
- **Direction**: the music steers the logo across all axes — right-heavy
  sound yaws right, left-heavy yaws left, matched stereo pitches up and
  down, overall energy rolls. Audio motion applies on top of the idle
  spin, so even `speed=0` dances.
- **Expand**: `boom=N` sizes the logo up with the volume
  (`scale = 1 + boom × energy`, `0`–`1`).

```jsonc
"animation": "spin y speed=2.0 sharkvis"                 // the trigger: nothing without it
"animation": "spin y sharkvis beat=0.8 grow=0.2"         // deeper dip + bigger pulse
"animation": "spin y sharkvis=off"                       // explicitly off (the default)
"animation": "spin y sharkvis chars=blocks"              // keep jefetch's own ramp
"animation": "spin y sharkvis grow=0"                    // volume speed only, no zoom
```

| Value | Effect |
|-------|--------|
| `sharkvis` | Enable while a `sharkvis` process is running (required — default is off) |
| `sharkvis=auto` | Same as bare `sharkvis` |
| `sharkvis=on` | Same as `auto`: still needs the running process, no exceptions |
| `sharkvis=off` / `no-sharkvis` | Never integrate (default) |
| `beat=N` | Slowdown dip on the beat, `0`–`0.9` (default `0.6`) |
| `boom=N` | Bass size-up `0`–`1` (e.g. `boom=0.3` grows to 1.3x on full bass) |
| `grow=N` | Pulse-bigger depth on the beat, `0`–`0.3` (default `0.12`, `0` disables) |

Or as a separate logo key (the animation string wins when both are set):

```jsonc
{ "logo": { "source": "nixos", "animation": "spin y", "sharkvis": "off" } }
```

How it works:

- **Color**: `gradient_low` → `gradient_high` from the sharkvis config
  (`$SHARKVIS_CONFIG`, `~/.config/sharkvis/config`) as a vertical logo
  gradient. A fresh `$XDG_RUNTIME_DIR/sharkvis/state` file supplies live
  energy, beat, bass, stereo levels and colors (see sharkvis README).
- **Beat**: from the state file when present (tempo grid locked from
  kicks, snares and other onsets across the spectrum — fills soft hits,
  recalibrates on tempo changes, drops in silence), otherwise from a
  tiny built-in PulseAudio monitor (same tracking on 8 kHz mono RMS)
  that only runs while the integration is active.

Next: [Logos](Logos.md) →