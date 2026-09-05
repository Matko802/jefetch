# Logos

530 builtins — every `fastfetch` logo: `jefetch --list-logos`.

```jsonc
{ "logo": { "source": "arch" } }                 // builtin, "" = auto-detect
{ "logo": { "type": "file", "source": "~/logo.txt" } }
```

```sh
jefetch --logo arch   # one-run override
```

- Builtins use `$N` slot colors automatically — no setup needed.
- Custom logos: `color: "red"` or per-line `color: { "1": "green", "2-4": "blue" }`.
- Padding: `4` or `{ top, left, right }` (`right` default `4`).

Next: [Modules](Modules.md) →
