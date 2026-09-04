# Configuration

sharkfetch is **config-compatible with fastfetch's `config.jsonc`**, but its native file is TOML. **Both are supported — JSONC takes precedence if both exist.**

## File Locations (in order)

1. `-c /path/to/config.jsonc` or `-c /path/to/config.toml` (explicit)
2. `~/.config/sharkfetch/config.jsonc` **← preferred if you create it**
3. `~/.config/sharkfetch/config.toml` (auto-created on first run)
4. *fallback*: compiled defaults (`default_structure()`)

If **neither** `config.jsonc` nor `config.toml` exists, `ensure_default_config()` (`src/app.rs:69`) creates `~/.config/sharkfetch/config.toml` from `DEFAULT_TOML_CONFIG` (`src/config/toml_config.rs:15`).

> **To use JSONC:** just `touch ~/.config/sharkfetch/config.jsonc` (or copy `DEFAULT_JSONC_CONFIG` below) — it will be picked up on next run and TOML will be ignored. Delete `config.toml` if you want JSONC-only.

---

## Tabs: TOML vs JSONC (same result)

=== "TOML (`config.toml`)"

    ```toml
    # sharkfetch configuration — auto-generated
    modules = [
        "title", "separator", "os", "host", "kernel", "uptime", "packages",
        "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
        "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
        "colors",
    ]

    [display]
    separator = ": "
    separatorColor = ""
    keyColor = "bold_cyan"
    titleColor = "bold_blue"
    padding = 0
    brightColor = true

    [logo]
    name = ""              # "" = OS auto-detect (nixos on NixOS)
    # name = "arch"
    # animation = "spin"   # see Animation page
    # animation = "spin y speed=2.0"
    # animation = "spin xyz speed=1.5 speed_z=-1"

    [logo.padding]
    top = 0
    left = 0
    right = 4
    ```

=== "JSONC (`config.jsonc`)"

    ```jsonc
    {
        // sharkfetch configuration (JSONC) — fastfetch-compatible
        "modules": [
            "title", "separator", "os", "host", "kernel", "uptime", "packages",
            "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
            "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
            "colors"
        ],
        "display": {
            "separator": ": ",
            "separatorColor": "",
            "keyColor": "bold_cyan",
            "titleColor": "bold_blue",
            "padding": 0,
            "brightColor": true
        },
        "logo": {
            "source": "", // "" = OS auto-detect
            // "animation": "spin",
            // "animation": "spin y speed=2.0",
            "padding": { "top": 0, "left": 0, "right": 4 }
        }
    }
    ```

Both parse to the same `Config` (`src/config/configfile.rs:27`, `src/config/toml_config.rs:105`). The JSONC parser is hand-rolled zero-crate (`src/config/mod.rs:1`) supporting `//` and `/* */` comments + trailing commas.

---

## All Sections

### `modules` (array)

Ordered list of modules. Bare strings or objects with `type`. Unknown names are skipped 1:1 like fastfetch.

```jsonc
"modules": ["title", "separator", "os", {"type": "cpu", "temp": true}, "break", "colors"]
```
```toml
modules = ["title", "separator", "os", "break", "colors"]
```

Available: `title`, `separator`, `os`, `host`, `kernel`, `uptime`, `packages`, `shell`, `display`, `de`/`wm`, `theme`, `icons`, `font`, `cursor`, `terminal`, `terminalfont`, `cpu`, `gpu`, `memory`, `swap`, `disk`, `localip`/`ip`, `battery`, `locale`, `break`, `colors`. See [Modules](Modules).

### `display` (object)

| Key | Type | Default | Effect |
|-----|------|---------|--------|
| `separator` | string | `": "` | Between key and value |
| `separatorColor` | string | `""` | ANSI name or `""` |
| `keyColor` | string | `"bold_cyan"` | `title`/`os` key color |
| `titleColor` | string | `"bold_blue"` | `user@host` color |
| `padding` | int | `0` | Left padding |
| `brightColor` | bool | `true` | Use bright/bold |

If `keyColor`/`titleColor` are unset, they are derived from the logo's `slots[0]`/`slots[1]` (`src/app.rs:335`).

### `logo` (object)

| Key | Type | Notes |
|-----|------|-------|
| `source` / `name` | string | Builtin id (`"nixos"`, `"arch"` …) or `""` = auto-detect via `/etc/os-release` (`src/detection/os.rs`). In JSONC use `source`, in TOML use `name`. |
| `type` | string | `"builtin"` / `"none"` / `"file"` (file path in `source`) |
| `color` | string / map | `color: "red"` or `color: { "1": "green", "2-4": "blue" }` — 1-based line specs (`src/app.rs:721`) |
| `padding` | int / object | `4` or `{top,left,right}` (TOML also supports `[logo.padding]`) |
| `animation` | string | `off`/`static`/`false` = static; `spin`/`spin xyz`/`spin y speed=2.0` etc. — see [Animation](Animation) |
| `width`/`height` | int | Override logo width/height |
| `fontSize` | int | For `terminalfont` |

**Padding:** `top` inserts blank lines, `left` prefixes spaces, `right` (`4` default) is the gap to the text column (`src/app.rs:633`).

---

## CLI Overrides

| Flag | Effect |
|------|--------|
| `-c <path>` | Use explicit config (`.toml` → TOML parser, else JSONC) |
| `--no-config` | Ignore all configs, use compiled defaults |
| `--static` | Force static even if `animation = "spin"` |
| `--structure "os:kernel:uptime"` | Override `modules` order at runtime |

Example:
```sh
sharkfetch -c /tmp/test.toml
sharkfetch -c ~/.config/fastfetch/config.jsonc
sharkfetch --static -c ./my.jsonc
```

---

## Switching Formats

=== "TOML → JSONC"

    ```sh
    # If you already have TOML and want JSONC:
    rm ~/.config/sharkfetch/config.toml
    cat > ~/.config/sharkfetch/config.jsonc <<'JSONC'
    {
        "logo": { "source": "nixos", "animation": "spin y speed=2.0" },
        "modules": ["title","separator","os","kernel","uptime","break","colors"]
    }
    JSONC
    sharkfetch   # now loads config.jsonc
    ```

=== "JSONC → TOML"

    ```sh
    rm ~/.config/sharkfetch/config.jsonc
    sharkfetch   # auto-recreates config.toml on next run
    ```

If both exist, JSONC wins (`src/app.rs:58`). Copy `DEFAULT_JSONC_CONFIG` (`src/config/toml_config.rs:51`) or `DEFAULT_TOML_CONFIG` (`src/config/toml_config.rs:15`) as a starting point.

Next: [Animation](Animation) →
