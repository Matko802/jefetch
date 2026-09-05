# Animation

The logo truly spins in 3D (areofyl-style engine). Static and animated share the same text output; only the logo moves.

```jsonc
{ "logo": { "source": "nixos", "animation": "spin y speed=2.0" } }
```

No `speed` value, no motion — `spin y` alone stays static. `off` / `static` / `none` also disable it.

`--static` prints one static frame and exits (also used for piped output).
In a terminal you get the live view: `t` pauses/resumes the spin in place,
`q` / `Esc` / `Ctrl-C` quits, info refreshes every second.
Edits to `config.jsonc` apply live (broken files are ignored until they parse again).

## Axes, speed, look

`x` / `y` / `z` in any combo, per-axis speed (negative = reverse):

```jsonc
"animation": "spin y speed=2.0"               // gentle default
"animation": "spin z speed=1.5 flat"          // flat roll, no thickness
"animation": "spin xyz speed=2.5 speed_z=-1"  // tumble, Z reversed
"animation": "spin z speed=1.5 style=3d chars=ascii"  // extruded, original glyphs
```

| Key | Effect |
|-----|--------|
| `speed=N` | Overall multiplier, refresh rate follows automatically |
| `speed_x/y/z=N` | Per-axis multiplier |
| `light=X` | `top-left`, `top-right` (default), `top`, `left`, `right`, `front`, `bottom-left`, `bottom-right`, or `x,y,z` |
| `flat` / `style=flat` | Single-sided plane instead of extruded `3d` |
| `chars=ascii` | Keep the logo's own characters instead of `░▒▓█` |
| `chars=<ramp>` | Custom ramp, first char = darkest (`chars=.,-~:;=!*#$@`) |

Separate logo keys `style` / `chars` win over the animation string.

## sharkvis: music-reactive logo

Add `sharkvis` and, while [`sharkvis`](https://github.com/Matko802/sharkvis)
runs, the logo follows the music — colors, charset, beats, direction, size:

- **Colors**: vertical gradient, `gradient_low` at the bottom → `gradient_high` at the top.
- **Charset**: sharkvis `[visualizer] glyphs`, unless your animation picks its own.
- **Beat**: spin dips per kick, logo pulses bigger. Never dims.
- **Direction**: right-heavy yaws right, left-heavy yaws left, matched stereo pitches, energy rolls — on enabled axes only, base `speed` steps aside while driving.
- **Expand**: `boom=N` sizes up with the volume (`0`–`1`).

```jsonc
"animation": "spin y speed=2.0 sharkvis"                 // needs the token, nothing without it
"animation": "spin y speed=2.0 sharkvis beat=0.8 grow=0"   // dips only, no zoom
```

Or split it into its own key — options there fine-tune the base animation
while sharkvis runs (`style`/`chars` keys still win over both):

```jsonc
{ "logo": { "animation": "spin xz flat", "sharkvis": "speed=0 boom=0.3 chars=ascii" } }
```

| Value | Effect |
|-------|--------|
| `sharkvis` / `=auto` / `=on` | Enable while `sharkvis` runs (default is off) |
| `sharkvis=off` / `no-sharkvis` | Never integrate |
| `beat=N` | Dip depth, `0`–`0.9` (default `0.6`) |
| `boom=N` | Size-up with volume, `0`–`1` |
| `grow=N` | Pulse depth, `0`–`0.3` (default `0.12`, `0` disables) |
| `motion=continuous` | Spin accumulates forever (default) |
| `motion=revert` | Winds toward a full turn, eases back when quiet |

Next: [Logos](Logos.md) →
