# Configuration

Everything lives in one `config.jsonc` file. Plain JSON, but you can use
`//` comments and trailing commas.

jefetch looks for it here, in order:

1. `-c /path/to/config.jsonc`
2. `~/.config/jefetch/config.jsonc` (created for you on first run)
3. Built-in defaults

Messed it up? `rm ~/.config/jefetch/config.jsonc && jefetch` starts over.

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

Just the list of lines in your output, top to bottom. Each entry is either
a bare name or an object with options:

```jsonc
{ "modules": ["os", { "type": "cpu", "temp": true }, "break", "colors"] }
```

All of them: `title`, `separator`, `os`, `host`, `kernel`, `uptime`,
`packages`, `shell`, `display`, `wm`/`de`, `theme`, `icons`, `font`,
`cursor`, `terminal`, `terminalfont`, `cpu`, `gpu`, `memory`, `swap`,
`disk`, `localip`/`ip`, `battery`, `locale`, `break`, `colors`,
`initsystem`, `lm`.

Two special cases:

- `disk.folders`: give it one path or a list, otherwise it shows every
  physical disk.
- `packages.combined`: `true` collapses everything into one total instead
  of a line per package manager. Same via format:
  `{ "type": "packages", "format": "{all}" }`, and per-manager bits work
  too: `"{nix-system} (nix), {nix-user} (user)"`.
- `colors`: same options as fastfetch — `"symbol"` (`background` default,
  `block`, `circle`, `diamond`, `triangle`, `square`, `star`),
  `"brightness"` (`default`, `normal`, `light`), `"paddingLeft"`, and
  `"block": { "width": 3, "range": [0, 15] }`.

You can also reorder at runtime without touching the file:

```sh
jefetch --structure "os:kernel:uptime:break:colors"
```

## Display

| Key | Default | What it does |
|-----|---------|--------------|
| `separator` | `": "` | Sits between key and value |
| `keyColor` / `titleColor` | bold cyan / blue | Colors for keys and the `user@host` line |
| `padding` | `0` | Left padding |
| `brightColor` | `true` | Bright/bold text |

## Logo

| Key | Notes |
|-----|-------|
| `source` | Builtin id (see `jefetch --list-logos`), or `""` to autodetect |
| `type` | `"builtin"` / `"none"` / `"file"` (with `"source": "~/logo.txt"`) |
| `color` | `"red"`, or per-line like `{ "1": "green", "2-4": "blue" }` (`$N` slots just work) |
| `padding` | `4`, or `{ top, left, right }` (`right` defaults to `4`) |
| `animation` | Needs an explicit `speed` or the logo won't move; `off` turns it off |
| `style` / `chars` | `"flat"` or `"3d"`; `"ascii"`, `"blocks"`, or your own ramp. These beat `animation` |
| `sharkvis` | Its own profile for when sharkvis is running (own speed and axes). Base `animation` is ignored meanwhile |

`jefetch --logo arch` swaps the logo for one run.

## Animation

The logo actually rotates in 3D. Static output and the animation print the
same text, so nothing breaks when you pipe it.

```jsonc
"animation": "spin y speed=2.0"               // gentle default
"animation": "spin xyz speed=2.5 speed_z=-1"  // tumble with Z reversed
"animation": "spin z speed=1.5 flat chars=ascii"
```

Mix `x` / `y` / `z` however you like, each axis takes its own speed and
negative runs backwards. `speed=N` sets the pace. In a terminal you get the
live view where `t` pauses, `q` / `Esc` / `Ctrl-C` quits, and `--static`
prints a single frame. Saving `config.jsonc` applies while it runs.

## sharkvis music mode

Put `sharkvis` in `animation` (or fill in the `"sharkvis"` key) and the logo
dances while [sharkvis](https://github.com/Matko802/sharkvis) plays, using
that profile only. Your base `animation` sits out meanwhile.

Only the axes you enabled respond to audio: heavy right channel yaws `y`
right, heavy left yaws it left, matched stereo pitches `x`, energy rolls
`z`. Silence holds still. Each kick dips the logo, it never dims, and
`boom=N` makes it swell with volume.

Colors stay the logo's own unless you add `color=sharkvis`, which hands them
over to sharkvis's `gradient_low` → `gradient_high`. Same deal with
characters: sharkvis's charset loses if you set your own.

`motion=continuous` (the default) keeps winding up. `motion=revert` winds to
a turn and snaps back, with `retract=N` controlling how fast.

```jsonc
"animation": "spin y speed=2.0 sharkvis"
{ "logo": { "animation": "spin xz flat", "sharkvis": "speed=0 boom=0.3 chars=ascii" } }
```

| Value | What it does |
|-------|--------------|
| `sharkvis` / `=auto` / `=on` | Switch on while `sharkvis` runs (off unless you ask) |
| `sharkvis=off` / `no-sharkvis` | Never hook in |
| `color=sharkvis` | Take colors from sharkvis, otherwise the logo keeps its own |
| `beat=N` | How deep each kick dips, `0`–`0.9` (default `0.6`) |
| `boom=N` | How much it swells with volume, `0`–`1` |
| `grow=N` | Pulse strength, `0`–`0.3` (default `0.12`, `0` turns it off) |
| `motion` / `retract` / `limit` | `continuous` (default) or `revert`; `retract=N` is the snap-back speed (`0` holds, default `1`); `limit=N` caps the wind-up in turns, revert only (default `1`) |

## CLI Overrides

| Flag | What it does |
|------|--------------|
| `-c <path>` | Use this config file |
| `--logo <name>` | Different logo for one run |
| `--no-config` | Skip configs entirely |
| `--static` | Print once, no animation |
| `--structure "os:kernel:"` | Reorder modules for one run |
