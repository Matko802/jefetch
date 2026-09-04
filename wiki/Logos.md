# Logos

sharkfetch ships **530 logos** — every `fastfetch` logo, matching `fastfetch --list-logos` names.

## Use a Builtin

In `~/.config/sharkfetch/config.jsonc`:

```jsonc
{ "logo": { "source": "arch" } }
```

`source`: `""` auto-detects your OS; any other value looks up a builtin (aliases included); unknown falls back to `linux`.

List all: `sharkfetch --list-logos`

## Override at runtime

```sh
sharkfetch --logo arch
```

`--logo <name>` temporarily overrides the config without editing it.

## Custom Logo (file)

```jsonc
{ "logo": { "type": "file", "source": "~/logo.txt" } }
```

## Colors

- Builtins use `$N` slot colors automatically (e.g. `$1██`) — no setup needed.
- Custom logos: `color: "red"` or per-line `color: { "1": "green", "2-4": "blue" }`.

## Padding

```jsonc
{ "logo": { "padding": 4 } }
// or
{ "logo": { "padding": { "top": 1, "left": 2, "right": 4 } } }
```

`top` inserts blank lines, `left` prefixes spaces, `right` (default 4) is the gap to the text.

Next: [Modules](Modules.md) →