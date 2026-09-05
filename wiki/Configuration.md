# Configuration

jefetch reads an optional `config.jsonc` file (JSONC = JSON with `//` and `/* */` comments, trailing commas).

## File Lookup (in order)

1. `-c /path/to/config.jsonc`
2. `~/.config/jefetch/config.jsonc`
3. Compiled defaults

`config.jsonc` is auto-created on first run. To start fresh: `rm ~/.config/jefetch/config.jsonc && jefetch`.

## Example

```jsonc
{
    "modules": [
        "title", "separator", "os", "host", "kernel", "uptime", "packages",
        "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
        "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
        "colors"
    ],
    "display": {
        "separator": ": ",
        "keyColor": "bold_cyan",
        "titleColor": "bold_blue",
        "brightColor": true
    },
    "logo": {
        "source": "",  // "" = auto-detect
        // "animation": "spin z speed=1.5"
    }
}
```

## Sections

### `modules`
Ordered list of modules. Each is a bare name or an object with options:

```jsonc
{ "modules": ["os", { "type": "cpu", "temp": true }, "break", "colors"] }
```

### `display`

| Key | Default | Effect |
|-----|---------|--------|
| `separator` | `": "` | Between key and value |
| `keyColor` | `"bold_cyan"` | Info key color |
| `titleColor` | `"bold_blue"` | `user@host` color |
| `padding` | `0` | Left padding |
| `brightColor` | `true` | Use bright/bold |

### `logo`

| Key | Notes |
|-----|-------|
| `source` | Builtin id or `""` to auto-detect |
| `type` | `"builtin"` / `"none"` / `"file"` |
| `color` | `"red"` or per-line `{ "1": "green", "2-4": "blue" }` |
| `padding` | `4` or `{ top, left, right }` (`right` default `4`) |
| `animation` | `off` = static; see [Animation](Animation.md) |
| `style` | `"flat"` or `"3d"` (animation logo style) |
| `chars` | `"ascii"` keeps logo chars, `"blocks"` default, or custom ramp |
| `sharkvis` | `"auto"` (default) / `"on"` / `"off"` — tint + beat slowdown while sharkvis runs; see [Animation](Animation.md) |

## CLI Overrides

| Flag | Effect |
|------|--------|
| `-c <path>` | Use an explicit config |
| `--logo <name>` | Override logo (builtin id) for one run |
| `--no-config` | Ignore all configs, use defaults |
| `--static` | Force static (disable animation) |
| `--structure "os:kernel:"` | Override `modules` order |

Next: [Animation](Animation.md) →