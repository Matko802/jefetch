# Configuration

Optional `config.jsonc` (JSON with `//` comments, trailing commas).

1. `-c /path/to/config.jsonc`
2. `~/.config/jefetch/config.jsonc` (auto-created on first run)
3. Compiled defaults

To start fresh: `rm ~/.config/jefetch/config.jsonc && jefetch`.

## Example

```jsonc
{
    "modules": [
        "title",
        "separator",
        "os",
        "host",
        "kernel",
        "uptime",
        { "type": "packages", "combined": true },
        "shell",
        "display",
        "wm",
        "theme",
        "icons",
        "font",
        "cursor",
        "terminal",
        "cpu",
        "gpu",
        "memory",
        "swap",
        "disk",
        "localip",
        "locale",
        "break",
        "colors"
    ],
    "display": {
        "separator": "->",
        "separatorColor": "red",
        "keyColor": "",
        "titleColor": "",
        "padding": 1,
        "brightColor": true
    },
    "logo": {
        "source": "cachyos",
        "animation": "spin speed=0 xyz",
        "sharkvis": "motion=revert retract=1 boom=10 chars=blocks",
        "padding": {
            "top": 0,
            "left": 0,
            "right": 0
        }
    }
}
```

## Modules

Ordered list shown in output. Bare name or object with options:

```jsonc
{ "modules": ["os", { "type": "cpu", "temp": true }, "break", "colors"] }
```

`title`, `separator`, `os`, `host`, `kernel`, `uptime`, `packages`, `shell`, `display`, `wm`/`de`, `theme`, `icons`, `font`, `cursor`, `terminal`, `terminalfont`, `cpu`, `gpu`, `memory`, `swap`, `disk`, `localip`/`ip`, `battery`, `locale`, `break`, `colors`.

- `disk.folders`: one path or a list. Without it, all physical disks.
- `packages.combined`: one total instead of per-manager lines.

```sh
jefetch --structure "os:kernel:uptime:break:colors"  # override order at runtime
```

## Display

| Key | Default | Effect |
|-----|---------|--------|
| `separator` | `": "` | Between key and value |
| `keyColor` / `titleColor` | bold cyan / blue | Key and `user@host` colors |
| `padding` | `0` | Left padding |
| `brightColor` | `true` | Bright/bold |

## Logo

| Key | Notes |
|-----|-------|
| `source` | Builtin id (`jefetch --list-logos`) or `""` to auto-detect |
| `type` | `"builtin"` / `"none"` / `"file"` (`"source": "~/logo.txt"`) |
| `color` | `"red"` or per-line `{ "1": "green", "2-4": "blue" }` (`$N` slots work automatically) |
| `padding` | `4` or `{ top, left, right }` (`right` default `4`) |
| `animation` | Needs an explicit `speed` or it stays static; `off` disables |
| `style` / `chars` | `"flat"` or `"3d"`; `"ascii"`, `"blocks"`, or custom ramp — win over `animation` |
| `sharkvis` | Standalone profile used while sharkvis runs (own speed/axes); base `animation` ignored then |

`jefetch --logo arch` overrides the logo for one run.

## Animation

The logo truly spins in 3D. Static and animated share the same text output.

```jsonc
"animation": "spin y speed=2.0"               // gentle default
"animation": "spin xyz speed=2.5 speed_z=-1"  // tumble, Z reversed
"animation": "spin z speed=1.5 flat chars=ascii"
```

`x` / `y` / `z` in any combo, per-axis speed (negative = reverse).
`speed=N` sets the pace (refresh rate follows). In a terminal you get the
live view: `t` pauses/resumes, `q` / `Esc` / `Ctrl-C` quits, `--static` prints one frame.
Edits to `config.jsonc` apply live.

## sharkvis music mode

Add `sharkvis` (or a `"sharkvis"` key) and, while `sharkvis` runs, the logo
follows the music using that profile alone — base `animation` is ignored.
Audio steers only enabled axes (right-heavy yaws `y` right, left-heavy
yaws left, matched stereo pitches `x`, energy rolls `z`); quiet holds still:

- Gradient `gradient_low` → `gradient_high`, sharkvis charset unless yours wins.
- Dips per kick, pulses bigger, never dims. `boom=N` sizes up with volume.
- `motion=continuous` accumulates (default), `motion=revert` winds to a turn then retracts (`retract=N` sets snap-back speed).

```jsonc
"animation": "spin y speed=2.0 sharkvis"
{ "logo": { "animation": "spin xz flat", "sharkvis": "speed=0 boom=0.3 chars=ascii" } }
```

| Value | Effect |
|-------|--------|
| `sharkvis` / `=auto` / `=on` | Enable while `sharkvis` runs (default is off) |
| `sharkvis=off` / `no-sharkvis` | Never integrate |
| `beat=N` | Dip depth, `0`–`0.9` (default `0.6`) |
| `boom=N` | Size-up with volume, `0`–`1` |
| `grow=N` | Pulse depth, `0`–`0.3` (default `0.12`, `0` disables) |
| `motion` / `retract` / `limit` | `continuous` (default) or `revert`; `retract=N` snap-back speed (`0` holds, default `1`); `limit=N` max wind-up in turns, revert only (default `1`) |

## CLI Overrides

| Flag | Effect |
|------|--------|
| `-c <path>` | Explicit config |
| `--logo <name>` | Logo override for one run |
| `--no-config` | Ignore configs |
| `--static` | Static output |
| `--structure "os:kernel:"` | Module order override |
