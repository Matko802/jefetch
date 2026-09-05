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
        "keyColor": "bold_cyan",
        "titleColor": "bold_blue",
        "brightColor": true
    },
    "logo": {
        "source": "",  // "" = auto-detect
        "animation": "spin xz flat",
        "sharkvis": "speed=0 boom=10 chars=ascii"
    }
}
```

## Sections

### `modules`

Ordered list shown in output. Bare name or object with options:

```jsonc
{ "modules": ["os", { "type": "cpu", "temp": true }, "break", "colors"] }
```

### `display`

| Key | Default | Effect |
|-----|---------|--------|
| `separator` | `": "` | Between key and value |
| `keyColor` / `titleColor` | bold cyan / blue | Key and `user@host` colors |
| `padding` | `0` | Left padding |
| `brightColor` | `true` | Bright/bold |

### `logo`

| Key | Notes |
|-----|-------|
| `source` | Builtin id or `""` to auto-detect |
| `type` | `"builtin"` / `"none"` / `"file"` |
| `color` | `"red"` or per-line `{ "1": "green", "2-4": "blue" }` |
| `padding` | `4` or `{ top, left, right }` (`right` default `4`) |
| `animation` | `off` = static; needs a `speed`; see [Animation](Animation.md) |
| `style` | `"flat"` or `"3d"` |
| `chars` | `"ascii"`, `"blocks"` (default), or custom ramp |
| `sharkvis` | Mode word or options (`"speed=0 boom=0.3 chars=ascii"`), per-key over `animation` |

## CLI Overrides

| Flag | Effect |
|------|--------|
| `-c <path>` | Explicit config |
| `--logo <name>` | Logo override for one run |
| `--no-config` | Ignore configs |
| `--static` | Static output |
| `--structure "os:kernel:"` | Module order override |

Next: [Animation](Animation.md) →
