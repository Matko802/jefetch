# Logos

sharkfetch ships **530 logos** — every `fastfetch` `ascii/` logo, generated into `src/logo/data.rs` (`LOGOS: &[Logo]` `src/logo/mod.rs:10`). Names/aliases match `fastfetch --list-logos`.

## Using a Logo

=== "TOML"

    ```toml
    [logo]
    name = "arch"          # builtin id, case-insensitive
    # name = ""            # "" = OS auto-detect (reads /etc/os-release)
    # type = "none"        # no logo
    # type = "file"
    # source = "~/my-logo.txt"
    ```

=== "JSONC"

    ```jsonc
    {
        "logo": {
            "source": "arch",
            // "source": ""  // auto-detect
            // "type": "none"
        }
    }
    ```

| `name`/`source` | Result |
|-----------------|--------|
| `""` / unset | Auto-detect (`src/detection/os.rs:detect().id`, e.g. `nixos` on NixOS) → `builtin_logo_v(id)` `src/app.rs:322` |
| `"nixos"`, `"arch"`, `"ubuntu"` … | Exact match via `by_name()` (`src/logo/mod.rs:28`) |
| Alias (`"neon"`, `"manjaro"` …) | Same — `aliases` field |
| Unknown | Falls back `linux` → `unknown` |

List all:
```sh
sharkfetch --list-logos
fastfetch --list-logos   # same names
```

## File / Custom Logos

=== "File"

    ```toml
    [logo]
    type = "file"
    source = "~/logo.txt"   # or "/absolute/path"
    ```
    ```jsonc
    { "logo": { "type": "file", "source": "~/logo.txt" } }
    ```

=== "Inline (JSONC only)"

    ```jsonc
    {
        "logo": {
            "type": "file",
            "source": "line1\nline2\nline3\n"
        }
    }
    ```

`logo_from_lines()` (`src/app.rs:594`) handles color maps and padding.

## Colors — `$N` Slots

Builtins may contain fastfetch `$N` markers (1-based slots). They expand per `src/app.rs:646` `builtin_logo_v`:

```
slots = ["34", "38"]  # SGR payloads
raw line: "$1██ $2██  $$ is one $"
→ "\x1b[1m\x1b[34m██ \x1b[38m██  $ is one $"
```

- `$$` → literal `$`
- `$N` → `\x1b[{slots[N-1]}m` and sets `carryColor` for next line (fastfetch `logoLineCacheBuild` behavior)
- Every line starts with `\x1b[1m` (bold) + `carryColor`, ends with `RESET` (`\x1b[0m`).

Custom logos: `color: "red"` or per-line `color: { "1": "green", "2-4": "blue" }` (`src/app.rs:604`, `src/app.rs:721` `apply_line_spec` 1-based).

## Padding

=== "TOML"

    ```toml
    [logo]
    padding = 4              # all sides
    # or
    padding_top = 1
    padding_left = 2
    padding_right = 4

    [logo.padding]
    top = 0
    left = 0
    right = 4
    ```

=== "JSONC"

    ```jsonc
    {
        "logo": {
            "padding": 4,
            // or { "top": 1, "left": 2, "right": 4 }
        }
    }
    ```

- `top` inserts blank lines (`src/app.rs:620`)
- `left` prefixes spaces (`src/app.rs:626`)
- `right` (`4` default) is gap to text (`src/app.rs:633`, `PADDING_RIGHT`)

Animated mode ignores per-frame `width` jitter — it uses a fixed `60×render_height` canvas (`src/anim.rs:9`, `src/app.rs:243`).

## Logo Cache (fastfetch compat)

`fastfetch` caches logos; sharkfetch reparses each run (hand-rolled parsers, `OnceLock` for `os`/`pci`/`terminal`).

## Adding a New Builtin Logo

1. Add a file to `TXT_SRC` (mirror `fastfetch` `src/logo/ascii/`) or edit `src/logo/data.rs` directly (generated via `tmp/ffgen/` `src/main.rs:../ffgen`).
2. Run the generator to rebuild `data.rs`:
   ```sh
   cargo run --bin ffgen   # writes src/logo/data.rs
   ```
3. `make && sharkfetch --list-logos | grep mylogo`

Next: [Modules](Modules) →
