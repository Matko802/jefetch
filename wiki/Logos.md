# Logos

sharkfetch ships **530 logos** — every `fastfetch` logo, matching `fastfetch --list-logos` names.

## Use a Builtin

=== "TOML"

    ```toml
    [logo]
    name = "arch"     # builtin id, case-insensitive
    # name = ""        # "" = auto-detect your OS
    ```

=== "JSONC"

    ```jsonc
    { "logo": { "source": "arch" } }
    ```

`name` / `source`: `""` auto-detects your OS; any other value looks up a builtin (aliases included); unknown falls back to `linux`.

List all: `sharkfetch --list-logos`

## Custom Logo (file)

=== "TOML"

    ```toml
    [logo]
    type = "file"
    source = "~/logo.txt"
    ```

=== "JSONC"

    ```jsonc
    { "logo": { "type": "file", "source": "~/logo.txt" } }
    ```

## Colors

- Builtins use `$N` slot colors automatically (e.g. `$1██`) — no setup needed.
- Custom logos: `color: "red"` or per-line `color: { "1": "green", "2-4": "blue" }`.

## Padding

```toml
[logo]
padding = 4                    # all sides
# or
padding_top = 1
padding_left = 2
padding_right = 4
```

`top` inserts blank lines, `left` prefixes spaces, `right` (default 4) is the gap to the text.

Next: [Modules](Modules) →
