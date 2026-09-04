# Configuration

sharkfetch reads an optional config file. **Both TOML and JSONC are supported**; JSONC takes precedence if both exist.

## File Lookup (in order)

1. `-c /path/to/config` (`.toml` → TOML, else JSONC)
2. `~/.config/sharkfetch/config.jsonc`
3. `~/.config/sharkfetch/config.toml` (auto-created on first run)
4. Compiled defaults

**To use JSONC:** `rm ~/.config/sharkfetch/config.toml && touch ~/.config/sharkfetch/config.jsonc && sharkfetch` — it will auto-fill `config.jsonc` for you.

## Tabs: TOML vs JSONC

=== "TOML (`config.toml`)"

    ```toml
    modules = [
        "title", "separator", "os", "host", "kernel", "uptime", "packages",
        "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
        "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
        "colors",
    ]

    [display]
    separator = ": "
    keyColor = "bold_cyan"
    titleColor = "bold_blue"
    brightColor = true

    [logo]
    name = ""              # "" = auto-detect (e.g. nixos)
    # animation = "spin y speed=2.0"
    ```

=== "JSONC (`config.jsonc`)"

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
            // "animation": "spin y speed=2.0"
        }
    }
    ```

Both parse to the same config. JSONC supports `//` and `/* */` comments, TOML uses `#`.

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
| `source`/`name` | Builtin id or `""` to auto-detect |
| `type` | `"builtin"` / `"none"` / `"file"` |
| `color` | `"red"` or per-line `{ "1": "green", "2-4": "blue" }` |
| `padding` | `4` or `{ top, left, right }` (`right` default `4`) |
| `animation` | `off` = static; see [Animation](Animation) |

## CLI Overrides

| Flag | Effect |
|------|--------|
| `-c <path>` | Use an explicit config |
| `--no-config` | Ignore all configs, use defaults |
| `--static` | Force static (disable animation) |
| `--structure "os:kernel:"` | Override `modules` order |

Next: [Animation](Animation) →
