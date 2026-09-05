# Modules

Ordered list shown in output. Unknown names are skipped.

```jsonc
{ "modules": ["title", "separator", "os", { "type": "cpu", "temp": true }, "break", "colors"] }
```

`title`, `separator`, `os`, `host`, `kernel`, `uptime`, `packages`, `shell`, `display`, `wm`/`de`, `theme`, `icons`, `font`, `cursor`, `terminal`, `terminalfont`, `cpu`, `gpu`, `memory`, `swap`, `disk`, `localip`/`ip`, `battery`, `locale`, `break`, `colors`.

Options inline or as a top-level key:

```jsonc
{
    "modules": ["disk", "gpu"],
    "disk": { "folders": "/" }
}
```

- `disk.folders`: one path or a list. Without it, all physical disks.
- `packages.combined`: one total instead of per-manager lines.

```sh
jefetch --structure "os:kernel:uptime:break:colors"  # override order at runtime
```

Next: [Development](Development.md) →
