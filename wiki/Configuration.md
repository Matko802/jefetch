# Configuration

Config is located in `~/.config/jefetch/config.jsonc`

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
        "sharkvis": "return=10 boom=10 chars=blocks",
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
| `keyColor` / `titleColor` | bold cyan / blue | Colors for keys and the `user@host` line; with `textcolor=sharkvis` in the animation profile the live gradient always overrides them and they only show when idle |
| `separatorColor` | unset | Color for the separator |
| `padding` | `0` | Left padding |
| `brightColor` | `true` | Bright/bold text |

## Logo

| Key | Notes |
|-----|-------|
| `source` | Builtin id (see `jefetch --list-logos`), a `"~/logo.txt"` file, an image path (see below), or `""` to autodetect |
| `type` | `"builtin"` / `"none"` / `"file"` (with `"source": "~/logo.txt"`) / `"image"` (with `"source": "~/pic.png"`) |
| `color` | `"red"`, or per-line like `{ "1": "green", "2-4": "blue" }` (`$N` slots just work) |
| `padding` | `4`, or `{ top, left, right }` (`right` defaults to `4`; images default to `2`) |
| `width` / `height` | Image logos only: target size in terminal columns / rows (default fits 48 columns, aspect kept). Set one side and the other follows the aspect; set both to stretch |
| `animation` | Needs an explicit `speed` or the logo won't move; `off` turns it off |
| `style` / `chars` | `"flat"` or `"3d"`; `"ascii"` keeps the logo's own glyphs, anything else is shade blocks like fetch. These beat `animation` |
| `sharkvis` | Its own profile for when sharkvis is running (own speed and axes). Base `animation` is ignored meanwhile |

`jefetch --logo arch` swaps the logo for one run. Point `--logo` at an
image file (`jefetch --logo ~/pic.png`) for a one-run image logo.

## Image logos

```jsonc
{ "logo": { "type": "image", "source": "~/Pictures/logo.png", "width": 40 } }
```

Raster images (png, jpeg, gif first frame, bmp) print as a real
image when the terminal supports it — kitty graphics, sixel, then
iTerm2 inline images, auto-detected. Otherwise (unsupported
terminal or piped output) they render as static truecolor
half-blocks (`▀`/`▄`, two image rows per terminal row), so they work
in any terminal with no graphics protocols needed.
Transparency is honored — translucent pixels stay empty so the
background shows through.

With no `type` set, a `source` pointing at an image file is picked up
as an image automatically.

## Animation

The logo actually rotates in 3D. Static output and the animation print the
same text, so nothing breaks when you pipe it.

```jsonc
"animation": "spin y speed=2.0"           
"animation": "spin xyz speed=2.5 speed_z=-1"  
"animation": "spin z speed=1.5 flat chars=ascii"
```

Mix `x` / `y` / `z` however you like, each axis takes its own speed and
negative runs backwards. `speed=N` sets the pace. In a terminal you get the
live view where `t` pauses, `q` / `Esc` / `Ctrl-C` quits, and `--static`
prints a single frame. Saving `config.jsonc` applies while it runs.

## sharkvis music mode

Put `sharkvis` in `animation` (or fill in the `"sharkvis"` key) and the logo
dances while [sharkvis](https://github.com/Matko802/sharkvis) plays, using
that profile only. Your base `animation` sits out meanwhile. With several
sharkvis sessions running, only the newest one is followed and older ones
are ignored.

Only the axes you enabled respond to audio: heavy right channel yaws `y`
right, heavy left yaws it left, matched stereo pitches `x`, energy rolls
`z`. Silence holds still. Each kick dips the logo, it never dims, and
`boom=N` makes it swell with volume.

Colors stay the logo's own unless you add `color=sharkvis`, which hands them
over to sharkvis's `gradient_low` → `gradient_high` from
`~/.config/sharkvis/config.toml`. Add `textcolor=sharkvis` and the text
(keys/title/separator) follows the same gradient while
it plays, always overriding their normal colors (which only show when
idle). Same deal with characters:
`chars=sharkvis` mimics sharkvis's `chars` charset while it plays
(blocks otherwise). Custom `chars=` ramps are ignored — the logo
renders shade blocks (`░▒▓█`) like fetch, unless `chars=ascii` keeps
its original glyphs.

The rotation always keeps winding up while there's sound. `return=N`
eases the logo back to its root position after `N` seconds of silence.
Without it the logo stays where the music left it.

```jsonc
"animation": "spin y speed=2.0 sharkvis"
{ "logo": { "animation": "spin xz flat", "sharkvis": "speed=0 boom=0.3 chars=ascii" } }
```

| Value | What it does |
|-------|--------------|
| `sharkvis` / `=auto` / `=on` | Switch on while `sharkvis` runs (off unless you ask) |
| `sharkvis=off` / `no-sharkvis` | Never hook in |
| `color=sharkvis` | Take logo colors from sharkvis, otherwise the logo keeps its own |
| `color=terminal` | Glide the logo through your terminal's real 16 colors (asked via OSC 4, needs an interactive terminal; falls back to daemon colors, then plain) |
| `textcolor=sharkvis` | Text follows the live gradient too, overriding normal colors (shown when idle) |
| `textcolor=terminal` | Text follows the terminal-color flow too |
| `chars=sharkvis` | Take the charset from sharkvis's `chars`, otherwise blocks |
| `beat=N` | How deep each kick dips, `0`–`0.9` (default `0.6`) |
| `boom=N` | How much it swells with volume, `0`–`1` |
| `grow=N` | Pulse strength, `0`–`0.3` (default `0.12`, `0` turns it off) |
| `return=N` | Seconds of silence before easing back to root position; unset means it never returns |

## CLI Overrides

| Flag | What it does |
|------|--------------|
| `-c <path>` | Use this config file |
| `--logo <name\|path>` | Different logo for one run (builtin id or image file) |
| `--no-config` | Skip configs entirely |
| `--static` | Print once, no animation |
| `--structure "os:kernel:"` | Reorder modules for one run |
